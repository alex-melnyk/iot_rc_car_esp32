/*
 * Self-balance bring-up (MPU-6050 via Adafruit lib)  ->  Nano ESP32 + L293D
 * ------------------------------------------------------------
 * Balances the car on ONE axle (default = rear wheels M3+M4) using the
 * MPU-6050 (Adafruit library) with a complementary filter + PID.
 *
 * SAFE BRING-UP: starts in CALIBRATE mode (motors OFF, prints the angle).
 *   1. Confirm the angle TRACKS as you tip the sensor.
 *   2. Hold at the balance point, read the angle -> set SETPOINT.
 *   3. Set CALIBRATE 0, low Kp, sign-check, then tune.
 *
 * MPU-6050 wiring: VCC->3V3, GND->GND, SCL->A5, SDA->A4.
 * Shield wiring: same as car_l293d.  Serial 115200.
 */
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// ================= CONFIG (set from CALIBRATE readings) =====================
#define CALIBRATE   0          // 1 = print angle only, motors OFF (safe)
const bool  USE_PITCH   = true;    // true: pitch + gyroY ; false: roll + gyroX
float       SETPOINT    = 67.6;    // measured balance angle over the rear axle
const float OUTPUT_SIGN = -1.0;    // flipped: wheels now chase the lean
float Kp = 20.0, Ki = 0.0, Kd = 0.8;
const int   MAXOUT     = 220;
const int   MIN_DRIVE  = 55;       // feedforward kick to beat TT-motor deadband
const float ERR_DEAD   = 1.0;      // deg: within this of setpoint = coast
const float FALL_LIMIT = 40.0;     // deg from setpoint = fallen -> disarm
const float ARM_WINDOW = 8.0;      // deg from setpoint to auto-re-arm after a fall
const float I_MAX      = 400.0;
// ============================================================================

// ---- L293D shield pins (same as car_l293d) ----
const int DIR_LATCH = D2, DIR_CLK = D3, DIR_SER = D4;
const int PWM_M1 = D5, PWM_M2 = D6, PWM_M3 = D8, PWM_M4 = D7;   // M3=D8, M4=D7
#define M1_A 2
#define M1_B 3
#define M2_A 1
#define M2_B 4
#define M3_A 5
#define M3_B 7
#define M4_A 0
#define M4_B 6
uint8_t latch_state = 0;

// balancing axle = wheels on the ground (default rear M3+M4)
#define BAL1_A M3_A
#define BAL1_B M3_B
#define BAL1_PWM PWM_M3
#define BAL2_A M4_A
#define BAL2_B M4_B
#define BAL2_PWM PWM_M4

Adafruit_MPU6050 mpu;

// ---- motor driver ----
void latch_tx(){
  digitalWrite(DIR_LATCH, LOW); digitalWrite(DIR_CLK, LOW);
  for(int i=0;i<8;i++){
    digitalWrite(DIR_SER, (latch_state&(0x80>>i))?HIGH:LOW);
    delayMicroseconds(5); digitalWrite(DIR_CLK, HIGH);
    delayMicroseconds(5); digitalWrite(DIR_CLK, LOW);
  }
  digitalWrite(DIR_LATCH, HIGH); delayMicroseconds(5);
}
void setDir(int aBit,int bBit,int dir){
  if(dir>0){ latch_state|=(1<<aBit); latch_state&=~(1<<bBit); }
  else if(dir<0){ latch_state&=~(1<<aBit); latch_state|=(1<<bBit); }
  else { latch_state&=~(1<<aBit); latch_state&=~(1<<bBit); }
}
void motorRaw(int aBit,int bBit,int pwmPin,int speed){
  setDir(aBit,bBit, speed>0?1:(speed<0?-1:0));
  int d=abs(speed); if(d>255)d=255; analogWrite(pwmPin,d);
}
void balanceDrive(int u){ motorRaw(BAL1_A,BAL1_B,BAL1_PWM,u); motorRaw(BAL2_A,BAL2_B,BAL2_PWM,u); latch_tx(); }
void motorsOff(){ analogWrite(BAL1_PWM,0); analogWrite(BAL2_PWM,0); latch_state=0; latch_tx(); }

float angle = 0, integ = 0;
float biasX = 0, biasY = 0;
bool balancing = false;            // armed/balancing vs idle (fallen)
unsigned long lastUs = 0;

void readIMU(float &pitchAcc, float &rollAcc, float &gxDps, float &gyDps){
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  float ax=a.acceleration.x, ay=a.acceleration.y, az=a.acceleration.z;
  pitchAcc = atan2(-ax, sqrt(ay*ay+az*az)) * 57.2958;
  rollAcc  = atan2(ay, az) * 57.2958;
  gxDps = g.gyro.x * 57.2958;    // rad/s -> deg/s
  gyDps = g.gyro.y * 57.2958;
}

void calibrateGyro(){
  Serial.println("Gyro bias calibration - hold still...");
  double sx=0, sy=0; const int N=400;
  for(int i=0;i<N;i++){ float p,r,gx,gy; readIMU(p,r,gx,gy); sx+=gx; sy+=gy; delay(3); }
  biasX=sx/N; biasY=sy/N;
  Serial.printf("bias gX=%.2f gY=%.2f dps\n", biasX, biasY);
}

void setup(){
  Serial.begin(115200); delay(400);
  int outs[]={DIR_LATCH,DIR_CLK,DIR_SER,PWM_M1,PWM_M2,PWM_M3,PWM_M4};
  for(int p:outs) pinMode(p,OUTPUT);
  motorsOff();

  Wire.begin();
  if(!mpu.begin()){ Serial.println("MPU begin FAILED - check wiring"); while(1) delay(100); }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
  Serial.println("MPU OK");

  calibrateGyro();
  lastUs=micros();
  Serial.println(CALIBRATE ? "CALIBRATE mode - motors OFF. Tip it, read the angle."
                           : "BALANCE mode - stand it up!");
}

void loop(){
  float pitchAcc, rollAcc, gx, gy; readIMU(pitchAcc, rollAcc, gx, gy);
  gx-=biasX; gy-=biasY;
  float accAngle = USE_PITCH ? pitchAcc : rollAcc;
  float rate     = USE_PITCH ? gy : gx;

  unsigned long now=micros(); float dt=(now-lastUs)/1e6; lastUs=now;
  if(dt<=0 || dt>0.2) dt=0.004;

#if CALIBRATE
  angle = 0.98*(angle + rate*dt) + 0.02*accAngle;
  angle = constrain(angle, -120.0, 120.0);
  static unsigned long t=0;
  if(millis()-t>100){ t=millis();
    Serial.printf("angle=%6.1f (pitchAcc=%6.1f rollAcc=%6.1f gY=%6.1f gX=%6.1f)\n",
                  angle, pitchAcc, rollAcc, gy, gx);
  }
  balanceDrive(0);
#else
  if(!balancing){
    // IDLE/FALLEN: motors off, track true angle from accel, auto-re-arm on stand-up
    angle = accAngle;
    integ = 0;
    motorsOff();
    if(fabs(accAngle - SETPOINT) < ARM_WINDOW) balancing = true;   // stood back up
  } else {
    angle = 0.98*(angle + rate*dt) + 0.02*accAngle;                // fuse while balancing
    float err = angle - SETPOINT;
    if(fabs(err) > FALL_LIMIT){ balancing = false; integ = 0; motorsOff(); }  // fell -> disarm
    else if(fabs(err) < ERR_DEAD){ balanceDrive(0); }
    else{
      integ += err*dt; integ = constrain(integ, -I_MAX, I_MAX);
      float u = (Kp*err + Ki*integ + Kd*rate) * OUTPUT_SIGN;
      int out = (int)u + (u > 0 ? MIN_DRIVE : -MIN_DRIVE);         // feedforward kick
      out = constrain(out, -MAXOUT, MAXOUT);
      balanceDrive(out);
    }
  }
  static unsigned long t=0;
  if(millis()-t>150){ t=millis();
    Serial.printf("%s angle=%6.1f err=%6.1f\n", balancing?"BAL ":"idle", angle, angle-SETPOINT);
  }
#endif
  delay(4);
}

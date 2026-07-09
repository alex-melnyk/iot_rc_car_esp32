/*
 * ESP-NOW RC - CAR on the L293D shield   ->  Arduino Nano ESP32
 * ------------------------------------------------------------
 * Drives the MH-Electronics / Adafruit-v1 L293D motor shield by
 * bit-banging its 74HC595 (direction) and PWMing the enable pins.
 * All 4 motors: M1,M2 = LEFT ; M3,M4 = RIGHT (tank steering).
 *
 * Nano ESP32 -> L293D shield header holes (by printed number):
 *   D2 -> "12"  (DIR_LATCH, 74HC595 latch)
 *   D3 -> "4"   (DIR_CLK,   74HC595 clock)
 *   D4 -> "8"   (DIR_SER,   74HC595 data)
 *   D5 -> "11"  (M1 PWM/enable)
 *   D6 -> "3"   (M2 PWM/enable)
 *   D7 -> "5"   (M4 PWM/enable  -- hole 5 is Adafruit M4)
 *   D8 -> "6"   (M3 PWM/enable  -- hole 6 is Adafruit M3)
 *   shield "7"  -> GND        (DIR_EN low = enable 595 outputs)
 *   Nano 5V     -> shield 5V  (chip logic power)
 *   Nano GND    -> shield GND (common ground)
 *   Battery 11.1V -> shield EXT_PWR (+M/GND), PWR jumper in place
 *
 * NOTE: the 74HC595 runs at 5V and the Nano outputs 3.3V, which is
 * marginal for its HIGH threshold. If directions act erratic, that's
 * the cause and we add a level shifter (or move to an Uno).
 *
 * Flash with core: esp32:esp32   board: Arduino Nano ESP32 (nano_nora)
 */
#include <esp_now.h>
#include <WiFi.h>

// ---- 74HC595 control pins ----
const int DIR_LATCH = D2;
const int DIR_CLK   = D3;
const int DIR_SER   = D4;

// ---- motor enable (PWM) pins ----
const int PWM_M1 = D5;   // left
const int PWM_M2 = D6;   // left
const int PWM_M3 = D8;   // right  (shield hole 6 -- Adafruit M3 PWM)
const int PWM_M4 = D7;   // right  (shield hole 5 -- Adafruit M4 PWM)

// ---- 74HC595 output-bit assignments (Adafruit Motor Shield v1) ----
#define M1_A 2
#define M1_B 3
#define M2_A 1
#define M2_B 4
#define M3_A 5
#define M3_B 7
#define M4_A 0
#define M4_B 6

const int MAX_DUTY = 180;              // speed cap (0-255); ~70% on 11.1V
const int DEADZONE = 300;              // raw-ADC counts around center
const unsigned long FAILSAFE_MS = 400;
volatile unsigned long lastRecv = 0;

uint8_t latch_state = 0;

volatile bool demoRequested = false;
volatile bool demoActive = false;

typedef struct { int16_t x; int16_t y; uint8_t btn; } Packet;
Packet data;

void latch_tx() {
  // slow, clean shift-out: data settles before each rising clock edge.
  // The delays matter because the 595 runs at 5V and the Nano drives 3.3V
  // (marginal HIGH) -- giving the signal time to settle makes it reliable.
  digitalWrite(DIR_LATCH, LOW);
  digitalWrite(DIR_CLK, LOW);
  for (int i = 0; i < 8; i++) {
    digitalWrite(DIR_SER, (latch_state & (0x80 >> i)) ? HIGH : LOW);
    delayMicroseconds(5);
    digitalWrite(DIR_CLK, HIGH);
    delayMicroseconds(5);
    digitalWrite(DIR_CLK, LOW);
  }
  digitalWrite(DIR_LATCH, HIGH);
  delayMicroseconds(5);
}

void setDir(int aBit, int bBit, int dir) {   // dir: +1 fwd, -1 rev, 0 stop
  if (dir > 0)      { latch_state |=  (1 << aBit); latch_state &= ~(1 << bBit); }
  else if (dir < 0) { latch_state &= ~(1 << aBit); latch_state |=  (1 << bBit); }
  else              { latch_state &= ~(1 << aBit); latch_state &= ~(1 << bBit); }
}

void driveMotor(int aBit, int bBit, int pwmPin, int speed) {
  setDir(aBit, bBit, speed > 0 ? 1 : (speed < 0 ? -1 : 0));
  int duty = abs(speed);
  if (duty > 255) duty = 255;
  analogWrite(pwmPin, duty);
}

void stopAll() {
  analogWrite(PWM_M1, 0); analogWrite(PWM_M2, 0);
  analogWrite(PWM_M3, 0); analogWrite(PWM_M4, 0);
  latch_state = 0; latch_tx();
}

// drive both sides at once: +speed = forward (uses the corner mapping)
void drive(int left, int right) {
  driveMotor(M2_A, M2_B, PWM_M2, left);    // front-left
  driveMotor(M3_A, M3_B, PWM_M3, left);    // back-left
  driveMotor(M1_A, M1_B, PWM_M1, right);   // front-right
  driveMotor(M4_A, M4_B, PWM_M4, right);   // back-right
  latch_tx();
}

// autonomous demo show: spins, straights, a figure-8, then a wiggle finale
void runDemo() {
  demoActive = true;
  Serial.println("DEMO show!");
  const int D = MAX_DUTY;
  const int I = D * 45 / 100;           // inner-wheel speed during curves

  drive(-D,  D); delay(900);            // spin left
  drive( D, -D); delay(900);            // spin right
  drive( D,  D); delay(700);            // forward
  drive(-D, -D); delay(700);            // back

  // figure-8: one big loop curving right, then one curving left
  drive( D,  I); delay(2800);           // right loop
  drive( I,  D); delay(2800);           // left loop -> completes the 8

  drive( D,  D); delay(600);            // dash forward
  for (int i = 0; i < 6; i++) {         // wiggle finale
    drive(-D,  D); delay(220);
    drive( D, -D); delay(220);
  }
  drive(0, 0);
  demoActive = false;
}

void onRecv(const esp_now_recv_info_t *info, const uint8_t *incoming, int len) {
  if (len != sizeof(Packet)) return;
  memcpy(&data, incoming, sizeof(Packet));
  lastRecv = millis();

  // triple-click the joystick button (within 1.2s) -> run the demo show
  static uint8_t lastBtn = 0;
  static int pressCount = 0;
  static unsigned long lastPress = 0;
  if (data.btn == 1 && lastBtn == 0) {          // button press (rising edge)
    unsigned long now = millis();
    if (now - lastPress > 1200) pressCount = 0; // window expired -> restart
    pressCount++;
    lastPress = now;
    if (pressCount >= 3) { pressCount = 0; demoRequested = true; }
  }
  lastBtn = data.btn;

  if (demoActive) return;         // demo owns the motors; ignore the stick

  int throttle = data.y - 2048;
  int steer    = data.x - 2048;
  if (abs(throttle) < DEADZONE) throttle = 0;
  if (abs(steer)    < DEADZONE) steer    = 0;
  throttle = map(throttle, -2048, 2048, -255, 255);
  steer    = map(steer,    -2048, 2048, -255, 255);

  int left  = constrain(throttle + steer, -255, 255);
  int right = constrain(throttle - steer, -255, 255);
  left  = map(left,  -255, 255, -MAX_DUTY, MAX_DUTY);
  right = map(right, -255, 255, -MAX_DUTY, MAX_DUTY);

  // Corners: M1=front-right, M2=front-left, M3=back-left, M4=back-right
  // LEFT side = M2+M3 | RIGHT side = M1+M4  (handled inside drive())
  drive(left, right);

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 200) {
    lastPrint = millis();
    Serial.printf("RX  left=%4d  right=%4d  btn=%d\n", left, right, data.btn);
  }
}

void setup() {
  Serial.begin(115200);
  int outs[] = {DIR_LATCH, DIR_CLK, DIR_SER, PWM_M1, PWM_M2, PWM_M3, PWM_M4};
  for (int p : outs) pinMode(p, OUTPUT);
  stopAll();

  WiFi.mode(WIFI_STA);
  Serial.print("Car MAC: "); Serial.println(WiFi.macAddress());
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); ESP.restart(); }
  esp_now_register_recv_cb(onRecv);
  Serial.println("Car (L293D) ready. Listening...");
}

void loop() {
  if (demoRequested) { demoRequested = false; runDemo(); }
  if (!demoActive && millis() - lastRecv > FAILSAFE_MS) stopAll();
  delay(10);
}

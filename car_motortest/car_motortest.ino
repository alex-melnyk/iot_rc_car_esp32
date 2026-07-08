/*
 * L293D shield MOTOR DIAGNOSTIC  ->  Arduino Nano ESP32
 * ------------------------------------------------------------
 * No radio. Drives ONE motor at a time, forward then reverse,
 * printing what it's doing. Watch which motor/direction is wrong.
 * Same wiring as car_l293d.
 */
const int DIR_LATCH = D2, DIR_CLK = D3, DIR_SER = D4;
// NOTE: shield PWM pins are M3=hole6, M4=hole5 (Adafruit mapping).
// Physical wiring unchanged: D7->hole5, D8->hole6 -> so M3=D8, M4=D7.
const int PWM_M1 = D5, PWM_M2 = D6, PWM_M3 = D8, PWM_M4 = D7;

#define M1_A 2
#define M1_B 3
#define M2_A 1
#define M2_B 4
#define M3_A 5
#define M3_B 7
#define M4_A 0
#define M4_B 6

const int DUTY = 150;     // wheels up, so a clear speed
uint8_t latch_state = 0;

void latch_tx() {
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
}

void setDir(int aBit, int bBit, int dir) {
  if (dir > 0)      { latch_state |=  (1 << aBit); latch_state &= ~(1 << bBit); }
  else if (dir < 0) { latch_state &= ~(1 << aBit); latch_state |=  (1 << bBit); }
  else              { latch_state &= ~(1 << aBit); latch_state &= ~(1 << bBit); }
  latch_tx();
}

void run(int aBit, int bBit, int pwmPin, int dir, const char *label) {
  Serial.println(label);
  setDir(aBit, bBit, dir);
  analogWrite(pwmPin, dir == 0 ? 0 : DUTY);
}

void allStop() {
  analogWrite(PWM_M1, 0); analogWrite(PWM_M2, 0);
  analogWrite(PWM_M3, 0); analogWrite(PWM_M4, 0);
  latch_state = 0; latch_tx();
}

void setup() {
  Serial.begin(115200);
  int outs[] = {DIR_LATCH, DIR_CLK, DIR_SER, PWM_M1, PWM_M2, PWM_M3, PWM_M4};
  for (int p : outs) pinMode(p, OUTPUT);
  allStop();
  Serial.println("Motor diagnostic starting...");
}

void step(int aBit, int bBit, int pwmPin, const char *name) {
  run(aBit, bBit, pwmPin, +1, name);       // forward
  delay(1500);
  allStop(); delay(600);
  run(aBit, bBit, pwmPin, -1, name);       // reverse
  delay(1500);
  allStop(); delay(600);
}

void loop() {
  step(M1_A, M1_B, PWM_M1, "M1 forward / then reverse");
  step(M2_A, M2_B, PWM_M2, "M2 forward / then reverse");
  step(M3_A, M3_B, PWM_M3, "M3 forward / then reverse");
  step(M4_A, M4_B, PWM_M4, "M4 forward / then reverse");
  Serial.println("--- cycle complete, repeating ---");
  delay(1000);
}

/*
 * ESP-NOW RC - REMOTE (transmitter)   ->  Arduino Nano ESP32
 * ------------------------------------------------------------
 * Reads the joystick and broadcasts {x, y, button} ~50x/sec via ESP-NOW.
 * Broadcast means no MAC pairing needed - the car just listens.
 *
 * Joystick (KY-023) -> Nano ESP32:
 *   GND -> GND
 *   +5V -> 3V3   (!! 3.3V, NOT 5V - the ESP ADC maxes at 3.3V)
 *   VRx -> A0
 *   VRy -> A1
 *   SW  -> D3
 *
 * Flash with core: esp32:esp32   board: Arduino Nano ESP32 (nano_nora)
 */
#include <esp_now.h>
#include <WiFi.h>

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const int PIN_X  = A0;
const int PIN_Y  = A1;
const int PIN_SW = D3;

typedef struct {
  int16_t x;      // 0..4095 (raw ADC)
  int16_t y;      // 0..4095
  uint8_t btn;    // 0/1
} Packet;
Packet data;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SW, INPUT_PULLUP);
  analogReadResolution(12);          // 0..4095, center ~2048

  WiFi.mode(WIFI_STA);
  Serial.print("Remote MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    ESP.restart();
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = 0;                  // use current WiFi channel
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    ESP.restart();
  }
  Serial.println("Remote ready. Broadcasting...");
}

void loop() {
  data.x   = analogRead(PIN_X);
  data.y   = analogRead(PIN_Y);
  data.btn = (digitalRead(PIN_SW) == LOW) ? 1 : 0;

  esp_now_send(broadcastAddress, (uint8_t *)&data, sizeof(data));
  delay(20);                         // ~50 packets/sec
}

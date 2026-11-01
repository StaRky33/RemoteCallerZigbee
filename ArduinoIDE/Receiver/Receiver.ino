#ifndef ZIGBEE_MODE_ZCZR
#error "Outils > Zigbee Mode > Zigbee ZCZR (coordinator/router)"
#endif

#include "Zigbee.h"
#include <Arduino.h>

#define LIGHT_EP_A 10   // emitter 1
#define LIGHT_EP_B 11   // emitter 2
#define SWITCH_EP  5
#define BUTTON_PIN BOOT_PIN
#define LED_BRIGHTNESS 40   // 0-255, global brightness (try 40-60 if too bright)
#define LED_PIN    RGB_BUILTIN

struct Color {
  uint8_t r, g, b;
};

constexpr Color OFF    = {0,   0,   0};
constexpr Color RED    = {255, 0,   0};
constexpr Color GREEN  = {0,   255, 0};
constexpr Color BLUE   = {0,   0,   255};
constexpr Color YELLOW = {255, 160, 0};

volatile bool stateA = false, stateB = false;
volatile bool sendA = false, sendB = false;

ZigbeeLight  zbLightA(LIGHT_EP_A);
ZigbeeLight  zbLightB(LIGHT_EP_B);
ZigbeeSwitch zbSwitch(SWITCH_EP);

inline void setLed(Color c) {
  rgbLedWrite(LED_PIN,
              (uint16_t)c.r * LED_BRIGHTNESS / 255,
              (uint16_t)c.g * LED_BRIGHTNESS / 255,
              (uint16_t)c.b * LED_BRIGHTNESS / 255);
}

void updateLed() {
  if(stateA && stateB)
  {
    setLed(BLUE);
  }
  else if(stateA || stateB)
  {
  setLed(stateA ? GREEN : YELLOW);
  }
  else
  {
    setLed(OFF);
  }
}

void onLightA(bool s) {
  if (s != stateA) { stateA = s; sendA = true; }
  updateLed();
}

void onLightB(bool s) {
  if (s != stateB) { stateB = s; sendB = true; }
  updateLed();
}

// Envoi direct d'une commande On/Off, sans binding
// cmdId : ESP_ZB_ZCL_CMD_ON_OFF_ON_ID, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID ou ESP_ZB_ZCL_CMD_ON_OFF_TOGGLE_ID
void sendOnOff(uint16_t shortAddr, uint8_t dstEp, uint8_t cmdId) {
  esp_zb_zcl_on_off_cmd_t cmd = {};
  cmd.zcl_basic_cmd.src_endpoint = SWITCH_EP;  // endpoint client local
  cmd.zcl_basic_cmd.dst_endpoint = dstEp;
  cmd.zcl_basic_cmd.dst_addr_u.addr_short = shortAddr;
  cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
  cmd.on_off_cmd_id = cmdId;
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_on_off_cmd_req(&cmd);
  esp_zb_lock_release();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  zbLightA.onLightChange(onLightA);
  zbLightB.onLightChange(onLightB);
  zbLightA.setManufacturerAndModel("Espressif", "ZigbeeHubA");
  zbLightB.setManufacturerAndModel("Espressif", "ZigbeeHubB");
  zbSwitch.setManufacturerAndModel("Espressif", "ZigbeeHubSwitch");

  Zigbee.addEndpoint(&zbLightA);
  Zigbee.addEndpoint(&zbLightB);
  Zigbee.addEndpoint(&zbSwitch);

  setLed(OFF);
  Serial.println("\nInit");

  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {  
    while (true) {
      Serial.println("Zigbee.begin() FAILED");
      delay(1000);
    }
  }
  Zigbee.openNetwork(180);  // fenêtre d'appairage de 180 s
  Serial.println("Starting Coordinator");
}

void loop() {
  static bool lastBtn = HIGH;
  static uint32_t pressedAt = 0;
  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW) {
    pressedAt = millis();
  } else if (lastBtn == LOW && btn == HIGH) {
    uint32_t d = millis() - pressedAt;
    if (d > 50 && d < 3000) {
      // Everything off: receiver + both emitters
      stateA = false; stateB = false;
      zbLightA.setLight(false);
      zbLightB.setLight(false);
      updateLed();
      sendOnOff(0xFFFD, LIGHT_EP_A, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
      sendOnOff(0xFFFD, LIGHT_EP_B, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
      Serial.println("OFF");
    } else if (d >= 3000) {
      Zigbee.factoryReset();
    }
  }
  lastBtn = btn;

  // An emitter asked for ON: confirm only to that emitter (endpoint = identity)
  if (sendA) {
    sendA = false;
    Serial.println("E1 -> ON");
    sendOnOff(0xFFFD, LIGHT_EP_A, stateA ? ESP_ZB_ZCL_CMD_ON_OFF_ON_ID : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
  }
  if (sendB) {
    sendB = false;
    Serial.println("E2 -> ON");
    sendOnOff(0xFFFD, LIGHT_EP_B, stateB ? ESP_ZB_ZCL_CMD_ON_OFF_ON_ID : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
  }

  delay(10);
}
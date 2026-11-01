#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include <Arduino.h>

#define EMITTER_ID 2 // 1 for the first emitter, 2 for the second
#define LED_BRIGHTNESS 40   // 0-255, global brightness (try 40-60 if too bright)
#define LED_PIN    RGB_BUILTIN
#define LIGHT_EP   (9 + EMITTER_ID)  // 10 or 11
#define SWITCH_EP  5
#define BUTTON_PIN BOOT_PIN

struct Color {
  uint8_t r, g, b;
};

constexpr Color OFF    = {0,   0,   0};
constexpr Color RED    = {255, 0,   0};
constexpr Color GREEN  = {0,   255, 0};
constexpr Color BLUE   = {0,   0,   255};
constexpr Color YELLOW = {255, 160, 0};

constexpr uint32_t BLINK_ON_MS  = 200;
constexpr uint32_t BLINK_OFF_MS = 3000;

volatile bool ledOn = false;

ZigbeeSwitch zbSwitch(SWITCH_EP);  // client : envoie vers la light
ZigbeeLight  zbLight(LIGHT_EP);    // serveur : reflète l'état de la light


/********************* RGB LED functions **************************/
void restoreLed() {
  if (ledOn) setLed(EMITTER_ID == 1 ? GREEN : YELLOW);
  else       setLed(OFF);
}

void onLightChange(bool state) {
  ledOn = state;
  restoreLed();
}

inline void setLed(Color c) {
  rgbLedWrite(LED_PIN,
              (uint16_t)c.r * LED_BRIGHTNESS / 255,
              (uint16_t)c.g * LED_BRIGHTNESS / 255,
              (uint16_t)c.b * LED_BRIGHTNESS / 255);
}


// 200 ms red pulse, then 3 s pause, repeated while noLink is true
void handleNoLinkBlink(bool noLink) {
  static bool redOn = false;
  static uint32_t t = 0;
  uint32_t now = millis();

  if (!noLink) {
    if (redOn) { redOn = false; restoreLed(); }
    t = now;
    return;
  }
  if (!redOn && now - t >= BLINK_OFF_MS) {
    redOn = true; t = now; setLed(RED);
  } else if (redOn && now - t >= BLINK_ON_MS) {
    redOn = false; t = now; restoreLed();
  }
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

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  zbLight.onLightChange(onLightChange);
  zbLight.setManufacturerAndModel("Espressif", "ZigbeeSwitchNode");
  zbSwitch.setManufacturerAndModel("Espressif", "ZigbeeSwitchNodeBtn");
  zbSwitch.allowMultipleBinding(true);

  Zigbee.addEndpoint(&zbSwitch);
  Zigbee.addEndpoint(&zbLight);
  
  setLed(OFF);
  Serial.println("\nInit");

  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  }
}

void loop() {
  static bool joinedNow = false;
  static uint32_t lastJoinCheck = 0;
  if (millis() - lastJoinCheck >= 500) {
    lastJoinCheck = millis();
    esp_zb_lock_acquire(portMAX_DELAY);
    joinedNow = esp_zb_bdb_dev_joined();
    esp_zb_lock_release();
  }

  handleNoLinkBlink(!joinedNow);
  static bool lastBtn = HIGH;
  static uint32_t pressedAt = 0;
  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW) {
    pressedAt = millis();
  } else if (lastBtn == LOW && btn == HIGH) {
    uint32_t d = millis() - pressedAt;
    if (d > 50 && d < 3000) {
      if (Zigbee.connected()) {
        sendOnOff(0x0000, LIGHT_EP, ESP_ZB_ZCL_CMD_ON_OFF_ON_ID);
        Serial.println("Button Pressed!");
      } else {
        Serial.println("Button pressed, not connected");
        setLed(RED); delay(200); restoreLed();   // immediate feedback (B)
      }
    }
  }
  lastBtn = btn;
  delay(10);
}
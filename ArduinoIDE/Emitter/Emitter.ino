#include <Arduino.h>
#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"

/* Zigbee light bulb configuration */
#define LIGHT_EP   10
#define SWITCH_EP  5
#define BUTTON_PIN BOOT_PIN
#define LED_PIN    RGB_BUILTIN
uint8_t led = RGB_BUILTIN;
uint8_t button = BOOT_PIN;

ZigbeeSwitch zbSwitch(SWITCH_EP);  // client : envoie vers la light
ZigbeeLight  zbLight(LIGHT_EP);    // serveur : reflète l'état de la light

/********************* RGB LED functions **************************/
void onLightChange(bool state) {
  //rgbLedWrite(LED_PIN, state ? 255 : 0, state ? 255 : 0, state ? 255 : 0); // White
  //rgbLedWrite(LED_PIN, state ? 255 : 0, state ? 0 : 0, state ? 0 : 0); // Red
  //rgbLedWrite(LED_PIN, state ? 0 : 0, state ? 255 : 0, state ? 0 : 0); // Green
  rgbLedWrite(LED_PIN, state ? 255 : 0, state ? 255 : 0, state ? 0 : 0); // Yellow
  //rgbLedWrite(LED_PIN, state ? 0 : 0, state ? 0 : 0, state ? 255 : 0); // Blue
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
  
  Serial.println("\nConnecte");

  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  }
  Serial.println("Connecting to network");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();
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
      sendOnOff(0x0000, LIGHT_EP, ESP_ZB_ZCL_CMD_ON_OFF_TOGGLE_ID);
      /*if (zbSwitch.bound()) {
        zbSwitch.lightToggle();  // la light décide, puis renvoie l'état à tous
      } else {
        Serial.println("Pas encore lie a la light");
      }*/
    } else if (d >= 3000) {
      Zigbee.factoryReset();
    }
  }
  lastBtn = btn;
  delay(10);
}
#ifndef ZIGBEE_MODE_ZCZR
#error "Outils > Zigbee Mode > Zigbee ZCZR (coordinator/router)"
#endif

#include "Zigbee.h"

#define LIGHT_EP   10
#define SWITCH_EP  5
#define BUTTON_PIN BOOT_PIN
#define LED_PIN    RGB_BUILTIN

ZigbeeLight  zbLight(LIGHT_EP);    // serveur On/Off : reçoit
ZigbeeSwitch zbSwitch(SWITCH_EP);  // client On/Off : envoie

volatile bool stateChanged = false;
volatile bool currentState = false;

// Appelé par la pile Zigbee : on ne fait que mémoriser, l'envoi se fait dans loop()
void onLightChange(bool state) {
  //rgbLedWrite(LED_PIN, state ? 255 : 0, state ? 255 : 0, state ? 255 : 0); // White
  //rgbLedWrite(LED_PIN, state ? 255 : 0, state ? 0 : 0, state ? 0 : 0); // Red
  //rgbLedWrite(LED_PIN, state ? 0 : 0, state ? 255 : 0, state ? 0 : 0); // Green
  //rgbLedWrite(LED_PIN, state ? 255 : 0, state ? 255 : 0, state ? 0 : 0); // Yellow
  rgbLedWrite(LED_PIN, state ? 0 : 0, state ? 0 : 0, state ? 255 : 0); // Blue
  if (state != currentState) {
    currentState = state;
    stateChanged = true;
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

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  zbLight.onLightChange(onLightChange);
  zbLight.setManufacturerAndModel("Espressif", "ZigbeeLightHub");
  zbSwitch.setManufacturerAndModel("Espressif", "ZigbeeLightHubSwitch");
  zbSwitch.allowMultipleBinding(true);

  Zigbee.addEndpoint(&zbLight);
  Zigbee.addEndpoint(&zbSwitch);

  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {  
    while (true) {
      Serial.println("Zigbee.begin() FAILED");
      delay(1000);
    }
  }
  Zigbee.openNetwork(180);  // fenêtre d'appairage de 180 s
  Serial.println("Coordinateur demarre");
}

void loop() {
  // Bouton local (BOOT) : maintenu 3 s = reset usine
  static bool lastBtn = HIGH;
  static uint32_t pressedAt = 0;
  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW) {
    pressedAt = millis();
  } else if (lastBtn == LOW && btn == HIGH) {
    uint32_t d = millis() - pressedAt;
    if (d > 50 && d < 3000) {
      zbLight.setLight(!zbLight.getLightState());  // déclenche onLightChange
    } else if (d >= 3000) {
      Zigbee.factoryReset();
    }
  }
  lastBtn = btn;

  // Répercute l'état de la light vers les switches (broadcast)
  if (stateChanged) {
    stateChanged = false;
    Serial.printf("Envoi broadcast: %s\n", currentState ? "ON" : "OFF");
    sendOnOff(0xFFFD, LIGHT_EP,
              currentState ? ESP_ZB_ZCL_CMD_ON_OFF_ON_ID : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
  }

  delay(10);
}
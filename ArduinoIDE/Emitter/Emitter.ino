#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_partition.h>

#define EMITTER_ID 1        // 1 for the first emitter, 2 for the second
#define SWITCH_EP  5
#define LIGHT_EP   (9 + EMITTER_ID)  // 10 or 11
#define LED_PIN    RGB_BUILTIN
#define BUTTON_PIN BOOT_PIN
#define BTN_UP_PIN   19
#define BTN_DOWN_PIN 20

struct Color {
  uint8_t r, g, b;
};

constexpr Color OFF    = {0,   0,   0};
constexpr Color RED    = {255, 0,   0};
constexpr Color GREEN  = {0,   255, 0};
constexpr Color BLUE   = {0,   0,   255};
constexpr Color YELLOW = {255, 160, 0};
constexpr uint8_t BRIGHT_MIN  = 55;
constexpr uint8_t BRIGHT_MAX  = 255;
constexpr uint8_t BRIGHT_STEP = 20;
constexpr uint32_t BLINK_ON_MS    = 200;
constexpr uint32_t BLINK_OFF_MS   = 3000;
constexpr uint32_t WIPE_HOLD_MS   = 3000;
constexpr uint8_t  MAX_BOOT_FAILS = 3;

volatile uint8_t brightness = BRIGHT_MIN;
volatile bool previewActive = false;
volatile bool ledOn = false;
volatile bool joinedFlag = false;   // written by loop(), read by uiTask()

ZigbeeSwitch zbSwitch(SWITCH_EP);  // client: sends to the receiver
ZigbeeLight  zbLight(LIGHT_EP);    // server: reflects the receiver's state

/********************* RGB LED functions **************************/
inline void setLed(Color c) {
  uint8_t b = brightness;
  rgbLedWrite(LED_PIN,
              (uint16_t)c.r * b / 255,
              (uint16_t)c.g * b / 255,
              (uint16_t)c.b * b / 255);
}

void restoreLed() {
  if (previewActive) return;
  if (ledOn) setLed(EMITTER_ID == 1 ? GREEN : YELLOW);
  else       setLed(OFF);
}

void onLightChange(bool state) {
  ledOn = state;
  restoreLed();
}

// 200 ms red pulse, then 3 s pause, repeated while noLink is true
void handleNoLinkBlink(bool noLink) {
  if (previewActive) return;
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

void changeBrightness(int delta) {
  int b = (int)brightness + delta;
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > BRIGHT_MAX) b = BRIGHT_MAX;
  brightness = b;
  previewActive = true;
  setLed(EMITTER_ID == 1 ? GREEN : YELLOW);               // preview at the new brightness
}

// Call every ~10 ms
void handleBrightnessButtons() {
  static bool lastDown = HIGH, lastUp = HIGH;
  static uint32_t lastEdge = 0;
  bool down = digitalRead(BTN_DOWN_PIN);
  bool up   = digitalRead(BTN_UP_PIN);
  uint32_t now = millis();

  if (down == lastDown && up == lastUp) return;
  if (now - lastEdge < 30) return;      // debounce, the edge is seen again next call
  lastEdge = now;

  if (lastDown == HIGH && down == LOW) changeBrightness(-(int)BRIGHT_STEP);
  if (lastUp   == HIGH && up   == LOW) changeBrightness(+(int)BRIGHT_STEP);

  lastDown = down; lastUp = up;

  if (down == HIGH && up == HIGH && previewActive) {   // both released
    previewActive = false;
    restoreLed();
  }
}

/********************* Zigbee storage wipe ************************/
void wipeZigbeeStorage() {
  const esp_partition_t* p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "zb_storage");
  if (p) esp_partition_erase_range(p, 0, p->size);
  else Serial.println("zb_storage partition not found");
}

// Runs independently of Zigbee.begin(): blink + long press = wipe
void uiTask(void*) {
  bool lastBtn = HIGH;
  uint32_t pressedAt = 0;
  for (;;) {
    bool btn = digitalRead(BUTTON_PIN);
    if (lastBtn == HIGH && btn == LOW) pressedAt = millis();
    if (btn == LOW && millis() - pressedAt >= WIPE_HOLD_MS) {
      setLed(RED);
      wipeZigbeeStorage();
      delay(300);
      ESP.restart();
    }
    lastBtn = btn;
    handleNoLinkBlink(!joinedFlag);
    handleBrightnessButtons();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/********************* Zigbee send ********************************/
// Direct On/Off command, no binding needed
void sendOnOff(uint16_t shortAddr, uint8_t dstEp, uint8_t cmdId) {
  esp_zb_zcl_on_off_cmd_t cmd = {};
  cmd.zcl_basic_cmd.src_endpoint = SWITCH_EP;
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
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  setLed(OFF);
  Serial.println("\nInit");

  // Started before Zigbee.begin() so blink and long press work while it blocks
  xTaskCreate(uiTask, "ui", 4096, nullptr, 1, nullptr);

  zbLight.onLightChange(onLightChange);
  zbLight.setManufacturerAndModel("Espressif", "ZigbeeSwitchNode");
  zbSwitch.setManufacturerAndModel("Espressif", "ZigbeeSwitchNodeBtn");
  zbSwitch.allowMultipleBinding(true);

  Zigbee.addEndpoint(&zbSwitch);
  Zigbee.addEndpoint(&zbLight);

  Preferences prefs;
  prefs.begin("zbrec", false);

  if (!Zigbee.begin()) {
    uint8_t fails = prefs.getUChar("fails", 0) + 1;
    Serial.printf("Zigbee start failed (%u in a row)\n", fails);
    if (fails >= MAX_BOOT_FAILS) {   // too many failed boots: wipe and retry clean
      prefs.putUChar("fails", 0);
      wipeZigbeeStorage();
    } else {
      prefs.putUChar("fails", fails);
    }
    prefs.end();
    ESP.restart();
  }
  prefs.putUChar("fails", 0);
  prefs.end();
}

void loop() {
  static bool joinedNow = false;
  static uint32_t lastJoinCheck = 0;
  if (millis() - lastJoinCheck >= 500) {
    lastJoinCheck = millis();
    esp_zb_lock_acquire(portMAX_DELAY);
    joinedNow = esp_zb_bdb_dev_joined();
    esp_zb_lock_release();
    joinedFlag = joinedNow;
  }

  static bool lastBtn = HIGH;
  static uint32_t pressedAt = 0;
  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW) {
    pressedAt = millis();
  } else if (lastBtn == LOW && btn == HIGH) {
    uint32_t d = millis() - pressedAt;
    if (d > 50 && d < WIPE_HOLD_MS) {   // long press is handled by uiTask
      if (joinedNow) {
        sendOnOff(0x0000, LIGHT_EP, ESP_ZB_ZCL_CMD_ON_OFF_ON_ID);
        Serial.println("Button Pressed!");
      } else {
        Serial.println("Button pressed, not connected");
      }
    }
  }
  lastBtn = btn;
  delay(10);
}
// DEVICE_INDEX: 1 = emitter 1, 2 = emitter 2, 3 = receiver
// Tools > Zigbee Mode: ED for 1 and 2, ZCZR (coordinator/router) for 3.
#ifndef DEVICE_INDEX
#define DEVICE_INDEX 1
#endif

#define IS_RECEIVER (DEVICE_INDEX == 3)

#if DEVICE_INDEX < 1 || DEVICE_INDEX > 3
#error "DEVICE_INDEX must be 1, 2 or 3"
#endif

#if IS_RECEIVER
  #ifndef ZIGBEE_MODE_ZCZR
  #error "Receiver needs Tools > Zigbee Mode > Zigbee ZCZR (coordinator/router)"
  #endif
#else
  #ifndef ZIGBEE_MODE_ED
  #error "Emitters need Tools > Zigbee Mode > Zigbee ED (end device)"
  #endif
#endif

#include "Zigbee.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_partition.h>

// Pins: avoid strapping pins (4, 5, 8, 9, 15). BOOT_PIN is GPIO 9.
#define LED_PIN      RGB_BUILTIN
#define BUTTON_PIN   BOOT_PIN
#define BTN_UP_PIN   19
#define BTN_DOWN_PIN 20
#define BUZZER_PIN   18   // receiver only, passive piezo

// Endpoints are the emitter identity: emitter 1 -> 10, emitter 2 -> 11
#define SWITCH_EP  5
#define LIGHT_EP_A 10
#define LIGHT_EP_B 11
#define LIGHT_EP   (9 + DEVICE_INDEX)   // emitters only

struct Color {
  uint8_t r, g, b;
};

constexpr Color OFF    = {0,   0,   0};
constexpr Color RED    = {255, 0,   0};
constexpr Color GREEN  = {0,   255, 0};
constexpr Color BLUE   = {0,   0,   255};
constexpr Color YELLOW = {255, 160, 0};   // 255,255,0 looks greenish on WS2812

constexpr Color EMITTER_COLOR = (DEVICE_INDEX == 1) ? BLUE : YELLOW;
constexpr Color PREVIEW_COLOR = IS_RECEIVER ? GREEN : EMITTER_COLOR;

constexpr uint8_t  BRIGHT_MIN  = 15;
constexpr uint8_t  BRIGHT_MAX  = 255;
constexpr uint8_t  BRIGHT_STEP = 48;
constexpr uint8_t  BRIGHT_DEFAULT = 255;   
constexpr uint32_t BLINK_ON_MS  = 200;
constexpr uint32_t BLINK_OFF_MS = 3000;
constexpr uint32_t LONG_PRESS_MS = 3000;

// Buzzer: duty out of 255. 50% (128) is the loudest, lower is quieter.
constexpr uint32_t BUZZER_FREQ  = 2700;   // Hz, near a typical piezo resonance
constexpr uint8_t  VOL_LOW      = 6;
constexpr uint8_t  VOL_MID      = 60;
constexpr uint8_t  VOL_HIGH     = 140;
constexpr uint32_t BEEP_MS      = 150;
constexpr uint32_t BEEP_GAP_MS  = 150;
constexpr uint32_t ROUND_GAP_MS = 10000;

constexpr uint8_t MAX_BOOT_FAILS = 3;

volatile uint8_t brightness = BRIGHT_DEFAULT;
volatile bool previewActive = false;

ZigbeeSwitch zbSwitch(SWITCH_EP);

#if IS_RECEIVER
struct Round { uint8_t beeps; uint8_t duty; };
constexpr Round ALARM[] = {
  {2, VOL_LOW},  {2, VOL_LOW},  {2, VOL_LOW},
  {2, VOL_MID},  {2, VOL_MID},  {2, VOL_MID},
  {2, VOL_HIGH}, {2, VOL_HIGH}, {2, VOL_HIGH},
};
constexpr uint8_t ALARM_ROUNDS = sizeof(ALARM) / sizeof(ALARM[0]);

volatile bool stateA = false, stateB = false;
volatile bool sendA = false, sendB = false;
volatile bool alarmRequest = false;
bool alarmActive = false, beepOn = false;
uint8_t roundIdx = 0, beepIdx = 0;
uint32_t buzzT = 0;

ZigbeeLight zbLightA(LIGHT_EP_A);
ZigbeeLight zbLightB(LIGHT_EP_B);
#else
volatile bool ledOn = false;
volatile bool joinedFlag = false;   // written by loop(), read by uiTask()

ZigbeeLight zbLight(LIGHT_EP);
#endif

// Separate namespace from "zbrec": wiping zb_storage does not touch it.
void loadBrightness() {
  Preferences p;
  p.begin("settings", true);
  int b = p.getUChar("bright", BRIGHT_DEFAULT);
  p.end();
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > BRIGHT_MAX) b = BRIGHT_MAX;
  brightness = b;
}

// Called on button release only, to limit flash wear.
void saveBrightness() {
  static uint8_t saved = 0;
  uint8_t b = brightness;
  if (b == saved) return;
  Preferences p;
  p.begin("settings", false);
  p.putUChar("bright", b);
  p.end();
  saved = b;
}

/********************* LED **************************/
inline void setLed(Color c) {
  uint8_t b = brightness;
  rgbLedWrite(LED_PIN,
              (uint16_t)c.r * b / 255,
              (uint16_t)c.g * b / 255,
              (uint16_t)c.b * b / 255);
}

void restoreLed() {
  if (previewActive) return;
#if IS_RECEIVER
  if (stateA && stateB)    setLed(BLUE);
  else if (stateA || stateB) setLed(stateA ? GREEN : YELLOW);
  else                     setLed(OFF);
#else
  setLed(ledOn ? EMITTER_COLOR : OFF);
#endif
}

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
  setLed(PREVIEW_COLOR);
}

void handleBrightnessButtons() {
  static bool lastDown = HIGH, lastUp = HIGH;
  static uint32_t lastEdge = 0;
  bool down = digitalRead(BTN_DOWN_PIN);
  bool up   = digitalRead(BTN_UP_PIN);
  uint32_t now = millis();

  if (down == lastDown && up == lastUp) return;
  if (now - lastEdge < 30) return;
  lastEdge = now;

  if (lastDown == HIGH && down == LOW) changeBrightness(-(int)BRIGHT_STEP);
  if (lastUp   == HIGH && up   == LOW) changeBrightness(+(int)BRIGHT_STEP);

  lastDown = down; lastUp = up;

    if (down == HIGH && up == HIGH && previewActive) {
    previewActive = false;
    saveBrightness();
    restoreLed();
  }
}

/********************* Zigbee send **************************/
// Direct send by short address: ZigbeeSwitch refuses to send when not bound.
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

#if IS_RECEIVER
/********************* Receiver **************************/
void onLightA(bool s) {
  if (s != stateA) { stateA = s; sendA = true; if (s) alarmRequest = true; }
  restoreLed();
}

void onLightB(bool s) {
  if (s != stateB) { stateB = s; sendB = true; if (s) alarmRequest = true; }
  restoreLed();
}

uint8_t countPairedDevices() {
  uint8_t n = 0;
  esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
  esp_zb_nwk_neighbor_info_t nbr;
  esp_zb_lock_acquire(portMAX_DELAY);
  while (esp_zb_nwk_get_next_neighbor(&it, &nbr) == ESP_OK) n++;
  esp_zb_lock_release();
  return n;
}

void buzzerStart() {
  alarmActive = true;
  roundIdx = 0; beepIdx = 0; beepOn = false;
  buzzT = millis() - ROUND_GAP_MS;   // first beep starts immediately
}

void buzzerStop() {
  alarmActive = false;
  beepOn = false;
  ledcWrite(BUZZER_PIN, 0);
}

void handleBuzzer() {
  if (!alarmActive) return;
  uint32_t now = millis();
  const Round& r = ALARM[roundIdx];

  if (beepOn) {
    if (now - buzzT >= BEEP_MS) {
      ledcWrite(BUZZER_PIN, 0);
      beepOn = false; buzzT = now; beepIdx++;
      if (beepIdx >= r.beeps && roundIdx == ALARM_ROUNDS - 1) alarmActive = false;
    }
    return;
  }

  uint32_t wait = (beepIdx == 0) ? ROUND_GAP_MS : BEEP_GAP_MS;
  if (beepIdx >= r.beeps) {
    if (now - buzzT >= ROUND_GAP_MS) { roundIdx++; beepIdx = 0; buzzT = now - ROUND_GAP_MS; }
    return;
  }
  if (now - buzzT >= wait) {
    ledcWrite(BUZZER_PIN, r.duty);
    beepOn = true; buzzT = now;
  }
}

void setup() {
  Serial.begin(115200);
  loadBrightness();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  setLed(OFF);
  Serial.println("\nInit");

  ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);
  ledcWrite(BUZZER_PIN, 0);

  zbLightA.onLightChange(onLightA);
  zbLightB.onLightChange(onLightB);
  zbLightA.setManufacturerAndModel("Espressif", "ZigbeeHubA");
  zbLightB.setManufacturerAndModel("Espressif", "ZigbeeHubB");
  zbSwitch.setManufacturerAndModel("Espressif", "ZigbeeHubSwitch");

  Zigbee.addEndpoint(&zbLightA);
  Zigbee.addEndpoint(&zbLightB);
  Zigbee.addEndpoint(&zbSwitch);

  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {
    while (true) {
      Serial.println("Zigbee.begin() FAILED");
      delay(1000);
    }
  }
  Zigbee.openNetwork(180);
  Serial.println("Starting Coordinator");
}

void loop() {
  static uint8_t paired = 0;
  static uint32_t lastCount = 0;
  if (millis() - lastCount >= 1000) {
    lastCount = millis();
    uint8_t n = countPairedDevices();
    if (n != paired) { paired = n; Serial.printf("Paired devices: %u\n", n); }
  }
  handleNoLinkBlink(paired < 1);

  static bool lastBtn = HIGH;
  static uint32_t pressedAt = 0;
  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW) {
    pressedAt = millis();
  } else if (lastBtn == LOW && btn == HIGH) {
    uint32_t d = millis() - pressedAt;
    if (d > 50 && d < LONG_PRESS_MS) {
      stateA = false; stateB = false;
      zbLightA.setLight(false);
      zbLightB.setLight(false);
      restoreLed();
      sendOnOff(0xFFFD, LIGHT_EP_A, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
      sendOnOff(0xFFFD, LIGHT_EP_B, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
      buzzerStop();
      Serial.println("OFF");
    } else if (d >= LONG_PRESS_MS) {
      Zigbee.factoryReset();
    }
  }
  lastBtn = btn;

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

  if (alarmRequest) {
    alarmRequest = false;
    if (!alarmActive) buzzerStart();
  }
  handleBuzzer();
  handleBrightnessButtons();

  delay(10);
}

#else
/********************* Emitter **************************/
void onLightChange(bool state) {
  ledOn = state;
  restoreLed();
}

// Erasing zb_storage fixes pairing that a soft reboot cannot.
void wipeZigbeeStorage() {
  const esp_partition_t* p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "zb_storage");
  if (p) esp_partition_erase_range(p, 0, p->size);
  else Serial.println("zb_storage partition not found");
}

// Started before Zigbee.begin() so blink, brightness and wipe work while it blocks.
void uiTask(void*) {
  bool lastBtn = HIGH;
  uint32_t pressedAt = 0;
  for (;;) {
    bool btn = digitalRead(BUTTON_PIN);
    if (lastBtn == HIGH && btn == LOW) pressedAt = millis();
    if (btn == LOW && millis() - pressedAt >= LONG_PRESS_MS) {
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

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  setLed(OFF);
  Serial.println("\nInit");

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
    if (fails >= MAX_BOOT_FAILS) {
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
    joinedNow = esp_zb_bdb_dev_joined();   // Zigbee.connected() never goes back to false
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
    if (d > 50 && d < LONG_PRESS_MS) {   // long press is handled by uiTask
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
#endif
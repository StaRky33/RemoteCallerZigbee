#ifndef ZIGBEE_MODE_ZCZR
#error "Outils > Zigbee Mode > Zigbee ZCZR (coordinator/router)"
#endif

#include "Zigbee.h"
#include <Arduino.h>

#define LIGHT_EP_A 10   // emitter 1
#define LIGHT_EP_B 11   // emitter 2
#define SWITCH_EP  5
#define BUTTON_PIN BOOT_PIN
#define LED_PIN    RGB_BUILTIN
#define BUZZER_PIN 18
#define BTN_UP_PIN   19
#define BTN_DOWN_PIN 20

struct Round { uint8_t beeps; uint8_t duty; };
struct Color {
  uint8_t r, g, b;
};

constexpr uint32_t BUZZER_FREQ  = 2700;   // Hz, near a typical piezo resonance
constexpr uint8_t  VOL_LOW      = 6;      // duty out of 255
constexpr uint8_t  VOL_MID      = 60;
constexpr uint8_t  VOL_HIGH     = 140;
constexpr uint32_t BEEP_MS      = 150;
constexpr uint32_t BEEP_GAP_MS  = 150;    // pause between beeps of one round
constexpr uint32_t ROUND_GAP_MS = 10000;  // pause between rounds
constexpr uint32_t BLINK_ON_MS  = 200;
constexpr uint32_t BLINK_OFF_MS = 3000;
constexpr Color OFF    = {0,   0,   0};
constexpr Color RED    = {255, 0,   0};
constexpr Color GREEN  = {0,   255, 0};
constexpr Color BLUE   = {0,   0,   255};
constexpr Color YELLOW = {255, 160, 0};
constexpr uint8_t BRIGHT_MIN  = 55;
constexpr uint8_t BRIGHT_MAX  = 255;
constexpr uint8_t BRIGHT_STEP = 20;

constexpr Round ALARM[] = {
  {2, VOL_LOW},  {2, VOL_LOW},  {2, VOL_LOW},
  {2, VOL_MID},  {2, VOL_MID},  {2, VOL_MID},
  {2, VOL_HIGH}, {2, VOL_HIGH}, {2, VOL_HIGH},
};

constexpr uint8_t ALARM_ROUNDS = sizeof(ALARM) / sizeof(ALARM[0]);

volatile bool ledOn = false;
volatile bool stateA = false, stateB = false;
volatile bool sendA = false, sendB = false;
volatile bool alarmRequest = false;   // set from the Zigbee callbacks
volatile uint8_t brightness = BRIGHT_MIN;
volatile bool previewActive = false;

bool alarmActive = false;
bool beepOn = false;
uint8_t roundIdx = 0, beepIdx = 0;
uint32_t buzzT = 0;

ZigbeeLight  zbLightA(LIGHT_EP_A);
ZigbeeLight  zbLightB(LIGHT_EP_B);
ZigbeeSwitch zbSwitch(SWITCH_EP);

uint8_t countPairedDevices() {
  uint8_t n = 0;
  esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
  esp_zb_nwk_neighbor_info_t nbr;
  esp_zb_lock_acquire(portMAX_DELAY);
  while (esp_zb_nwk_get_next_neighbor(&it, &nbr) == ESP_OK) n++;
  esp_zb_lock_release();
  return n;
}

/********************* RGB LED functions **************************/
inline void setLed(Color c) {
  uint8_t b = brightness;
  rgbLedWrite(LED_PIN,
              (uint16_t)c.r * b / 255,
              (uint16_t)c.g * b / 255,
              (uint16_t)c.b * b / 255);
}

void updateLed() {
  if (previewActive) return;
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
// 200 ms red pulse, then 3 s pause, repeated while noLink is true
void handleNoLinkBlink(bool noLink) {
  if (previewActive) return;
  static bool redOn = false;
  static uint32_t t = 0;
  uint32_t now = millis();

  if (!noLink) {
    if (redOn) { redOn = false; updateLed(); }
    t = now;
    return;
  }
  if (!redOn && now - t >= BLINK_OFF_MS) {
    redOn = true; t = now; setLed(RED);
  } else if (redOn && now - t >= BLINK_ON_MS) {
    redOn = false; t = now; updateLed();
  }
}

void onLightA(bool s) {
  if (s != stateA) { stateA = s; sendA = true; if (s) alarmRequest = true;}
  updateLed();
}

void onLightB(bool s) {
  if (s != stateB) { stateB = s; sendB = true; if (s) alarmRequest = true;}
  updateLed();
}

void changeBrightness(int delta) {
  int b = (int)brightness + delta;
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > BRIGHT_MAX) b = BRIGHT_MAX;
  brightness = b;
  previewActive = true;
  setLed(BLUE);               // preview at the new brightness
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
    updateLed();
  }
}

/***************** BUZZER FUNCTIONS ****************/
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

// Non-blocking: call it every loop()
void handleBuzzer() {
  if (!alarmActive) return;
  uint32_t now = millis();
  const Round& r = ALARM[roundIdx];

  if (beepOn) {
    if (now - buzzT >= BEEP_MS) {
      ledcWrite(BUZZER_PIN, 0);
      beepOn = false; buzzT = now; beepIdx++;
      if (beepIdx >= r.beeps && roundIdx == ALARM_ROUNDS - 1) alarmActive = false;  // done
    }
    return;
  }

  uint32_t wait = (beepIdx == 0) ? ROUND_GAP_MS : BEEP_GAP_MS;
  if (beepIdx >= r.beeps) {            // round finished, move to the next one
    if (now - buzzT >= ROUND_GAP_MS) { roundIdx++; beepIdx = 0; buzzT = now - ROUND_GAP_MS; }
    return;
  }
  if (now - buzzT >= wait) {
    ledcWrite(BUZZER_PIN, r.duty);
    beepOn = true; buzzT = now;
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
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);

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

  ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);
  ledcWrite(BUZZER_PIN, 0);

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
  static uint8_t paired = 0;
  static uint32_t lastCount = 0;
  if (millis() - lastCount >= 1000) {          // poll once per second
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
    if (d > 50 && d < 3000) {
      // Everything off: receiver + both emitters
      stateA = false; stateB = false;
      zbLightA.setLight(false);
      zbLightB.setLight(false);
      updateLed();
      sendOnOff(0xFFFD, LIGHT_EP_A, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
      sendOnOff(0xFFFD, LIGHT_EP_B, ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID);
      buzzerStop();
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
  
  if (alarmRequest) {
    alarmRequest = false;
    if (!alarmActive) buzzerStart();
  }
  handleBuzzer();
  handleBrightnessButtons();

  delay(10);
}
// ESP 2 Receiver

#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ─── Display Pins ──────────────────────────────────────────────
#define TFT_CS   5
#define TFT_DC   22
#define TFT_RST  4
#define TFT_BL  27   // backlight control — move BL wire from 3.3V to GPIO 27
// ─── Buzzer & Button Pins ──────────────────────────────────────
#define BUZZER_PIN    15
#define SILENCE_BTN   14   // external push button

// ─── Buzzer PWM Settings ───────────────────────────────────────
#define BUZZER_FREQ     2000
#define BUZZER_RES      8
#define BUZZER_VOLUME   255    // 0–255, lower = quieter. Try 60–100 for a gentle alert

// ─── Display ───────────────────────────────────────────────────
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ─── Message struct ────────────────────────────────────────────
typedef struct {
  bool cryDetected;
} AlertMessage;

AlertMessage inMsg;
bool alertActive = false;
bool newDataFlag  = false;
bool silenced     = false;

// ─── Buzzer helpers (ESP32 Core 3.x API) ───────────────────────
void buzzerOn() {
  ledcAttach(BUZZER_PIN, BUZZER_FREQ, BUZZER_RES);
  ledcWrite(BUZZER_PIN, BUZZER_VOLUME);
}

void buzzerOff() {
  ledcDetach(BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

// ─── ESP-NOW callback ──────────────────────────────────────────
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len == sizeof(AlertMessage)) {
    memcpy(&inMsg, data, len);
    newDataFlag = true;
  }
}

// ─── Display helpers ───────────────────────────────────────────
void showStandby() {
  digitalWrite(TFT_BL, HIGH);  // make sure screen is on
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(4);
  tft.setCursor(72, 50);
  tft.print("BABY");
  tft.setCursor(36, 100);
  tft.print("MONITOR");
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(48, 170);
  tft.print("All quiet  :)");
}

void showAlert() {
  digitalWrite(TFT_BL, HIGH);  // make sure screen is on
  tft.fillScreen(ST77XX_RED);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(4);
  tft.setCursor(72, 40);
  tft.print("BABY");
  tft.setCursor(36, 90);
  tft.print("CRYING!");
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(36, 170);
  tft.print("Check on baby!");
}

// void showSilenced() {
//   tft.fillScreen(ST77XX_RED);           // screen stays red — still an active alert
//   tft.setTextColor(ST77XX_WHITE);
//   tft.setTextSize(4);
//   tft.setCursor(72, 40);
//   tft.print("BABY");
//   tft.setCursor(36, 90);
//   tft.print("CRYING!");
//   tft.setTextColor(ST77XX_YELLOW);
//   tft.setTextSize(2);
//   tft.setCursor(36, 170);
//   tft.print("Check on baby!");
//   // Small mute indicator at bottom
//   tft.setTextColor(ST77XX_WHITE);
//   tft.setTextSize(1);
//   tft.setCursor(70, 215);
//   tft.print("[ buzzer silenced ]");
// }
void showSilenced() {
  // Turn screen off completely when buzzer is silenced
  digitalWrite(TFT_BL, LOW);
}

// ─── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
pinMode(TFT_BL, OUTPUT);
digitalWrite(TFT_BL, HIGH);  // screen on by default
  pinMode(SILENCE_BTN, INPUT_PULLUP);   // button pulls GPIO 14 LOW when pressed
  buzzerOff();

  SPI.begin(18, -1, 23, 5);
  delay(100);
  tft.init(240, 240);
  delay(100);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  delay(200);
  showStandby();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Baby cry receiver started");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

// ─── Loop ──────────────────────────────────────────────────────
void loop() {

  // Check silence button (LOW = pressed because of INPUT_PULLUP)
  if (alertActive && !silenced && digitalRead(SILENCE_BTN) == LOW) {
    delay(50); // debounce
    if (digitalRead(SILENCE_BTN) == LOW) {
      silenced = true;
      buzzerOff();
      showSilenced();   // update screen to show mute indicator
      Serial.println("Buzzer silenced by user");
      delay(300);       // prevent double-trigger
    }
  }

  // Handle incoming ESP-NOW messages
  if (newDataFlag) {
    newDataFlag = false;

    if (inMsg.cryDetected && !alertActive) {
      // New cry event — always reset silence for a fresh alert
      alertActive = true;
      silenced    = false;
      showAlert();
      buzzerOn();
      Serial.println("ALERT: Baby crying!");

    } else if (!inMsg.cryDetected && alertActive) {
      // Crying stopped
      alertActive = false;
      silenced    = false;
      showStandby();
      buzzerOff();
      Serial.println("Alert cleared");
    }
  }
}
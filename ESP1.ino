// ESP 1 Sender

#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>
#include <WiFi.h>
#include <esp_now.h>
#include "arduinoFFT.h"

// ─── Pin Definitions ───────────────────────────────────────────
#define I2S_WS   25
#define I2S_SCK  26
#define I2S_SD   33
#define LED_PIN   2

// ─── ESP-NOW: Receiver MAC ─────────────────────────────────────
uint8_t receiverMAC[] = {0x94, 0x3C, 0xC6, 0x92, 0x50, 0x58};

// ─── Audio Settings ────────────────────────────────────────────
#define I2S_PORT    I2S_NUM_0
#define SAMPLE_RATE 16000
#define SAMPLES     512        // must be power of 2

// ─── Detection Thresholds ──────────────────────────────────────
float CRY_RMS_THRESHOLD   = 10000.0;  // minimum loudness 22000
float CRY_FREQ_LOW        = 400.0;    // Hz - baby cry range start
float CRY_FREQ_HIGH       = 4000.0;   // Hz - baby cry range end
float CRY_FREQ_MIN_RATIO  = 0.35;     //0.55 raised from 0.45 — stricter frequency match
                                       // increase further if false triggers persist

const int REQUIRED_HITS        = 8;  // raised from 8 — requires ~0.75s of sustained cry
                                       // raise more if short noises still trigger
const unsigned long ALERT_HOLD_MS = 5000;

// ─── FFT ───────────────────────────────────────────────────────
double vReal[SAMPLES];
double vImag[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLE_RATE);

// ─── State ─────────────────────────────────────────────────────
int hitCount = 0;
bool alertState = false;
unsigned long lastCryTime = 0;

typedef struct {
  bool cryDetected;
} AlertMessage;
AlertMessage outMsg;

void onSendCallback(const wifi_tx_info_t *info, esp_now_send_status_t status) {}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onSendCallback);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  const i2s_pin_config_t pin_config = {
    .bck_io_num  = I2S_SCK,
    .ws_io_num   = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

// Returns true if sound is loud enough AND cry frequencies dominate AND peak is in cry range
bool analyzeCry() {
  int32_t rawSamples[SAMPLES];
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, rawSamples, sizeof(rawSamples), &bytesRead, portMAX_DELAY);

  int count = bytesRead / sizeof(int32_t);
  if (count == 0) return false;

  // Fill FFT buffers + compute RMS
  double sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    double s = (i < count) ? (double)(rawSamples[i] >> 8) : 0.0;
    vReal[i] = s;
    vImag[i] = 0.0;
    sum += s * s;
  }

  double rms = sqrt(sum / SAMPLES);
  Serial.print("RMS: ");
  Serial.println(rms);

  // Step 1: Must be loud enough
  if (rms < CRY_RMS_THRESHOLD) return false;

  // Step 2: Run FFT
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  // Frequency resolution = SAMPLE_RATE / SAMPLES = 16000 / 512 = 31.25 Hz per bin
  int binLow    = (int)(CRY_FREQ_LOW  / (SAMPLE_RATE / SAMPLES));
  int binHigh   = (int)(CRY_FREQ_HIGH / (SAMPLE_RATE / SAMPLES));
  int totalBins = SAMPLES / 2;  // only first half is meaningful

  double cryEnergy   = 0;
  double totalEnergy = 0;
  double peakMag     = 0;
  int    peakBin     = 0;

  for (int i = 1; i < totalBins; i++) {
    totalEnergy += vReal[i];
    if (i >= binLow && i <= binHigh) {
      cryEnergy += vReal[i];
    }
    // Track the single loudest frequency bin across ALL bins
    if (vReal[i] > peakMag) {
      peakMag = vReal[i];
      peakBin = i;
    }
  }

  double ratio      = (totalEnergy > 0) ? (cryEnergy / totalEnergy) : 0;
  double peakFreq   = peakBin * ((double)SAMPLE_RATE / SAMPLES);
  bool peakInRange  = (peakFreq >= CRY_FREQ_LOW && peakFreq <= CRY_FREQ_HIGH);

  Serial.print("Cry freq ratio: ");
  Serial.print(ratio);
  Serial.print("  |  Peak freq: ");
  Serial.print(peakFreq);
  Serial.println(peakInRange ? " Hz [IN RANGE]" : " Hz [OUT OF RANGE]");

  // Step 3: At least 55% of energy must be in cry range
  if (ratio < CRY_FREQ_MIN_RATIO) return false;

  // Step 4: The dominant frequency must actually fall inside the cry band
  // This blocks sounds that have scattered energy in the range but peak elsewhere
  // (e.g. a TV, a door slam, a clap)
  if (!peakInRange) return false;

  return true;
}

void setAlert(bool on) {
  alertState = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  outMsg.cryDetected = on;
  esp_now_send(receiverMAC, (uint8_t *)&outMsg, sizeof(outMsg));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  setupESPNow();
  setupI2S();
  Serial.println("Baby cry sender started");
}

void loop() {
  bool isCry = analyzeCry();

  if (isCry) {
    hitCount++;
    if (hitCount >= REQUIRED_HITS) {
      lastCryTime = millis();
      if (!alertState) setAlert(true);   // only send once per event
    }
  } else {
    if (hitCount > 0) hitCount--;
  }

  if (alertState && (millis() - lastCryTime > ALERT_HOLD_MS)) {
    setAlert(false);
    hitCount = 0;
  }
}
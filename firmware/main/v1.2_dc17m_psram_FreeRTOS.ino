#include <Arduino.h>
#include <driver/ledc.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_DRV2605.h>
//#include <SD.h>
#include "driver/i2s.h"

#include "DC17m_Blaster_01.h"
#include "DC17m_Blaster_02.h"
#include "DC17m_Blaster_03.h"
#include "DC17m_Blaster_04.h"
#include "DC17m_Blaster_05.h"
#include "DC17m_Blaster_06.h"
#include "DC17m_Blaster_07.h"
#include "DC17m_Blaster_08.h"
#include "DC17m_Blaster_09.h"
#include "DC17m_Reload_01.h"
#include "DC17m_EmptyClick.h"
#include "DC17m_PowerShotAlt_01.h"
#include "DC17m_PowerShotAlt_02.h"
#include "DC17m_PowerShotAlt_03.h"
#include "DC17m_PowerShotAlt_04.h"
#include "DC17m_ModeSwitchToPower.h"
#include "DC17m_ModeSwitchToAuto.h"

// #define BATTERY_PIN 7
// const float R1 = 100000.0, R2 = 100000.0, ADC_MAX = 4095.0, VREF = 3.3;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_DRV2605 drv;  // Haptic driver instance

#define MODE_SWITCH_PIN 15   //GPIO 3 ?
#define TRIGGER_PIN     14
#define MAG_SENSOR_PIN  20
#define VOLUME_PIN      1
#define RED_LED_PIN     6
#define GREEN_LED_PIN   5
#define BLUE_LED_PIN    4
#define LED_PIN         13  //mag led

#define RED_CHANNEL     0
#define GREEN_CHANNEL   1
#define BLUE_CHANNEL    2

//#define SD_CS   10
//#define SD_MOSI 11
//#define SD_MISO 13
//#define SD_SCK  12

#define I2S_BCLK 38
#define I2S_LRCK 39
#define I2S_DOUT 37

int ammo = 0, maxAmmo = 60;
unsigned long lastShotTime = 0;
const int fireRateMs = 120;
bool sdOK = false;

const unsigned int ledDurationAutoMin = 25;
const unsigned int ledDurationAutoMax = 50;
const unsigned int fadeDurationAutoMin = 150;
const unsigned int fadeDurationAutoMax = 200;

const unsigned int ledDurationPowerMin = 500;
const unsigned int ledDurationPowerMax = 700;
const unsigned int fadeDurationPowerMin = 800;
const unsigned int fadeDurationPowerMax = 1000;

bool flickerEnabled = true;
unsigned long flickerInterval = 30;  // Flicker update every 30ms
unsigned long lastFlickerTime = 0;

unsigned long lastPowerShotTime = 0;
const unsigned long powerShotCooldown = 1500; // 1.5 seconds

// unsigned long lastBatteryRead = 0;
// float vBat = 0.0;

bool triggerPreviouslyPressed = false;
bool autoHapticActive = false;

struct SoundData {
  uint8_t* data = nullptr;
  size_t length = 0;
};

SoundData soundLibrary[17];

const i2s_port_t I2S_PORT = I2S_NUM_0;
i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = 44100,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // RIGHT_LEFT if failing 
  .communication_format = I2S_COMM_FORMAT_I2S_MSB,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 512,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .fixed_mclk = 0
};
i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_BCLK,
  .ws_io_num = I2S_LRCK,
  .data_out_num = I2S_DOUT,
  .data_in_num = I2S_PIN_NO_CHANGE
};

const int MAX_STREAMS = 4;
struct SoundStream {
  const uint8_t* data = nullptr;
  size_t length = 0;
  size_t position = 0;
  bool active = false;
  uint32_t age = 0;  // mixer calls since this stream started - used for weight decay
};
SoundStream streams[MAX_STREAMS];

unsigned long ledOnTime = 0;
unsigned int ledDuration = 30;  // default, overwritten randomly on shot
bool ledActive = false;
bool oledDirty = false;         // set true whenever ammo or mode changes, redrawn at end of loop

uint8_t redFadeStart = 0;
uint8_t greenFadeStart = 0;
uint8_t blueFadeStart = 0;
unsigned long fadeStartTime = 0;
unsigned int fadeDuration = 150;  // milliseconds

enum FireMode { FULL_AUTO, POWER_SHOT };
FireMode currentFireMode = FULL_AUTO;

float globalVolume = 1.0;         // from 0.0 to 1.0
unsigned long lastVolRead = 0;    // throttle timestamp for ADC read
float smoothVolume = 1.0;         // EMA-filtered volume, avoids ADC noise jitter

// Mutex protecting streams[] array - shared between Core 1 (startNewStream)
// and Core 0 (audio task). Using a proper mutex enables FreeRTOS priority
// inheritance, preventing priority inversion stalls.
SemaphoreHandle_t streamsMutex = NULL;

void preloadSounds() {
  Serial.println("📦 Preloading embedded sounds...");

  // Struct holding pointer to embedded data and its total length
  struct EmbeddedSound {
    const uint8_t* rawData;
    unsigned int rawLength;
  };

  EmbeddedSound embeddedSounds[17] = {
    {DC17m_Blaster_01_wav, DC17m_Blaster_01_wav_len},
    {DC17m_Blaster_02_wav, DC17m_Blaster_02_wav_len},
    {DC17m_Blaster_03_wav, DC17m_Blaster_03_wav_len},
    {DC17m_Blaster_04_wav, DC17m_Blaster_04_wav_len},
    {DC17m_Blaster_05_wav, DC17m_Blaster_05_wav_len},
    {DC17m_Blaster_06_wav, DC17m_Blaster_06_wav_len},
    {DC17m_Blaster_07_wav, DC17m_Blaster_07_wav_len},
    {DC17m_Blaster_08_wav, DC17m_Blaster_08_wav_len},
    {DC17m_Blaster_09_wav, DC17m_Blaster_09_wav_len},
    {DC17m_Reload_01_wav, DC17m_Reload_01_wav_len},
    {DC17m_EmptyClick_wav, DC17m_EmptyClick_wav_len},
    {DC17m_PowerShotAlt_01_wav, DC17m_PowerShotAlt_01_wav_len},
    {DC17m_PowerShotAlt_02_wav, DC17m_PowerShotAlt_02_wav_len},
    {DC17m_PowerShotAlt_03_wav, DC17m_PowerShotAlt_03_wav_len},
    {DC17m_PowerShotAlt_04_wav, DC17m_PowerShotAlt_04_wav_len},
    {DC17m_ModeSwitchToPower_wav, DC17m_ModeSwitchToPower_wav_len},
    {DC17m_ModeSwitchToAuto_wav, DC17m_ModeSwitchToAuto_wav_len}
  };

  for (int i = 0; i < 17; i++) {
    size_t totalLength = embeddedSounds[i].rawLength;

    if (totalLength <= 44) {
      Serial.printf("❌ Embedded sound %d too small to be valid WAV\n", i);
      continue;
    }

    // Skip WAV header (first 44 bytes)
    const uint8_t* dataStart = embeddedSounds[i].rawData + 44;
    size_t dataSize = totalLength - 44;

    // Allocate and copy to PSRAM
    uint8_t* buffer = (uint8_t*)ps_malloc(dataSize);
    if (!buffer) {
      Serial.printf("❌ PSRAM allocation failed for sound %d (%u bytes)\n", i, (unsigned)dataSize);
      continue;
    }

    memcpy(buffer, dataStart, dataSize);
    soundLibrary[i].data = buffer;
    soundLibrary[i].length = dataSize;
    Serial.printf("✅ Sound %d loaded into PSRAM (%u bytes)\n", i, (unsigned)dataSize);
  }
}

void startNewStream(int index) {
  if (!soundLibrary[index].data || soundLibrary[index].length == 0) {
    Serial.printf("❌ Sound %d not loaded\n", index);
    return;
  }

  // 2ms timeout - if the audio task holds the mutex longer than that something
  // is seriously wrong; better to skip the sound than stall the trigger path.
  if (xSemaphoreTake(streamsMutex, pdMS_TO_TICKS(2)) != pdTRUE) {
    Serial.println("⚠️ startNewStream: mutex timeout, sound skipped");
    return;
  }

  // Clean up any finished streams
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (streams[i].active && streams[i].position >= streams[i].length) {
      streams[i].active = false;
      streams[i].data = nullptr;
      streams[i].position = 0;
    }
  }

  // Allocate new stream
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (!streams[i].active || streams[i].position >= streams[i].length) {
      streams[i].data = soundLibrary[index].data;
      streams[i].length = soundLibrary[index].length;
      streams[i].position = 0;
      streams[i].active = true;
      streams[i].age = 0;
      Serial.printf("🎵 Playing sound %d from RAM\n", index);
      xSemaphoreGive(streamsMutex);
      return;
    }
  }

  Serial.println("❌ No free audio streams.");
  xSemaphoreGive(streamsMutex);
}

void loopAudioMixer() {
  const int bufSize = 512;

  // Early exit when nothing is playing
  bool anyActive = false;
  for (int i = 0; i < MAX_STREAMS; i++)
    if (streams[i].active) { anyActive = true; break; }
  if (!anyActive) return;

  // Static buffers — off the main-task stack
  static uint8_t mixBuf[bufSize];
  static uint8_t tempBuf[bufSize];

  // --- Pass 1: compute age-based weights ---
  // Newest stream (age=0) gets weight 1.0, each older stream decays.
  // Decay factor 0.08 chosen so the 4th oldest stream sits at ~75% weight -
  // enough presence to give body without burying the new shot's transient.
  float weights[MAX_STREAMS] = {0};
  float totalWeight = 0;
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (!streams[i].active) continue;
    weights[i] = 1.0f / (1.0f + streams[i].age * 0.08f);
    totalWeight += weights[i];
  }
  // Normalise so the sum of all weights never exceeds MAX_STREAMS,
  // keeping headroom equivalent to a single full-volume stream.
  // Avoid divide-by-zero if somehow totalWeight is 0.
  float normScale = (totalWeight > 0) ? ((float)MAX_STREAMS / totalWeight) : 1.0f;

  // Fixed-point volume scale (0-256) - avoids float multiply in the hot loop
  int32_t volScale = (int32_t)(globalVolume * 256);

  // --- Pass 2: mix each active stream ---
  memset(mixBuf, 0, bufSize);

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (!streams[i].active) continue;

    size_t bytesToRead = min((size_t)bufSize, streams[i].length - streams[i].position);
    if (bytesToRead == 0) {
      streams[i].active = false;
      streams[i].data = nullptr;
      continue;
    }

    memset(tempBuf, 0, bufSize);
    memcpy(tempBuf, streams[i].data + streams[i].position, bytesToRead);
    streams[i].position += bytesToRead;

    // Per-stream weight in fixed-point (x256)
    int32_t wScale = (int32_t)(weights[i] * normScale * 256);

    for (int j = 0; j < (int)bytesToRead; j += 2) {
      int16_t sample = (int16_t)(tempBuf[j] | (tempBuf[j + 1] << 8));
      int16_t mixed  = (int16_t)(mixBuf[j]  | (mixBuf[j + 1]  << 8));

      // Apply volume then age-weight, all in integer - no float in the hot loop
      int32_t scaled = ((int32_t)sample * volScale) >> 8;   // volume
      scaled         = (scaled * wScale) >> 8;               // age weight
      int32_t sum    = (int32_t)mixed + scaled;

      // Soft clip: cubic saturator - rounds peaks smoothly instead of chopping them flat.
      // Operates in float only when clipping would actually occur (rare), so cost is low.
      if (sum > 24000 || sum < -24000) {
        float x = sum / 32768.0f;
        // Cubic soft clip: f(x) = 1.5*x - 0.5*x^3, clamped to [-1, 1]
        if (x >  1.0f) x =  1.0f;
        if (x < -1.0f) x = -1.0f;
        x = 1.5f * x - 0.5f * x * x * x;
        sum = (int32_t)(x * 32768.0f);
      }

      mixBuf[j]     = sum & 0xFF;
      mixBuf[j + 1] = (sum >> 8) & 0xFF;
    }

    if (streams[i].position >= streams[i].length) {
      streams[i].active = false;
      streams[i].data = nullptr;
    }

    streams[i].age++;  // increment after processing so age=0 on first mix call
  }

  size_t written;
  i2s_write(I2S_PORT, mixBuf, bufSize, &written, pdMS_TO_TICKS(5));
}

void triggerHaptic(uint8_t waveform) {
  drv.setWaveform(0, waveform);
  drv.setWaveform(1, 0);
  drv.go();
}

void triggerHapticEvent(String event) {
  if (event == "powerShot") {
    triggerHaptic(15); // Strong click effect
  } else if (event == "autoShot") {
    triggerHaptic(47); // Short medium buzz
  } else if (event == "emptyClick") {
    triggerHaptic(3);  // Light tick
  } else if (event == "modeSwitch") {
    triggerHaptic(13); // Sharp click
  }
}

void triggerHapticRealTime(uint8_t strength) {
  drv.setMode(DRV2605_MODE_REALTIME);      // Switch to real-time playback mode
  drv.setRealtimeValue(strength);          // Strength: 0 (off) to 127 (max)
}

void fireBlaster() {
  if (millis() - lastShotTime < fireRateMs) return;

  bool triggerPressed = digitalRead(TRIGGER_PIN) == HIGH;

  if (ammo > 0) {
    startNewStream(random(0, 9));
    ammo--;

    // Start haptic only once when trigger is held down
    if (!autoHapticActive) {
      triggerHapticRealTime(100);  // Real-time continuous vibration
      autoHapticActive = true;
    }

    // 💥 FULL AUTO LED Flash (cool blue)
    redFadeStart = random(100, 148);
    greenFadeStart = random(32, 64);
    blueFadeStart = random(240, 256);

    ledcWrite(RED_LED_PIN, redFadeStart);
    ledcWrite(GREEN_LED_PIN, greenFadeStart);
    ledcWrite(BLUE_LED_PIN, blueFadeStart);

    fadeStartTime = millis();
    ledActive = true;
    ledOnTime = millis();
    ledDuration = random(ledDurationAutoMin, ledDurationAutoMax);
    fadeDuration = random(fadeDurationAutoMin, fadeDurationAutoMax);

    oledDirty = true;  // ammo changed - redraw at end of loop, not mid-firing-path
    Serial.println("Shot fired!");
  } else {
    // Only play empty click on rising edge
    if (!triggerPreviouslyPressed && triggerPressed) {
      startNewStream(10);  // Empty click sound
      triggerHapticEvent("emptyClick");
      Serial.println("❗ Attempted to fire on empty mag (auto)");
    }
  }

  lastShotTime = millis();
}

// float readBatteryVoltage() {
//   int raw = analogRead(BATTERY_PIN);
//   float vOut = ((float)raw / ADC_MAX) * VREF;
//   return vOut * ((R1 + R2) / R2);
// }

// int getBatteryLevel(float vBat) {
//   if (vBat >= 4.2) return 100;
//   if (vBat <= 3.0) return 0;
//   return (int)((vBat - 3.0) / (4.2 - 3.0) * 100);
// }

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(5);
  display.setCursor(10, 0);
  if (ammo < 10) display.print("0");
  display.print(ammo);
  display.setTextSize(1); // Small text
  display.setCursor(75, 4); // Adjust to align near ammo digits
  display.print(currentFireMode == POWER_SHOT ? "POWER" : "AUTO");

  // int batteryLevel = getBatteryLevel(vBat);
  // for (int i = 0; i < 6; i++) {
  //   int y = 10 + (5 - i) * 6; // Start from bottom
  //   display.drawRect(116, y, 6, 4, SH110X_WHITE);
  //   if (i < map(batteryLevel, 0, 100, 0, 6)) {
  //     display.fillRect(116, y, 6, 4, SH110X_WHITE);
  //   }
  // }

  int ammoBarX = 4, ammoBarY = 54;
  display.drawRect(ammoBarX, ammoBarY, 120, 8, SH110X_WHITE);
  for (int i = 0; i < ammo; i++) {
    int x = ammoBarX + 1 + i * 2;
    if (x + 1 < ammoBarX + 120)
      display.fillRect(x, ammoBarY + 1, 1, 6, SH110X_WHITE);
  }
  display.display();
}

//void showSDError() {
//  display.clearDisplay();
//  display.setTextColor(SH110X_WHITE);
//  display.setCursor(0, 10);
//  display.println("ERROR:\nSD card not found!");
//  display.display();
//}

// -------------------------------------------------------------------
// Audio task - runs on Core 0, feeds I2S DMA continuously.
// Core 1 (loop) handles all inputs, display, haptic and LED logic.
// portMAX_DELAY is safe here: this task has nothing else to do while
// waiting, and the mixer itself holds the mutex for only ~50–100µs.
// -------------------------------------------------------------------
void audioTask(void* param) {
  while (true) {
    if (xSemaphoreTake(streamsMutex, portMAX_DELAY) == pdTRUE) {
      loopAudioMixer();
      xSemaphoreGive(streamsMutex);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);

  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);
  pinMode(MAG_SENSOR_PIN, INPUT_PULLUP);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);

  pinMode(LED_PIN, OUTPUT);   //mag led
  digitalWrite(LED_PIN, LOW);

  ledcAttach(RED_LED_PIN, 5000, 8);  // 5000 Hz, 8-bit resolution
  ledcAttach(GREEN_LED_PIN, 5000, 8);
  ledcAttach(BLUE_LED_PIN, 5000, 8);

  analogReadResolution(12);
  // analogSetAttenuation(ADC_11db);

  display.begin(0x3C, true);
  display.clearDisplay();
  display.display();

  drv.begin();
  drv.useERM();
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);

//  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
//  sdOK = SD.begin(SD_CS);
//  if (!sdOK) {
//    Serial.println("SD init failed!");
//    showSDError();
//  }
  preloadSounds();

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);

  // Create the streams mutex before launching the audio task
  streamsMutex = xSemaphoreCreateMutex();
  if (streamsMutex == NULL) {
    Serial.println("❌ Failed to create audio mutex - halting");
    while (true) delay(1000);  // safe halt, avoids running without protection
  }

  // Pin audio mixer to Core 0, priority 2 (above loop's priority 1,
  // below system tasks). Stack 4096 bytes - comfortable above the ~1.5KB
  // actually needed given static mixer buffers.
  xTaskCreatePinnedToCore(
    audioTask,     // task function
    "AudioMixer",  // name (visible in FreeRTOS debugger)
    4096,          // stack in bytes
    NULL,          // no parameters
    2,             // priority
    NULL,          // no handle needed
    0              // Core 0
  );

  ammo = digitalRead(MAG_SENSOR_PIN) == HIGH ? maxAmmo : 0;
  updateOLED();
}

void loop() {
  static bool lastMagState = false;
  bool magInserted = digitalRead(MAG_SENSOR_PIN) == HIGH;

  if (magInserted != lastMagState) {
    ammo = magInserted ? maxAmmo : 0;
    updateOLED();

    if (magInserted) {
      startNewStream(9);  // <- this is the index of your reload sound in RAM
      Serial.println("🔄 Reloaded!");
    }

    lastMagState = magInserted;
  }

  static bool lastModeButtonState = HIGH;
  bool modeButtonState = digitalRead(MODE_SWITCH_PIN);

  if (lastModeButtonState == HIGH && modeButtonState == LOW) {
    if (currentFireMode == FULL_AUTO) {
      currentFireMode = POWER_SHOT;
      startNewStream(15);  // Mode switch to Power sound
      triggerHapticEvent("modeSwitch");
    } else {
      currentFireMode = FULL_AUTO;
      startNewStream(16);  // Mode switch to Auto sound
      triggerHapticEvent("modeSwitch");
    }
    Serial.printf("🔁 Switched to %s mode\n", currentFireMode == POWER_SHOT ? "Power Shot" : "Auto Fire");
    updateOLED();
  }
  lastModeButtonState = modeButtonState;

  if (ledActive) {
    unsigned long elapsed = millis() - fadeStartTime;
    if (elapsed >= fadeDuration) {
      ledcWrite(RED_LED_PIN, 0);
      ledcWrite(GREEN_LED_PIN, 0);
      ledcWrite(BLUE_LED_PIN, 0);
      ledActive = false;
    } else {
      float fadeRatio = 1.0 - (float)elapsed / fadeDuration;
      fadeRatio = constrain(fadeRatio, 0.0, 1.0);

      // Add flicker on top of fading
      int redOut = redFadeStart * fadeRatio;
      int greenOut = greenFadeStart * fadeRatio;
      int blueOut = blueFadeStart * fadeRatio;

      if (flickerEnabled) {
        // Fast random jitter (each frame)
        int flickerRed = random(-40, 40);
        int flickerGreen = random(-30, 30);
        int flickerBlue = random(-30, 30);

        redOut = constrain(redOut + flickerRed, 0, 255);
        greenOut = constrain(greenOut + flickerGreen, 0, 255);
        blueOut = constrain(blueOut + flickerBlue, 0, 255);
      }

      ledcWrite(RED_LED_PIN, redOut);
      ledcWrite(GREEN_LED_PIN, greenOut);
      ledcWrite(BLUE_LED_PIN, blueOut);
    }
  }

  bool triggerPressed = digitalRead(TRIGGER_PIN) == HIGH;

  if (currentFireMode == POWER_SHOT) {
    if (triggerPressed && !triggerPreviouslyPressed) {
      if (ammo >= 5 && millis() - lastPowerShotTime >= powerShotCooldown) {
        startNewStream(random(11, 15));  // Random Power Shot variation
        ammo -= 5;
        triggerHapticEvent("powerShot");
        lastPowerShotTime = millis();
        // 🔥 POWER SHOT LED Flash (warm orange-red)
        redFadeStart = random(240, 256);
        greenFadeStart = random(80, 120);
        blueFadeStart = random(0, 10);

        ledcWrite(RED_LED_PIN, redFadeStart);
        ledcWrite(GREEN_LED_PIN, greenFadeStart);
        ledcWrite(BLUE_LED_PIN, blueFadeStart);

        fadeStartTime = millis();
        ledActive = true;
        ledOnTime = millis();
        ledDuration = random(ledDurationPowerMin, ledDurationPowerMax);
        fadeDuration = random(fadeDurationPowerMin, fadeDurationPowerMax);
        oledDirty = true;  // ammo changed — redraw at end of loop
        Serial.println("💥 Power Shot fired!");
      } else if (ammo < 5) {
        startNewStream(10);  // Empty click
        triggerHapticEvent("emptyClick");
        Serial.println("❗ Not enough ammo for Power Shot!");
      }
    }
  } else {
    if (triggerPressed) {
      fireBlaster(); // Regular full-auto mode
    }
  }
  triggerPreviouslyPressed = triggerPressed;
  if (!triggerPressed && autoHapticActive) {
    drv.setRealtimeValue(0);                // Stop vibration
    drv.setMode(DRV2605_MODE_INTTRIG);      // Reset to internal trigger mode
    autoHapticActive = false;
  }
  // Volume pot — throttled to every 80ms + EMA filter to kill ADC noise jitter
  if (millis() - lastVolRead > 80) {
    float raw = analogRead(VOLUME_PIN) / 4095.0f;
    smoothVolume  = smoothVolume * 0.75f + raw * 0.25f;  // EMA: ~3 reads to settle
    globalVolume  = smoothVolume;
    lastVolRead   = millis();
  }

  // 💡 Control the blue magazine LED on GPIO13
  digitalWrite(LED_PIN, (ammo > 0 && digitalRead(MAG_SENSOR_PIN) == HIGH) ? HIGH : LOW);

  // OLED flush — only redraws when something actually changed, keeps 8ms I2C cost
  // out of the firing path while still updating every shot within one loop tick
  if (oledDirty) {
    updateOLED();
    oledDirty = false;
  }

  // loopAudioMixer() removed - now runs continuously on Core 0 via audioTask()
  // if (millis() - lastBatteryRead > 500) {
  //   vBat = readBatteryVoltage();
  //   lastBatteryRead = millis();
  // }
}

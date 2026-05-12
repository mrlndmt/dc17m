// =============================================================
//  DC-17M MODULAR DIAGNOSTIC
//  Change TEST_STEP to select which module to test
//  Open Serial Monitor at 115200 baud
//
//  STEP 1 — GPIO outputs   : RGB LED + magazine LED
//  STEP 2 — GPIO inputs    : trigger, mode switch, reed switch, pot
//  STEP 3 — OLED only      : SH1106 on I2C (DRV2605 disconnected)
//  STEP 4 — DRV2605 only   : haptic driver on I2C (OLED disconnected)
//  STEP 5 — MAX98357 I2S   : audio amp + speaker (1kHz sine test tone)
// =============================================================

#define TEST_STEP 3   // ← change this (1–5) and reflash

// =============================================================
//  PIN DEFINITIONS
// =============================================================
#define MODE_SWITCH_PIN   15
#define TRIGGER_PIN       14
#define MAG_SENSOR_PIN    20
#define VOLUME_PIN         1
#define RED_LED_PIN        6
#define GREEN_LED_PIN      5
#define BLUE_LED_PIN       4
#define LED_PIN           13

#define I2C_SDA            8
#define I2C_SCL            9

#define I2S_BCLK          38
#define I2S_LRCK          39
#define I2S_DOUT          37

// =============================================================
//  INCLUDES — only what each step needs
// =============================================================
#include <Arduino.h>

#if TEST_STEP == 3
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SH110X.h>
  Adafruit_SH1106G display(128, 64, &Wire, -1);
  bool oledOK = false;
#endif

#if TEST_STEP == 4
  #include <Wire.h>
  #include <Adafruit_DRV2605.h>
  Adafruit_DRV2605 drv;
  bool drvOK = false;
#endif

#if TEST_STEP == 5
  #include "driver/i2s.h"
  #define I2S_PORT I2S_NUM_0
  #define SAMPLE_RATE 44100
  #define SINE_FREQ   1000
  #define AMPLITUDE   20000
#endif

// =============================================================
//  SHARED STATE
// =============================================================
unsigned long lastPrint   = 0;
unsigned long lastAction  = 0;
int           actionStep  = 0;

// =============================================================
//  STEP 1 HELPERS — GPIO outputs
// =============================================================
#if TEST_STEP == 1
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(RED_LED_PIN,   r);
  ledcWrite(GREEN_LED_PIN, g);
  ledcWrite(BLUE_LED_PIN,  b);
}

const char* rgbNames[] = { "RED", "GREEN", "BLUE", "WHITE", "OFF" };
uint8_t rgbR[] = {255,   0,   0, 255, 0};
uint8_t rgbG[] = {  0, 255,   0, 255, 0};
uint8_t rgbB[] = {  0,   0, 255, 255, 0};
#endif

// =============================================================
//  STEP 5 HELPERS — I2S sine wave
// =============================================================
#if TEST_STEP == 5
void writeI2SSine() {
  const int bufSamples = 256;
  static int16_t sineBuf[bufSamples];
  static bool built = false;
  if (!built) {
    for (int i = 0; i < bufSamples; i++)
      sineBuf[i] = (int16_t)(AMPLITUDE * sin(2.0 * PI * i / bufSamples));
    built = true;
  }
  size_t written;
  i2s_write(I2S_PORT, sineBuf, sizeof(sineBuf), &written, pdMS_TO_TICKS(10));
}
#endif

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n\n========================================");
  Serial.printf( "  DC-17M DIAGNOSTIC — STEP %d\n", TEST_STEP);
  Serial.println("========================================");

  // ── STEP 1 — GPIO outputs ──────────────────────────────────
  #if TEST_STEP == 1
  Serial.println("\n[STEP 1]  GPIO outputs: RGB LED + magazine LED");
  Serial.println("  Watch the RGB LED cycle R → G → B → White → Off");
  Serial.println("  Magazine LED will blink independently\n");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  ledcAttach(RED_LED_PIN,   5000, 8);
  ledcAttach(GREEN_LED_PIN, 5000, 8);
  ledcAttach(BLUE_LED_PIN,  5000, 8);
  setRGB(0, 0, 0);
  Serial.println("[STEP 1]  LEDC configured — starting colour cycle");
  #endif

  // ── STEP 2 — GPIO inputs ───────────────────────────────────
  #if TEST_STEP == 2
  Serial.println("\n[STEP 2]  GPIO inputs: trigger, mode switch, reed switch, pot");
  Serial.println("  Press each button and insert/remove the mag");
  Serial.println("  Turn the pot end to end — should sweep 0 to 4095\n");

  pinMode(TRIGGER_PIN,     INPUT_PULLUP);
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  pinMode(MAG_SENSOR_PIN,  INPUT_PULLUP);
  Serial.println("[STEP 2]  Inputs configured — reporting every 500ms");
  #endif

  // ── STEP 3 — OLED only ─────────────────────────────────────
  #if TEST_STEP == 3
  Serial.println("\n[STEP 3]  OLED SH1106 — DRV2605 must be DISCONNECTED");
  Serial.println("  Expected I2C address: 0x3C (try 0x3D if it fails)\n");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Serial.printf("[I2C]     Bus on SDA=GPIO%d SCL=GPIO%d @ 100kHz\n", I2C_SDA, I2C_SCL);

  Serial.println("[I2C]     Scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Device at 0x%02X", addr);
      if (addr == 0x3C || addr == 0x3D) Serial.print("  ← OLED SH1106");
      Serial.println();
      found++;
    }
  }
  Serial.printf("[I2C]     %d device(s) found\n\n", found);

  Serial.print("[OLED]    Initialising at 0x3C... ");
  if (display.begin(0x3C, true)) {
    oledOK = true;
    Serial.println("OK ✓");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("DC-17M DIAGNOSTIC");
    display.println("STEP 3: OLED test");
    display.println("");
    display.println("If you see this,");
    display.println("OLED is working!");
    display.display();
  } else {
    Serial.println("FAIL ✗ at 0x3C — trying 0x3D...");
    if (display.begin(0x3D, true)) {
      oledOK = true;
      Serial.println("[OLED]    OK at 0x3D ✓ — update OLED_ADDR to 0x3D in main sketch");
      display.clearDisplay();
      display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0);
      display.println("OLED OK at 0x3D");
      display.println("Update main sketch!");
      display.display();
    } else {
      Serial.println("[OLED]    FAIL at 0x3D too ✗");
      Serial.println("[OLED]    Check: SDA=GPIO8, SCL=GPIO9, VCC=3.3V, pull-ups present");
    }
  }
  #endif

  // ── STEP 4 — DRV2605 only ──────────────────────────────────
  #if TEST_STEP == 4
  Serial.println("\n[STEP 4]  DRV2605 haptic — OLED must be DISCONNECTED");
  Serial.println("  Expected I2C address: 0x5A\n");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Serial.printf("[I2C]     Bus on SDA=GPIO%d SCL=GPIO%d @ 100kHz\n", I2C_SDA, I2C_SCL);

  Serial.println("[I2C]     Scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Device at 0x%02X", addr);
      if (addr == 0x5A || addr == 0x5B) Serial.print("  ← DRV2605");
      Serial.println();
      found++;
    }
  }
  Serial.printf("[I2C]     %d device(s) found\n\n", found);

  Serial.print("[DRV2605] Initialising... ");
  if (drv.begin()) {
    drvOK = true;
    drv.selectLibrary(1);
    drv.setMode(DRV2605_MODE_INTTRIG);
    Serial.println("OK ✓");
    Serial.println("[DRV2605] Firing boot pulse — did you feel it?");
    drv.setWaveform(0, 1);
    drv.setWaveform(1, 0);
    drv.go();
  } else {
    Serial.println("FAIL ✗");
    Serial.println("[DRV2605] Check: SDA=GPIO8, SCL=GPIO9, VCC=3.3V, address=0x5A");
  }
  #endif

  // ── STEP 5 — MAX98357 I2S ──────────────────────────────────
  #if TEST_STEP == 5
  Serial.println("\n[STEP 5]  MAX98357 I2S audio — speaker must be connected");
  Serial.printf( "  BCLK=GPIO%d  LRCK=GPIO%d  DOUT=GPIO%d\n\n",
                 I2S_BCLK, I2S_LRCK, I2S_DOUT);

  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = 256,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num    = I2S_BCLK,
    .ws_io_num     = I2S_LRCK,
    .data_out_num  = I2S_DOUT,
    .data_in_num   = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err == ESP_OK) {
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
    Serial.println("[I2S]     Driver installed OK ✓");
    Serial.printf( "[I2S]     Playing %dHz sine tone at %d Hz sample rate\n",
                   SINE_FREQ, SAMPLE_RATE);
    Serial.println("[I2S]     You should hear a continuous tone from the speaker");
  } else {
    Serial.printf("[I2S]     Driver install FAILED — error 0x%x ✗\n", err);
  }
  #endif

  Serial.println("\n[READY]   Entering loop...\n");
}

// =============================================================
//  LOOP
// =============================================================
void loop() {
  unsigned long now = millis();

  // ── STEP 1 — cycle RGB every 800ms, blink mag LED ──────────
  #if TEST_STEP == 1
  if (now - lastAction > 800) {
    setRGB(rgbR[actionStep], rgbG[actionStep], rgbB[actionStep]);
    Serial.printf("[RGB]     %s\n", rgbNames[actionStep]);
    actionStep = (actionStep + 1) % 5;
    lastAction = now;
  }
  // Mag LED blinks on a different cadence to confirm it's independent
  digitalWrite(LED_PIN, ((now / 400) % 2) ? HIGH : LOW);
  #endif

  // ── STEP 2 — print inputs every 500ms ──────────────────────
  #if TEST_STEP == 2
  if (now - lastPrint > 500) {
    bool trigger = digitalRead(TRIGGER_PIN)     == LOW;
    bool modeSw  = digitalRead(MODE_SWITCH_PIN) == LOW;
    bool mag     = digitalRead(MAG_SENSOR_PIN)  == HIGH;
    int  pot     = analogRead(VOLUME_PIN);

    Serial.println("┌──────────────────────────────┐");
    Serial.printf( "│ TRIGGER  GPIO14 : %s\n", trigger ? "PRESSED ◄" : "open     ");
    Serial.printf( "│ MODE SW  GPIO15 : %s\n", modeSw  ? "PRESSED ◄" : "open     ");
    Serial.printf( "│ REED SW  GPIO20 : %s\n", mag     ? "MAG IN  ◄" : "no mag   ");
    Serial.printf( "│ POT      GPIO 1 : %4d / 4095\n", pot);
    Serial.println("└──────────────────────────────┘");
    lastPrint = now;
  }
  #endif

  // ── STEP 3 — scroll counter on OLED every second ───────────
  #if TEST_STEP == 3
  if (now - lastPrint > 1000) {
    actionStep++;
    if (oledOK) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0);
      display.println("DC-17M STEP 3");
      display.println("OLED OK ✓");
      display.println("");
      display.printf("Uptime : %lus\n", now / 1000);
      display.printf("Counter: %d\n", actionStep);
      display.display();
      Serial.printf("[OLED]    Frame %d drawn OK\n", actionStep);
    } else {
      Serial.printf("[OLED]    Still failing — check wiring (%ds)\n", now / 1000);
    }
    lastPrint = now;
  }
  #endif

  // ── STEP 4 — fire haptic every 3s ──────────────────────────
  #if TEST_STEP == 4
  if (now - lastPrint > 1000) {
    Serial.printf("[DRV2605] Status: %s — uptime %lus\n",
                  drvOK ? "OK ✓" : "FAIL ✗", now / 1000);
    lastPrint = now;
  }
  if (drvOK && now - lastAction > 3000) {
    Serial.println("[DRV2605] Firing pulse — feel it?");
    drv.setWaveform(0, 15);
    drv.setWaveform(1,  0);
    drv.go();
    lastAction = now;
  }
  #endif

  // ── STEP 5 — stream sine tone continuously ─────────────────
  #if TEST_STEP == 5
  writeI2SSine();
  if (now - lastPrint > 2000) {
    Serial.printf("[I2S]     Streaming — uptime %lus\n", now / 1000);
    lastPrint = now;
  }
  #endif
}

// =============================================================
//  DC-17M DIAGNOSTIC SKETCH
//  ESP32-S3-WROOM-1 N16R8
//  Tests every component individually — NO sound, no PSRAM load
//  Open Serial Monitor at 115200 baud
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "driver/i2s.h"
#include <driver/ledc.h>

// ── Pin definitions (from main sketch) ────────────────────────
#define MODE_SWITCH_PIN   15
#define TRIGGER_PIN       14
#define MAG_SENSOR_PIN    20
#define VOLUME_PIN         1
#define RED_LED_PIN        6
#define GREEN_LED_PIN      5
#define BLUE_LED_PIN       4
#define LED_PIN           13   // magazine LED (2-pin, blue)

// ── I2C pins — explicit declaration to avoid default pin ambiguity ─
// Default ESP32-S3 I2C: SDA=GPIO8, SCL=GPIO9
// Change these if your wiring differs
#define I2C_SDA   8
#define I2C_SCL   9

#define I2S_BCLK 38
#define I2S_LRCK 39
#define I2S_DOUT 37

#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT     64

// ── I2C address for SH1106 (typically 0x3C) ───────────────────
#define OLED_ADDR       0x3C

// ── Objects ───────────────────────────────────────────────────
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Test state ────────────────────────────────────────────────
bool oledOK = false;

// ── Timing ────────────────────────────────────────────────────
unsigned long lastPrint     = 0;
unsigned long lastRgbStep   = 0;
int  rgbStep = 0;


// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(800);   // give serial monitor time to open
  Serial.println("\n\n========================================");
  Serial.println("  DC-17M DIAGNOSTIC — v1.0");
  Serial.println("========================================\n");

  // ── GPIO inputs ───────────────────────────────────────────
  pinMode(TRIGGER_PIN,     INPUT_PULLUP);
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  pinMode(MAG_SENSOR_PIN,  INPUT_PULLUP);

  // ── GPIO outputs ──────────────────────────────────────────
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ── LEDC for RGB LED (ESP32 Arduino core v3.x API) ────────
  ledcAttach(RED_LED_PIN,   5000, 8);
  ledcAttach(GREEN_LED_PIN, 5000, 8);
  ledcAttach(BLUE_LED_PIN,  5000, 8);
  setRGB(0, 0, 0);
  Serial.println("[RGB]     LEDC channels configured");

  // ── I2C bus ───────────────────────────────────────────────
  // Explicit SDA/SCL — removes default pin ambiguity on ESP32-S3
  // 100kHz (standard mode) instead of 400kHz — more tolerant of
  // long wires, missing pull-ups, or marginal signal quality
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Serial.printf("[I2C]     Bus started on SDA=GPIO%d SCL=GPIO%d @ 100kHz\n", I2C_SDA, I2C_SCL);
  Serial.println("[I2C]     Scanning...");
  scanI2C();

  // ── OLED ──────────────────────────────────────────────────
  Serial.print("[OLED]    Initialising SH1106... ");
  if (display.begin(OLED_ADDR, true)) {
    oledOK = true;
    Serial.println("OK ✓");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("DC-17M DIAGNOSTIC");
    display.println("──────────────────");
    display.println("Serial: 115200 baud");
    display.println("");
    display.println("Testing components...");
    display.display();
  } else {
    Serial.println("FAILED ✗  — check wiring / I2C address");
  }

  // ── ADC / potentiometer ───────────────────────────────────
  Serial.println("[POT]     Volume pot on GPIO1 (ADC) — will print in loop");

  Serial.println("\n──────── BOOT SUMMARY ────────");
  Serial.printf("  OLED   (SH1106)  : %s\n", oledOK ? "OK ✓" : "FAIL ✗");
  Serial.println("  RGB LED          : check colours cycling in loop");
  Serial.println("  Mag LED (GPIO13) : follows MAG_SENSOR state");
  Serial.println("  Trigger  (GPIO14): pull to test");
  Serial.println("  Mode sw  (GPIO15): toggle to test");
  Serial.println("  Reed sw  (GPIO20): insert mag to test");
  Serial.println("  Volume   (GPIO1) : turn pot to test");
  Serial.println("  DRV2605          : EXCLUDED from this build");
  Serial.println("──────────────────────────────\n");
  Serial.println("Entering diagnostic loop...\n");
}


// =============================================================
//  HELPERS
// =============================================================

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(RED_LED_PIN,   r);
  ledcWrite(GREEN_LED_PIN, g);
  ledcWrite(BLUE_LED_PIN,  b);
}

// Scans I2C bus and prints found addresses — helps confirm OLED + DRV2605
void scanI2C() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  [I2C]   Device found at 0x%02X", addr);
      if (addr == 0x3C || addr == 0x3D) Serial.print("  ← OLED SH1106");
      if (addr == 0x5A || addr == 0x5B) Serial.print("  ← DRV2605 haptic");
      Serial.println();
      found++;
    }
  }
  if (found == 0) Serial.println("  [I2C]   No devices found — check SDA/SCL wiring");
  Serial.printf("  [I2C]   %d device(s) found\n\n", found);
}

// Cycles RGB LED through R → G → B → White → Off, one step per call
void stepRGB() {
  switch (rgbStep) {
    case 0: setRGB(255,   0,   0); break;  // Red
    case 1: setRGB(  0, 255,   0); break;  // Green
    case 2: setRGB(  0,   0, 255); break;  // Blue
    case 3: setRGB(255, 255, 255); break;  // White
    case 4: setRGB(  0,   0,   0); break;  // Off
  }
  rgbStep = (rgbStep + 1) % 5;
}

// Updates OLED with live component states
void updateDiagOLED(bool trigger, bool modeSw, bool mag, int pot) {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);
  display.println("DC-17M DIAGNOSTIC");
  display.drawFastHLine(0, 9, 128, SH110X_WHITE);

  display.setCursor(0, 12);
  display.printf("TRIGGER : %s\n", trigger ? "PRESSED  <--" : "open");
  display.printf("MODE SW : %s\n", modeSw  ? "PRESSED  <--" : "open");
  display.printf("REED SW : %s\n", mag     ? "MAG IN   <--" : "no mag");
  display.printf("VOLUME  : %d/4095\n", pot);

  display.drawFastHLine(0, 52, 128, SH110X_WHITE);
  display.setCursor(0, 55);
  display.printf("OLED:%s DRV2605:excluded", oledOK ? "OK" : "ERR");
  display.display();
}


// =============================================================
//  LOOP
// =============================================================
void loop() {
  unsigned long now = millis();

  // ── Read all inputs ───────────────────────────────────────
  bool trigger = (digitalRead(TRIGGER_PIN)     == LOW);   // active LOW pullup
  bool modeSw  = (digitalRead(MODE_SWITCH_PIN) == LOW);
  bool mag     = (digitalRead(MAG_SENSOR_PIN)  == HIGH);  // HIGH = mag inserted (reed closed)
  int  pot     = analogRead(VOLUME_PIN);

  // ── Magazine LED follows reed switch ─────────────────────
  digitalWrite(LED_PIN, mag ? HIGH : LOW);

  // ── RGB cycles every 600ms automatically ──────────────────
  if (now - lastRgbStep > 600) {
    stepRGB();
    lastRgbStep = now;
  }

  // ── OLED live update ──────────────────────────────────────
  updateDiagOLED(trigger, modeSw, mag, pot);

  // ── Serial report every 500ms ─────────────────────────────
  if (now - lastPrint > 500) {
    Serial.println("┌─────────────────────────────┐");
    Serial.printf( "│ TRIGGER  (GPIO14) : %s\n", trigger ? "PRESSED ◄" : "open     ");
    Serial.printf( "│ MODE SW  (GPIO15) : %s\n", modeSw  ? "PRESSED ◄" : "open     ");
    Serial.printf( "│ REED SW  (GPIO20) : %s\n", mag     ? "MAG IN  ◄" : "no mag   ");
    Serial.printf( "│ VOLUME   (GPIO 1) : %4d / 4095\n", pot);
    Serial.printf( "│ MAG LED  (GPIO13) : %s\n", mag     ? "ON  ◄"     : "off      ");
    Serial.printf( "│ RGB LED           : step %d/4\n", (rgbStep + 4) % 5);
    Serial.printf( "│ OLED              : %s\n", oledOK  ? "OK ✓" : "FAIL ✗");
    Serial.println("│ DRV2605           : excluded");
    Serial.println("└─────────────────────────────┘");
    lastPrint = now;
  }
}

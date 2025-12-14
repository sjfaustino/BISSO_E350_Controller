/**
 * @file lcd_interface.cpp
 * @brief LCD (20x4) abstraction layer (I2C/Serial)
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#include "lcd_interface.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>
#include <stdio.h>
#include "system_constants.h"
#include "encoder_calibration.h"
#include "plc_iface.h" 

// Global I2C LCD instance (will be initialized in lcdInterfaceInit)
static LiquidCrystal_I2C* lcd_i2c = nullptr;

struct {
  lcd_mode_t mode;
  bool backlight_on;
  uint32_t last_update;
  uint32_t update_count;
  char display[LCD_ROWS][LCD_COLS + 1];
  bool display_dirty[LCD_ROWS];
  bool i2c_found;
} lcd_state = {
  LCD_MODE_SERIAL,
  true,
  0,
  0,
  {"", "", "", ""},
  {true, true, true, true},
  false
};

void lcdInterfaceInit() {
  Serial.println("[LCD] Initializing...");

  for (int i = 0; i < LCD_ROWS; i++) {
    memset(lcd_state.display[i], 0, LCD_COLS + 1);
    lcd_state.display_dirty[i] = true;
  }

  lcd_state.last_update = millis();
  lcd_state.update_count = 0;

  // FIX: Wire.begin() is called by PLC init, so don't call it again to avoid I2C deadlock
  // Just probe for LCD presence on the already-initialized I2C bus
  Wire.beginTransmission(LCD_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    lcd_state.i2c_found = true;

    // Initialize LiquidCrystal_I2C (20x4 display)
    // Standard pin mapping: RS=0, RW=1, E=2, BL=3 (most common PCF8574 backpack)
    lcd_i2c = new LiquidCrystal_I2C(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
    if (lcd_i2c) {
      // Extended initialization with delays for reliable startup
      lcd_i2c->init();
      delay(50);  // Wait for LCD to stabilize after init
      lcd_i2c->backlight();
      delay(10);
      lcd_i2c->home();  // Move cursor to home position
      lcd_i2c->clear();
      delay(10);

      // Write test pattern to verify LCD is responding
      lcd_i2c->setCursor(0, 0);
      lcd_i2c->print("LCD Init OK");

      lcd_state.mode = LCD_MODE_I2C;
      Serial.printf("[LCD] [OK] I2C LCD Initialized at 0x%02X (20x4)\n", LCD_I2C_ADDR);
    } else {
      lcd_state.mode = LCD_MODE_SERIAL;
      Serial.println("[LCD] [ERR] Failed to allocate LiquidCrystal_I2C, using Serial");
    }
  } else {
    lcd_state.mode = LCD_MODE_SERIAL;
    Serial.println("[LCD] [WARN] I2C Not Found, using Serial simulation");
  }

  Serial.println("[LCD] [OK] Ready");
}

void lcdInterfaceUpdate() {
  uint32_t now = millis();
  if (now - lcd_state.last_update < LCD_REFRESH_INTERVAL_MS) return;

  lcd_state.last_update = now;
  lcd_state.update_count++;

  switch(lcd_state.mode) {
    case LCD_MODE_I2C:
      if (lcd_i2c) {
        for (int i = 0; i < LCD_ROWS; i++) {
          if (lcd_state.display_dirty[i]) {
            lcd_i2c->setCursor(0, i);
            lcd_i2c->print(lcd_state.display[i]);
            lcd_state.display_dirty[i] = false;
          }
        }
      }
      break;

    case LCD_MODE_SERIAL:
      for (int i = 0; i < LCD_ROWS; i++) {
        if (lcd_state.display_dirty[i]) {
          Serial.printf("[LCD:%d] %s\n", i, lcd_state.display[i]);
          lcd_state.display_dirty[i] = false;
        }
      }
      break;

    case LCD_MODE_NONE:
      break;
  }
}

void lcdInterfacePrintLine(uint8_t line, const char* text) {
  if (line >= LCD_ROWS || text == NULL) return;
  strncpy(lcd_state.display[line], text, LCD_COLS);
  lcd_state.display[line][LCD_COLS] = '\0';
  lcd_state.display_dirty[line] = true;
}

void lcdInterfacePrintAxes(int32_t x_counts, int32_t y_counts, int32_t z_counts, int32_t a_counts) {
    char line1[LCD_COLS + 1];
    char line2[LCD_COLS + 1];

    const float def_lin = (float)MOTION_POSITION_SCALE_FACTOR;
    const float def_ang = (float)MOTION_POSITION_SCALE_FACTOR_DEG;
    
    float sx = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : def_lin;
    float sy = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : def_lin;
    float sz = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : def_lin;
    float sa = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : def_ang;

    snprintf(line1, LCD_COLS + 1, "X %7.1f  Y %7.1f", x_counts/sx, y_counts/sy);
    
    // FIX: Removed " DEG" suffix to fit within 20 chars and prevent truncation warning.
    // Length: "Z " (2) + Val (7) + " A " (3) + Val (7) = 19 chars (Fits in 20)
    snprintf(line2, LCD_COLS + 1, "Z %7.1f A %7.1f", z_counts/sz, a_counts/sa);
    
    lcdInterfacePrintLine(0, line1);
    lcdInterfacePrintLine(1, line2);
}

void lcdInterfaceSetMode(lcd_mode_t mode) {
  if (mode == LCD_MODE_I2C && !lcd_state.i2c_found) {
    Serial.println("[LCD] [ERR] I2C Hardware not present");
    return;
  }
  lcd_state.mode = mode;
  Serial.printf("[LCD] [INFO] Mode set to %d\n", mode);
}

lcd_mode_t lcdInterfaceGetMode() { return lcd_state.mode; }

void lcdInterfaceClear() {
  for (int i = 0; i < LCD_ROWS; i++) {
    memset(lcd_state.display[i], ' ', LCD_COLS);
    lcd_state.display[i][LCD_COLS] = '\0';
    lcd_state.display_dirty[i] = true;
  }
  if (lcd_i2c) {
    lcd_i2c->clear();
  }
  Serial.println("[LCD] [OK] Cleared");
}

void lcdInterfaceBacklight(bool on) {
  lcd_state.backlight_on = on;
  if (lcd_i2c) {
    if (on) {
      lcd_i2c->backlight();
    } else {
      lcd_i2c->noBacklight();
    }
  }
  Serial.printf("[LCD] Backlight: %s\n", on ? "ON" : "OFF");
}

void lcdInterfaceDiagnostics() {
  Serial.println("\n[LCD] === Diagnostics ===");
  // FIX: Cast for printf to match %lu
  Serial.printf("Mode: %d\nI2C Found: %s\nBacklight: %s\nUpdates: %lu\n", 
    lcd_state.mode, lcd_state.i2c_found ? "YES" : "NO", 
    lcd_state.backlight_on ? "ON" : "OFF", (unsigned long)lcd_state.update_count);
    
  for (int i = 0; i < LCD_ROWS; i++) {
    Serial.printf("  [%d] %s\n", i, lcd_state.display[i]);
  }
}
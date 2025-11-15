#include "lcd_interface.h"
#include <Wire.h>

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
  Serial.println("[LCD] LCD interface initializing...");
  
  // Initialize display buffer
  for (int i = 0; i < LCD_ROWS; i++) {
    memset(lcd_state.display[i], 0, LCD_COLS + 1);
    lcd_state.display_dirty[i] = true;
  }
  
  lcd_state.last_update = millis();
  lcd_state.update_count = 0;
  
  // Try I2C mode first
  Wire.begin(4, 5, 100000);
  Wire.beginTransmission(LCD_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    lcd_state.mode = LCD_MODE_I2C;
    lcd_state.i2c_found = true;
    Serial.print("[LCD] I2C LCD found at 0x");
    Serial.println(LCD_I2C_ADDR, HEX);
  } else {
    lcd_state.mode = LCD_MODE_SERIAL;
    Serial.println("[LCD] No I2C LCD found, using serial mode");
  }
  
  Serial.println("[LCD] LCD interface ready");
}

void lcdInterfaceUpdate() {
  uint32_t now = millis();
  if (now - lcd_state.last_update < LCD_UPDATE_INTERVAL_MS) {
    return;
  }
  
  lcd_state.last_update = now;
  lcd_state.update_count++;
  
  // Update display (implementation depends on mode)
  switch(lcd_state.mode) {
    case LCD_MODE_I2C:
      // Send to I2C LCD (would use Wire library)
      for (int i = 0; i < LCD_ROWS; i++) {
        if (lcd_state.display_dirty[i]) {
          // Write line i to I2C LCD
          lcd_state.display_dirty[i] = false;
        }
      }
      break;
      
    case LCD_MODE_SERIAL:
      // Output to serial
      for (int i = 0; i < LCD_ROWS; i++) {
        if (lcd_state.display_dirty[i]) {
          Serial.print("[LCD:");
          Serial.print(i);
          Serial.print("] ");
          Serial.println(lcd_state.display[i]);
          lcd_state.display_dirty[i] = false;
        }
      }
      break;
      
    case LCD_MODE_NONE:
      break;
  }
}

void lcdInterfacePrintLine(uint8_t line, const char* text) {
  if (line >= LCD_ROWS || text == NULL) {
    return;
  }
  
  // Pad or truncate to LCD_COLS
  memset(lcd_state.display[line], ' ', LCD_COLS);
  strncpy(lcd_state.display[line], text, LCD_COLS);
  lcd_state.display[line][LCD_COLS] = '\0';
  lcd_state.display_dirty[line] = true;
}

void lcdInterfaceSetMode(lcd_mode_t mode) {
  if (mode == LCD_MODE_I2C && !lcd_state.i2c_found) {
    Serial.println("[LCD] ERROR: I2C LCD not found");
    return;
  }
  
  lcd_state.mode = mode;
  Serial.print("[LCD] Mode set to: ");
  switch(mode) {
    case LCD_MODE_I2C: Serial.println("I2C"); break;
    case LCD_MODE_SERIAL: Serial.println("SERIAL"); break;
    case LCD_MODE_NONE: Serial.println("NONE"); break;
  }
}

lcd_mode_t lcdInterfaceGetMode() {
  return lcd_state.mode;
}

void lcdInterfaceClear() {
  for (int i = 0; i < LCD_ROWS; i++) {
    memset(lcd_state.display[i], ' ', LCD_COLS);
    lcd_state.display[i][LCD_COLS] = '\0';
    lcd_state.display_dirty[i] = true;
  }
  Serial.println("[LCD] Display cleared");
}

void lcdInterfaceBacklight(bool on) {
  lcd_state.backlight_on = on;
  Serial.print("[LCD] Backlight: ");
  Serial.println(on ? "ON" : "OFF");
}

void lcdInterfaceDiagnostics() {
  Serial.println("\n[LCD] === LCD Interface Diagnostics ===");
  
  Serial.print("Mode: ");
  switch(lcd_state.mode) {
    case LCD_MODE_I2C: Serial.println("I2C (20x4)"); break;
    case LCD_MODE_SERIAL: Serial.println("SERIAL"); break;
    case LCD_MODE_NONE: Serial.println("NONE"); break;
  }
  
  Serial.print("I2C Available: ");
  Serial.println(lcd_state.i2c_found ? "YES" : "NO");
  
  Serial.print("Backlight: ");
  Serial.println(lcd_state.backlight_on ? "ON" : "OFF");
  
  Serial.print("Updates: ");
  Serial.println(lcd_state.update_count);
  
  Serial.println("Display Content:");
  for (int i = 0; i < LCD_ROWS; i++) {
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] ");
    Serial.println(lcd_state.display[i]);
  }
}

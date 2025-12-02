#include "lcd_interface.h"
#include <Wire.h>
#include <string.h>
#include <stdio.h>
#include "system_constants.h" 
#include "encoder_calibration.h" 
#include "plc_iface.h" // <-- FIX: Added to provide PLC_SDA_PIN, PLC_SCL_PIN, PLC_I2C_SPEED

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
  
  for (int i = 0; i < LCD_ROWS; i++) {
    memset(lcd_state.display[i], 0, LCD_COLS + 1);
    lcd_state.display_dirty[i] = true;
  }
  
  lcd_state.last_update = millis();
  lcd_state.update_count = 0;
  
  // Try I2C mode first
  // FIX: These constants are now correctly declared due to the new include.
  Wire.begin(PLC_SDA_PIN, PLC_SCL_PIN, PLC_I2C_SPEED);
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
  // Check against system constant for refresh rate
  if (now - lcd_state.last_update < LCD_REFRESH_INTERVAL_MS) { 
    return;
  }
  
  lcd_state.last_update = now;
  lcd_state.update_count++;
  
  switch(lcd_state.mode) {
    case LCD_MODE_I2C:
      // In a full implementation, the I2C writes to the LCD module happen here,
      // typically refreshing only the dirty lines.
      for (int i = 0; i < LCD_ROWS; i++) {
        if (lcd_state.display_dirty[i]) {
          // I2C Write Logic (omitted for brevity)
          lcd_state.display_dirty[i] = false;
        }
      }
      break;
      
    case LCD_MODE_SERIAL:
      // Output to serial terminal for debugging/simulated display
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
  
  strncpy(lcd_state.display[line], text, LCD_COLS);
  lcd_state.display[line][LCD_COLS] = '\0';
  lcd_state.display_dirty[line] = true;
}

// Implements the 10Hz axis position display logic using individual PPM/PPD factors
void lcdInterfacePrintAxes(int32_t x_counts, int32_t y_counts, int32_t z_counts, int32_t a_counts) {
    char line1_buffer[LCD_COLS + 1];
    char line2_buffer[LCD_COLS + 1];

    // --- Get Calibration Factors (Use correct default scale factor if Cal is 0) ---
    const float default_scale = (float)MOTION_POSITION_SCALE_FACTOR;
    const float default_scale_deg = (float)MOTION_POSITION_SCALE_FACTOR_DEG;
    
    // Linear Axes (X, Y, Z) use pulses_per_mm
    float x_scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : default_scale;
    float y_scale = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : default_scale;
    float z_scale = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : default_scale;
    
    // Rotational Axis (A) uses pulses_per_degree
    float a_scale = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : default_scale_deg;


    // --- Line 1: X and Y Axes (mm) ---
    float x_mm = (float)x_counts / x_scale;
    float y_mm = (float)y_counts / y_scale;
    
    // Format: "X -9999.9  Y -9999.9" (Total 20 chars)
    snprintf(line1_buffer, LCD_COLS + 1, "X %7.1f  Y %7.1f", x_mm, y_mm);
    lcdInterfacePrintLine(0, line1_buffer);


    // --- Line 2: Z (mm) and A (degrees) Axes ---
    float z_mm = (float)z_counts / z_scale;
    float a_deg = (float)a_counts / a_scale;
    
    // Format: "Z -999.9   A    -99.9 DEG" (Total 20 chars)
    snprintf(line2_buffer, LCD_COLS + 1, "Z %7.1f A %5.1f DEG", z_mm, a_deg);
    lcdInterfacePrintLine(1, line2_buffer);
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
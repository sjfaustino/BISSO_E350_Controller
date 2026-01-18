/**
 * @file lcd_interface.cpp
 * @brief LCD (20x4) abstraction layer (I2C/Serial)
 * @project PosiPro
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#include "lcd_interface.h"
#include "encoder_calibration.h"
#include "plc_iface.h"
#include "spindle_current_monitor.h"
#include "system_constants.h"
#include "task_manager.h"
#include "encoder_wj66.h"
#include "config_unified.h"
#include "config_keys.h"
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

#include "serial_logger.h"
#include "system_tuning.h"

// Global I2C LCD instance (will be initialized in lcdInterfaceInit)
static LiquidCrystal_I2C *lcd_i2c = nullptr;

struct {
  lcd_mode_t mode;
  bool backlight_on;
  uint32_t last_update;
  uint32_t update_count;
  char display[LCD_ROWS][LCD_COLS + 1];
  bool display_dirty[LCD_ROWS];
  bool i2c_found;
  uint16_t consecutive_i2c_errors;
} lcd_state = {LCD_MODE_SERIAL,          true, 0, 0, {"", "", "", ""},
               {true, true, true, true}, false, 0};

void lcdInterfaceInit() {
  logPrintln("[LCD] Initializing...");

  for (int i = 0; i < LCD_ROWS; i++) {
    memset(lcd_state.display[i], 0, LCD_COLS + 1);
    lcd_state.display_dirty[i] = true;
  }

  lcd_state.last_update = millis();
  lcd_state.update_count = 0;

  // FIX: Wire.begin() is called by PLC init, so don't call it again to avoid
  // I2C deadlock Just probe for LCD presence on the already-initialized I2C bus

  // CRITICAL FIX: Acquire LCD mutex if available (during boot, mutex may not
  // exist yet)
  SemaphoreHandle_t lcd_mutex = taskGetLcdMutex();
  bool mutex_locked = false;
  if (lcd_mutex != NULL) {
    mutex_locked = taskLockMutex(lcd_mutex, 1000); // 1s timeout for init
    if (!mutex_locked) {
      logWarning("[LCD] Could not acquire mutex for init");
    }
  }

  Wire.beginTransmission(LCD_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    lcd_state.i2c_found = true;

    // Initialize LiquidCrystal_I2C (20x4 display)
    // Standard pin mapping: RS=0, RW=1, E=2, BL=3 (most common PCF8574
    // backpack)
    lcd_i2c = new LiquidCrystal_I2C(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
    if (lcd_i2c) {
      // Extended initialization with delays for reliable startup
      lcd_i2c->init();
      delay(50); // Wait for LCD to stabilize after init
      lcd_i2c->backlight();
      delay(10);
      lcd_i2c->home(); // Move cursor to home position
      lcd_i2c->clear();
      delay(10);

      // Write test pattern to verify LCD is responding
      lcd_i2c->setCursor(0, 0);
      lcd_i2c->print("LCD Init OK");

      lcd_state.mode = LCD_MODE_I2C;
      logInfo("[LCD] [OK] I2C LCD Initialized at 0x%02X (20x4)",
                    LCD_I2C_ADDR);
    } else {
      lcd_state.mode = LCD_MODE_SERIAL;
      logError("[LCD] Failed to allocate LiquidCrystal_I2C, using Serial");
    }
  } else {
    lcd_state.mode = LCD_MODE_SERIAL;
    logWarning("[LCD] I2C Not Found, using Serial simulation");
  }

  // Release mutex if we locked it
  if (mutex_locked) {
    taskUnlockMutex(lcd_mutex);
  }

  // PHASE 4.0: Respect LCD_EN setting
  if (configGetInt(KEY_LCD_EN, 1) == 0) {
    logInfo("[LCD] Disabled via configuration");
    lcdInterfaceSetMode(LCD_MODE_NONE);
    lcdInterfaceBacklight(false);
  } else {
    logInfo("[LCD] [OK] Ready");
  }
}

void lcdInterfaceCleanup() {
  // Clean up allocated LCD resources
  if (lcd_i2c) {
    lcd_i2c->noBacklight();
    lcd_i2c->clear();
    delete lcd_i2c;
    lcd_i2c = nullptr;
  }
  lcd_state.mode = LCD_MODE_NONE;
}

void lcdInterfaceUpdate() {
  uint32_t now = millis();
  if (now - lcd_state.last_update < LCD_REFRESH_INTERVAL_MS)
    return;

  lcd_state.last_update = now;
  lcd_state.update_count++;

  switch (lcd_state.mode) {
  case LCD_MODE_I2C:
    if (lcd_i2c) {
      // CRITICAL FIX: Acquire LCD mutex to prevent I2C bus contention
      // Timeout: 100ms is reasonable for LCD updates (non-critical operation)
      if (taskLockMutex(taskGetLcdMutex(), 100)) {
        // CRITICAL FIX: Protect I2C operations with error detection
        // If I2C fails (Error 263 = hardware not responding), fall back to
        // serial
        bool i2c_error = false;
        for (int i = 0; i < LCD_ROWS && !i2c_error; i++) {
          if (lcd_state.display_dirty[i]) {
            // Check I2C bus health before operation
            Wire.beginTransmission(LCD_I2C_ADDR);
            if (Wire.endTransmission() != 0) {
              // I2C device not responding - log warning and skip this frame
              // Do NOT switch to Serial mode permanently - retry next frame
              static uint32_t last_lcd_err = 0;
              if (millis() - last_lcd_err > 2000) {
                logWarning("[LCD] I2C write failed - skipping frame");
                last_lcd_err = millis();
              }
              i2c_error = true;
              lcd_state.consecutive_i2c_errors++;
              break;
            }

            lcd_i2c->setCursor(0, i);
            // Print with explicit 20-character padding to clear old text
            // Format: %-20s pads with spaces on the right
            char padded_line[LCD_COLS + 1];
            snprintf(padded_line, sizeof(padded_line), "%-20s",
                     lcd_state.display[i]);
            lcd_i2c->print(padded_line);
            lcd_state.display_dirty[i] = false;
          }
        }
        taskUnlockMutex(taskGetLcdMutex());
        
        // Success! Clear error counter
        lcd_state.consecutive_i2c_errors = 0;
      } else {
        // Mutex timeout - skip this update cycle (LCD is non-critical)
        lcd_state.consecutive_i2c_errors++;
        static uint32_t last_log = 0;
        if (millis() - last_log > 5000) {
          logWarning("[LCD] LCD mutex timeout - skipping update");
          last_log = millis();
        }
      }
    }
    break;

  case LCD_MODE_SERIAL: {
    bool any_dirty = false;
    for (int i = 0; i < LCD_ROWS; i++) {
        if (lcd_state.display_dirty[i]) {
            any_dirty = true;
            break;
        }
    }
    
    if (any_dirty) {
        logPrintf("[LCD  ] +--------------------+\r\n");
        for (int i = 0; i < LCD_ROWS; i++) {
            logPrintf("[LCD:%d] |%-20s|\r\n", i, lcd_state.display[i]);
            lcd_state.display_dirty[i] = false;
        }
        logPrintf("[LCD  ] +--------------------+\r\n");
    }
    break;
  }

  case LCD_MODE_NONE:
    break;
  }
}

void lcdInterfacePrintLine(uint8_t line, const char *text) {
  if (line >= LCD_ROWS || text == NULL)
    return;

  // CRITICAL FIX: Only mark line as dirty if content actually changed
  // This prevents serial output spam when LCD is in serial mode
  char new_line[LCD_COLS + 1];
  strncpy(new_line, text, LCD_COLS);
  new_line[LCD_COLS] = '\0';

  // Compare new content with existing content
  if (strcmp(lcd_state.display[line], new_line) != 0) {
    // Content changed - update and mark dirty
    strncpy(lcd_state.display[line], new_line, LCD_COLS);
    lcd_state.display[line][LCD_COLS] = '\0';
    lcd_state.display_dirty[line] = true;
  }
  // else: Content unchanged - don't mark dirty, no serial spam
}

void lcdInterfacePrintAxes(int32_t x_counts, int32_t y_counts, int32_t z_counts,
                           int32_t a_counts) {
  // Line 0: X and Y positions
  // Line 1: Z and A positions with right-justified spindle amps
  // Buffer size increased to 48 to avoid "truncation" warnings regarding null terminators
  // Worst case: "Z " (2) + 11 + "  A" (3) + 11 + " " (1) + 11 = 39 chars + null
  char line1[48];
  char line2[48];

  const float def_lin = (float)MOTION_POSITION_SCALE_FACTOR;
  const float def_ang = (float)MOTION_POSITION_SCALE_FACTOR_DEG;

  float sx =
      (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : def_lin;
  float sy =
      (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : def_lin;
  float sz =
      (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : def_lin;
  float sa = (machineCal.A.pulses_per_degree > 0)
                 ? machineCal.A.pulses_per_degree
                 : def_ang;

  // --- SENSOR CONNECTIVITY CHECK ---
  // Increased buffer sizes to 12 to ensure room for null terminator
  char x_str[12], y_str[12], z_str[12], a_str[12], amps_str[12];
  
  // WJ66 Encoder Status
  bool enc_ok = (wj66GetStatus() == ENCODER_OK);
  
  if (enc_ok) {
    snprintf(x_str, sizeof(x_str), "%7.1f", x_counts / sx);
    snprintf(y_str, sizeof(y_str), "%8.1f", y_counts / sy);
    snprintf(z_str, sizeof(z_str), "%7.1f", z_counts / sz);
    snprintf(a_str, sizeof(a_str), "%4.0f", a_counts / sa);
  } else {
    // Show question marks if disconnected or timeout
    strncpy(x_str, "    ???", sizeof(x_str));
    strncpy(y_str, "     ???", sizeof(y_str));
    strncpy(z_str, "    ???", sizeof(z_str));
    strncpy(a_str, " ???", sizeof(a_str));
  }

  // Spindle Monitor Status
  const spindle_monitor_state_t* spindl = spindleMonitorGetState();
  if (spindl && spindl->enabled && spindl->read_count > 0) {
    snprintf(amps_str, sizeof(amps_str), "%2.0fA", spindl->current_amps);
  } else {
    strncpy(amps_str, "??A", sizeof(amps_str));
  }

  // Line 0: X and Y positions
  snprintf(line1, sizeof(line1), "X %s  Y%s", x_str, y_str);

  // Line 1: Z and A positions with right-justified spindle amps
  snprintf(line2, sizeof(line2), "Z %s  A%s %s", z_str, a_str, amps_str);

  lcdInterfacePrintLine(0, line1);
  lcdInterfacePrintLine(1, line2);
}

void lcdInterfaceSetMode(lcd_mode_t mode) {
  if (mode == LCD_MODE_I2C && !lcd_state.i2c_found) {
    logError("[LCD] I2C Hardware not present");
    return;
  }
  lcd_state.mode = mode;
  logInfo("[LCD] Mode set to %d", mode);
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
  logInfo("[LCD] [OK] Cleared");
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
  logInfo("[LCD] Backlight: %s", on ? "ON" : "OFF");
}

void lcdInterfaceResetErrors() {
  lcd_state.consecutive_i2c_errors = 0;
  if (lcd_state.i2c_found) {
    lcd_state.mode = LCD_MODE_I2C;
    logInfo("[LCD] Manually reset to I2C Mode");
  } else {
    logWarning("[LCD] Cannot reset to I2C - Hardware not found");
  }
}

void lcdInterfaceGetContent(char content[LCD_ROWS][LCD_COLS + 1]) {
  for (int i = 0; i < LCD_ROWS; i++) {
    strncpy(content[i], lcd_state.display[i], LCD_COLS);
    content[i][LCD_COLS] = '\0';
  }
}

void lcdInterfaceDiagnostics() {
  logPrintln("\r\n[LCD] === Diagnostics ===");
  logPrintf("Mode: %d\r\nI2C Found: %s\r\nBacklight: %s\r\nUpdates: %lu\r\nI2C Errors: %u\r\n",
                lcd_state.mode, lcd_state.i2c_found ? "YES" : "NO",
                lcd_state.backlight_on ? "ON" : "OFF",
                (unsigned long)lcd_state.update_count,
                lcd_state.consecutive_i2c_errors);

  for (int i = 0; i < LCD_ROWS; i++) {
    logPrintf("  [%d] %s\r\n", i, lcd_state.display[i]);
  }
}

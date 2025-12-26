#include "board_inputs.h"
#include "config_keys.h"
#include "config_unified.h"
#include "fault_logging.h"
#include "hardware_config.h" // HAL for dynamic pin mapping
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "task_manager.h" // THREAD SAFETY FIX: For I2C board mutex
#include <Wire.h>

static uint8_t input_cache = 0xFF;
static bool device_present = false; // Track if device was detected at init
static bool buttons_enabled =
    true; // Config flag to enable/disable button polling

// --- FAST PATH CACHE ---
// Bitmasks are resolved during init to avoid expensive lookups
// inside the high-frequency (5ms) safety loop.
// --- STABILITY FILTER (DEBOUNCE) ---
// To filter electrical noise, an input must remain stable for 
// DEBOUNCE_STABILITY_REQUIRED consecutive polls (5ms each = 15ms total)
#define DEBOUNCE_STABILITY_REQUIRED 3
static uint8_t debounced_input_cache = 0xFF;
static uint8_t input_stability_count[8] = {0};
static uint8_t last_raw_input = 0xFF;

static uint8_t mask_estop = 0;
static uint8_t mask_pause = 0;
static uint8_t mask_resume = 0;

void boardInputsInit() {
  logInfo("[INPUTS] Initializing...");

  // Check if buttons are enabled in config (default: DISABLED for testing)
  buttons_enabled = configGetInt(KEY_BUTTONS_ENABLED, 0) != 0;
  if (!buttons_enabled) {
    logInfo("[INPUTS] Physical buttons DISABLED (btn_en=0 or not set)");
    device_present = false;
    return;
  }

  // 1. Resolve Pin Mappings via HAL
  // The HAL handles NVS overrides or falls back to hardware defaults.
  int8_t pin_estop = getPin("input_estop");
  int8_t pin_pause = getPin("input_pause");
  int8_t pin_resume = getPin("input_resume");

  // 2. Validate and Generate Bitmasks
  // The KC868-A16 Input Expander (0x24) handles Virtual Pins 0-7 (X1-X8).
  // We enforce bounds checking to prevent invalid shifts.

  if (pin_estop >= 0 && pin_estop < 8) {
    mask_estop = (1 << pin_estop);
  } else {
    logError("[INPUTS] Invalid E-Stop Pin %d. Defaulting to P3 (X4).",
             pin_estop);
    mask_estop = (1 << 3); // Fallback: P3 (X4)
  }

  if (pin_pause >= 0 && pin_pause < 8) {
    mask_pause = (1 << pin_pause);
  } else {
    mask_pause = (1 << 4); // Fallback: P4 (X5)
  }

  if (pin_resume >= 0 && pin_resume < 8) {
    mask_resume = (1 << pin_resume);
  } else {
    mask_resume = (1 << 5); // Fallback: P5 (X6)
  }

  logInfo(
      "[INPUTS] Fast Path Mapped: Estop=0x%02X, Pause=0x%02X, Resume=0x%02X",
      mask_estop, mask_pause, mask_resume);

  // 3. Hardware Bus Init
  // THREAD SAFETY FIX: Protect I2C bus access with mutex
  uint8_t temp_data = 0xFF;
  i2c_result_t result = I2C_RESULT_UNKNOWN_ERROR;
  SemaphoreHandle_t i2c_mutex = taskGetI2cBoardMutex();

  if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
    result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &temp_data, 1);
    xSemaphoreGive(i2c_mutex);
  } else {
    logWarning(
        "[INPUTS] I2C mutex not available during init (expected at boot)");
    // During init, mutex might not be created yet - safe to call directly
    result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &temp_data, 1);
  }

  if (result == I2C_RESULT_OK) {
    input_cache = temp_data;
    device_present = true;
    logInfo("[INPUTS] Board detected (0x%02X)", BOARD_INPUT_I2C_ADDR);
  } else {
    device_present = false;
    logWarning("[INPUTS] Board NOT connected (0x%02X) - buttons disabled",
               BOARD_INPUT_I2C_ADDR);
    // Don't log as error - device may be intentionally disconnected
  }
}

button_state_t boardInputsUpdate() {
  button_state_t state = {false, false, false, false};
  static uint32_t invalid_until = 0;

  // Skip polling if buttons are disabled via config
  if (!buttons_enabled) {
    state.connection_ok = true; // Not a connection error, just disabled
    state.estop_active = false; // Safe default: E-Stop not active
    return state;
  }

  // Skip polling entirely if device wasn't detected at init
  // This prevents continuous I2C errors when buttons aren't connected
  if (!device_present) {
    state.connection_ok = false;
    state.estop_active = false; // Safe default: E-Stop not active
    return state;
  }

  // Fast fail backoff to prevent bus hammering on persistent faults
  if (millis() < invalid_until) {
    state.connection_ok = false;
    // Return potentially stale but safe state
    state.estop_active = (input_cache & mask_estop);
    return state;
  }

  // THREAD SAFETY FIX: Protect I2C bus access with mutex
  // Multiple tasks may call this function concurrently (safety task, telemetry,
  // etc.)
  SemaphoreHandle_t i2c_mutex = taskGetI2cBoardMutex();
  i2c_result_t result = I2C_RESULT_UNKNOWN_ERROR;

  if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10))) {
    result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    xSemaphoreGive(i2c_mutex);
  } else {
    // Mutex timeout - bus busy or deadlock
    state.connection_ok = false;
    return state;
  }

  if (result != I2C_RESULT_OK) {
    state.connection_ok = false;
    // Backoff for 500ms if I2C fails implies bus issues
    invalid_until = millis() + 500;
    return state;
  }
  state.connection_ok = true;

  // --- STABILITY FILTER (DEBOUNCE) ---
  // Iterate through all 8 bits of the input byte
  for (int i = 0; i < 8; i++) {
    uint8_t bit_mask = (1 << i);
    bool current_bit_raw = (input_cache & bit_mask);
    bool last_bit_raw = (last_raw_input & bit_mask);
    bool current_bit_debounced = (debounced_input_cache & bit_mask);

    if (current_bit_raw != current_bit_debounced) {
      if (current_bit_raw == last_bit_raw) {
        // Input is stable but different from debounced state
        input_stability_count[i]++;
        if (input_stability_count[i] >= DEBOUNCE_STABILITY_REQUIRED) {
          // Commit change to debounced cache
          if (current_bit_raw) debounced_input_cache |= bit_mask;
          else debounced_input_cache &= ~bit_mask;
          input_stability_count[i] = 0;
        }
      } else {
        // Input is jittering, reset counter
        input_stability_count[i] = 0;
      }
    } else {
      // Input matches debounced state, reset counter
      input_stability_count[i] = 0;
    }
  }
  last_raw_input = input_cache;

  // --- FAST PATH EXECUTION ---
  // Apply cached masks to the DEBOUNCED cache.
  // Logic:
  // E-STOP: NC (Normally Closed). Active = High (Open Circuit/Pressed)
  // Buttons: NO (Normally Open). Active = Low (Short to Ground)

  state.estop_active = (debounced_input_cache & mask_estop);
  state.pause_pressed = !(debounced_input_cache & mask_pause);
  state.resume_pressed = !(debounced_input_cache & mask_resume);

  return state;
}

void boardInputsDiagnostics() {
  serialLoggerLock();
  Serial.println("\n[INPUTS] === Physical Inputs (0x24) ===");

  // THREAD SAFETY FIX: Protect I2C bus access with mutex
  SemaphoreHandle_t i2c_mutex = taskGetI2cBoardMutex();

  if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10))) {
    i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    xSemaphoreGive(i2c_mutex);
  } else {
    Serial.println("[INPUTS] [ERR] Mutex timeout - I2C bus busy");
    serialLoggerUnlock();
    return;
  }

  Serial.printf("Raw Byte: 0x%02X | Debounced: 0x%02X\n", input_cache, debounced_input_cache);

  // Decode using current maps for diagnostics
  bool estop = (debounced_input_cache & mask_estop);
  bool pause = !(debounced_input_cache & mask_pause);
  bool resume = !(debounced_input_cache & mask_resume);

  Serial.printf("  E-STOP (Mask 0x%02X): %s\n", mask_estop,
                estop ? "TRIPPED (OPEN)" : "OK (CLOSED)");
  Serial.printf("  PAUSE  (Mask 0x%02X): %s\n", mask_pause,
                pause ? "PRESSED" : "RELEASED");
  Serial.printf("  RESUME (Mask 0x%02X): %s\n", mask_resume,
                resume ? "PRESSED" : "RELEASED");
  serialLoggerUnlock();
}
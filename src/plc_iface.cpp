/**
 * @file plc_iface.cpp
 * @brief Hardware Abstraction Layer for ELBO PLC (PosiPro)
 * @details Implements robust I2C drivers with error checking.
 *          CRITICAL FIX: Added MUTEX protection for shadow register access
 *          to prevent race conditions when multiple tasks modify the relay
 * states.
 *
 *          ARCHITECTURE: Spinlock vs Mutex Decision (Code Audit Compliant)
 *          - Shadow registers: Protected by MUTEX (not spinlock)
 *          - I2C operations: NEVER called inside mutex (copy-before-release
 * pattern)
 *          - Why mutex, not spinlock:
 *            1. Shadow registers accessed from tasks, not ISRs
 *            2. Mutexes allow proper task scheduling (no interrupt disable)
 *            3. I2C (milliseconds) must NEVER be in critical section
 *
 *          Pattern: Lock mutex → Modify shadow → Copy → Release mutex → I2C
 * call Result: I2C operations happen OUTSIDE mutex protection ✓
 */

#include "plc_iface.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "task_manager.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>

// Shadow Registers
static uint8_t i73_input_shadow = 0x00; // Default 0 (Inactive) to prevent phantom inputs on vanilla ESP32
static uint8_t q73_shadow_register = 0xFF; // All OFF (active-low: 1=OFF, 0=ON)

// CRITICAL FIX: Mutex to protect shadow register access
// Multiple tasks can call elboSetDirection(), elboSetSpeedProfile(),
// elboQ73SetRelay() Without protection, race conditions can corrupt relay state
// NOTE: Using Mutex instead of Spinlock because:
//   1. Shadow registers are only accessed from tasks, not ISRs
//   2. Mutexes allow proper task scheduling instead of disabling interrupts
//   3. More efficient for multi-task synchronization
static SemaphoreHandle_t plc_shadow_mutex = NULL;

// PHASE 5.7: Fix - Shadow Register Dirty Flag (Mutex Timeout Handling)
// If mutex timeout occurs, shadow register is NOT updated but hardware might be
// fine Dirty flag tracks when shadow register is out of sync with hardware Next
// successful I2C write will re-sync by writing the full shadow register
static bool q73_shadow_dirty = false;
static uint32_t q73_mutex_timeout_count = 0;

// Hardware presence flag - set at boot, checked by monitor tasks
static bool g_plc_hardware_present = true;  // Optimistic default

#define I2C_RETRIES 3
#define SHADOW_MUTEX_TIMEOUT_MS 100
#define SHADOW_MUTEX_RETRIES 3

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * @brief Safely acquire shadow register mutex with retry logic (Fix)
 * @details Implements retry mechanism to prevent shadow register desync
 *          If all retries fail, sets dirty flag for later recovery
 * @return true if mutex acquired, false if all retries failed
 */
static bool plcAcquireShadowMutex() {
  for (int retry = 0; retry < SHADOW_MUTEX_RETRIES; retry++) {
    if (xSemaphoreTake(plc_shadow_mutex,
                       pdMS_TO_TICKS(SHADOW_MUTEX_TIMEOUT_MS)) == pdTRUE) {
      return true; // Success
    }

    // Retry after brief delay
    if (retry < SHADOW_MUTEX_RETRIES - 1) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  // All retries failed - this is serious
  q73_shadow_dirty = true;
  q73_mutex_timeout_count++;

  logError(
      "[PLC] [CRITICAL] Shadow mutex timeout after %d retries (count: %lu)",
      SHADOW_MUTEX_RETRIES, (unsigned long)q73_mutex_timeout_count);

  return false;
}

// ============================================================================
// INTERNAL I2C HELPER
// ============================================================================

#include "i2c_bus_recovery.h"

// ... (existing helper function plcAcquireShadowMutex) ...

static bool plcWriteI2C(uint8_t address, uint8_t data, const char *context) {
  // CRITICAL FIX: Acquire PLC I2C mutex to prevent bus contention
  // Timeout: 200ms (PLC operations are time-sensitive but not critical)
  if (!taskLockMutex(taskGetI2cPlcMutex(), 200)) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 2000) {
      logWarning("[PLC] PLC I2C mutex timeout - skipping write: %s", context);
      last_log = millis();
    }
    return false;
  }

  uint8_t buffer = data;
  i2c_result_t res = i2cWriteWithRetry(address, &buffer, 1);

  taskUnlockMutex(taskGetI2cPlcMutex());

  if (res == I2C_RESULT_OK) {
    // PHASE 5.7: Fix - Clear dirty flag on successful I2C write
    // Shadow register is now in sync with hardware
    if (address == ADDR_Q73_OUTPUT) {
      q73_shadow_dirty = false;
    }
    return true;
  }

  logError("[PLC] I2C Write Failed (Addr 0x%02X, Err %d): %s", address, res,
           context);
  faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, address, context);

  // PHASE 5.7: I2C write failed - shadow register might be out of sync
  if (address == ADDR_Q73_OUTPUT) {
    q73_shadow_dirty = true;
  }

  return false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void elboInit() {
  logInfo("[PLC] Initializing I2C Bus...");

  // Create mutex for shadow register protection
  plc_shadow_mutex = xSemaphoreCreateMutex();
  if (plc_shadow_mutex == NULL) {
    logError("[PLC] [CRITICAL] Failed to create shadow register mutex!");
  }

  // Initialize Wire I2C bus (only called once at startup)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  Wire.setTimeOut(100); // Reduce I2C timeout from 1000ms to 100ms
  delay(10);            // Allow bus to settle

  // Reset Outputs (Safe State: All OFF)
  // KC868-A16 uses active-low relay outputs: 0xFF = all OFF, 0x00 = all ON
  q73_shadow_register = 0xFF;

  // ROBUSTNESS FIX: Retry board detection with delay to handle slow power-on
  bool board_detected = false;
  for (int i = 0; i < 3; i++) {
      if (plcWriteI2C(ADDR_Q73_OUTPUT, q73_shadow_register, "Init Q73")) {
          logInfo("[PLC] Q73 Output Board OK (Addr 0x%02X)", ADDR_Q73_OUTPUT);
          board_detected = true;
          break;
      }
      faultLogWarning(FAULT_PLC_COMM_LOSS, "Q73 Board detection retry");
      logWarning("[PLC] Q73 Board detection attempt %d/3 failed, retrying...", i + 1);
      delay(50); // Give hardware time to settle
  }

  if (board_detected) {
    g_plc_hardware_present = true;
  } else {
    logError("[PLC] [CRITICAL] Q73 Output Board Missing!");
    g_plc_hardware_present = false;  // Mark hardware as not present
  }
}

// ============================================================================
// OUTPUT CONTROL - NEW API (Matches Actual Hardware Wiring)
// ============================================================================

/**
 * @brief Set which axis is selected for motion
 * @param axis 0=X (Y1), 1=Y (Y2), 2=Z (Y3), 255=none
 */
void plcSetAxisSelect(uint8_t axis) {
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcSetAxisSelect FAILED (shadow register dirty)");
    return;
  }

  // Clear all axis select bits first (bits 0-2)
  q73_shadow_register |= ((1 << PLC_OUT_AXIS_X_SELECT) |
                          (1 << PLC_OUT_AXIS_Y_SELECT) |
                          (1 << PLC_OUT_AXIS_Z_SELECT));

  // Set the selected axis (active-low: clear bit = ON)
  if (axis == 0) {
    q73_shadow_register &= ~(1 << PLC_OUT_AXIS_X_SELECT);
  } else if (axis == 1) {
    q73_shadow_register &= ~(1 << PLC_OUT_AXIS_Y_SELECT);
  } else if (axis == 2) {
    q73_shadow_register &= ~(1 << PLC_OUT_AXIS_Z_SELECT);
  }
  // axis == 255 means no axis selected (all OFF)

  uint8_t register_copy = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Axis");
}

/**
 * @brief Set movement direction
 * @param positive true=Y4 (forward/+), false=Y5 (reverse/-)
 */
void plcSetDirection(bool positive) {
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcSetDirection FAILED (shadow register dirty)");
    return;
  }

  // Clear both direction bits first (bits 3-4)
  q73_shadow_register |= ((1 << PLC_OUT_DIR_POSITIVE) |
                          (1 << PLC_OUT_DIR_NEGATIVE));

  // Set the selected direction (active-low: clear bit = ON)
  if (positive) {
    q73_shadow_register &= ~(1 << PLC_OUT_DIR_POSITIVE);
  } else {
    q73_shadow_register &= ~(1 << PLC_OUT_DIR_NEGATIVE);
  }

  uint8_t register_copy = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Direction");
}

/**
 * @brief Set speed profile
 * @param speed_profile 0=slow (Y8), 1=medium (Y7), 2=fast (Y6)
 * @note Mapping: SPEED_PROFILE_1(0)=slowest, SPEED_PROFILE_3(2)=fastest
 *       Hardware: Y6=FAST, Y7=MEDIUM, Y8=SLOW
 *       So we invert: profile 0→SLOW(Y8), profile 2→FAST(Y6)
 */
void plcSetSpeed(uint8_t speed_profile) {
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcSetSpeed FAILED (shadow register dirty)");
    return;
  }

  // Clear all speed bits first (bits 5-7)
  q73_shadow_register |= ((1 << PLC_OUT_SPEED_FAST) |
                          (1 << PLC_OUT_SPEED_MEDIUM) |
                          (1 << PLC_OUT_SPEED_SLOW));

  // CRITICAL FIX: Invert mapping to match semantic meaning
  // Profile 0 = slowest speed request → Y8 (SLOW hardware)
  // Profile 1 = medium speed request → Y7 (MEDIUM hardware)
  // Profile 2 = fastest speed request → Y6 (FAST hardware)
  switch (speed_profile) {
    case 0:  // SPEED_PROFILE_1 = slowest
      q73_shadow_register &= ~(1 << PLC_OUT_SPEED_SLOW);  // Y8
      break;
    case 1:  // SPEED_PROFILE_2 = medium
      q73_shadow_register &= ~(1 << PLC_OUT_SPEED_MEDIUM);  // Y7
      break;
    case 2:  // SPEED_PROFILE_3 = fastest
      q73_shadow_register &= ~(1 << PLC_OUT_SPEED_FAST);  // Y6
      break;
    default:
      break;
  }

  uint8_t register_copy = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Speed");
}

/**
 * @brief Clear all outputs (safe stop)
 */
void plcClearAllOutputs() {
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcClearAllOutputs FAILED (shadow register dirty)");
    return;
  }

  q73_shadow_register = 0xFF; // All OFF (active-low)

  uint8_t register_copy = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Clear All");
}

/**
 * @brief Force write shadow register to hardware (for recovery)
 */
void plcCommitOutputs() {
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcCommitOutputs FAILED (shadow register dirty)");
    return;
  }

  uint8_t register_copy = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Commit");
}

// ============================================================================
// LEGACY API (Redirects to new API for backward compatibility)
// ============================================================================

void elboSetDirection(uint8_t axis, bool forward) {
  // Legacy function - now sets BOTH axis and direction
  // This maintains old API behavior
  plcSetAxisSelect(axis);
  plcSetDirection(forward);
}

void elboSetSpeedProfile(uint8_t profile_index) {
  // Redirect to new API
  plcSetSpeed(profile_index);
}

// PHASE 3.1: Added getter to read current speed profile
// Allows LCD and diagnostics to display active speed profile
uint8_t elboGetSpeedProfile() {
  // Read speed profile bits (5, 6, 7) from shadow register
  if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    logWarning("[PLC] Failed to acquire shadow mutex for GetSpeedProfile");
    return 0xFF;
  }

  uint8_t reg = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  // Active-low: bit cleared = speed active
  // Check in order: fast (5), medium (6), slow (7)
  if (!(reg & (1 << PLC_OUT_SPEED_FAST)))
    return 0; // Fast
  if (!(reg & (1 << PLC_OUT_SPEED_MEDIUM)))
    return 1; // Medium
  if (!(reg & (1 << PLC_OUT_SPEED_SLOW)))
    return 2; // Slow

  return 0xFF; // No speed set
}

void elboQ73SetRelay(uint8_t relay_bit, bool state) {
  if (relay_bit > 7)
    return;

  // PHASE 5.7: Fix - Use retry helper instead of simple timeout
  // CRITICAL BUG FIX: Previous code returned early on mutex timeout,
  // leaving shadow register out of sync with hardware
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] SetRelay FAILED for bit %d (shadow register dirty)",
             relay_bit);
    return;
  }

  // Active-low: 0 = relay ON, 1 = relay OFF
  if (state) {
    q73_shadow_register &= ~(1 << relay_bit); // Clear bit = ON
  } else {
    q73_shadow_register |= (1 << relay_bit); // Set bit = OFF
  }

  // Make a copy for I2C write before releasing mutex
  uint8_t register_copy = q73_shadow_register;

  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Relay");
}

// ============================================================================
// INPUT READING
// ============================================================================

bool elboI73GetInput(uint8_t bit, bool *success) {
  // CRITICAL FIX: Acquire PLC I2C mutex to prevent bus contention
  // Timeout: 200ms (PLC operations are time-sensitive but not critical)
  if (!taskLockMutex(taskGetI2cPlcMutex(), 200)) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 2000) {
      logWarning("[PLC] PLC I2C mutex timeout - using cached input");
      last_log = millis();
    }
    if (success)
      *success = false;
    return (i73_input_shadow & (1 << bit));
  }

  uint8_t count = Wire.requestFrom((uint8_t)ADDR_I73_INPUT, (uint8_t)1);

  if (count == 1) {
    i73_input_shadow = Wire.read();
    if (success)
      *success = true;
  } else {
    if (success)
      *success = false;

    // Throttled logging
    static uint32_t last_log = 0;
    if (millis() - last_log > 2000) {
      logError("[PLC] I2C Read Failed (I73)");
      last_log = millis();
    }
  }

  taskUnlockMutex(taskGetI2cPlcMutex());

  // Check bit state (Returns cached value on failure)
  return (i73_input_shadow & (1 << bit));
}

void elboDiagnostics() {
  logPrintln("\n[PLC] === IO Diagnostics ===");

  // Read shadow register safely with mutex protection
  uint8_t output_reg = 0x00;
  if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    output_reg = q73_shadow_register;
    xSemaphoreGive(plc_shadow_mutex);
  } else {
    logWarning("[PLC] Could not acquire shadow mutex for diagnostics");
  }

  logPrintf("Output Register: 0x%02X\n", output_reg);
  logPrintf("Input Register:  0x%02X\n", i73_input_shadow);

  // PHASE 5.7: Fix - Display shadow register health
  logPrintf("Shadow Register Dirty: %s\n",
                q73_shadow_dirty ? "YES (OUT OF SYNC!)" : "No");
  logPrintf("Mutex Timeout Count: %lu\n",
                (unsigned long)q73_mutex_timeout_count);

  // CRITICAL FIX: Acquire PLC I2C mutex for diagnostics
  if (taskLockMutex(taskGetI2cPlcMutex(), 500)) {
    Wire.beginTransmission(ADDR_Q73_OUTPUT);
    uint8_t err = Wire.endTransmission();
    logPrintf("Q73 (0x%02X) Status: %s\n", ADDR_Q73_OUTPUT,
                  (err == 0) ? "OK" : "ERROR");
    taskUnlockMutex(taskGetI2cPlcMutex());
  } else {
    logWarning("[PLC] Q73: Could not acquire I2C mutex for diagnostics");
  }
}

// PHASE 5.7: Fix - Shadow Register Health Monitoring
uint32_t elboGetMutexTimeoutCount() { return q73_mutex_timeout_count; }

bool elboIsShadowRegisterDirty() { return q73_shadow_dirty; }

// Hardware presence check - allows monitor tasks to skip I2C when no hardware
bool plcIsHardwarePresent() { return g_plc_hardware_present; }

uint8_t elboI73GetRawState() { return i73_input_shadow; }

uint8_t elboQ73GetRawState() { return q73_shadow_register; }

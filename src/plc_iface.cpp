/**
 * @file plc_iface.cpp
 * @brief Hardware Abstraction Layer for ELBO PLC (Gemini v3.5.21)
 * @details Implements robust I2C drivers with error checking.
 *          CRITICAL FIX: Added MUTEX protection for shadow register access
 *          to prevent race conditions when multiple tasks modify the relay
 * states.
 *
 *          ARCHITECTURE: Spinlock vs Mutex Decision (Gemini Audit Compliant)
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
static uint8_t i73_input_shadow = 0xFF;
static uint8_t q73_shadow_register = 0xFF; // All OFF (active-low: 1=OFF, 0=ON)

// CRITICAL FIX: Mutex to protect shadow register access
// Multiple tasks can call elboSetDirection(), elboSetSpeedProfile(),
// elboQ73SetRelay() Without protection, race conditions can corrupt relay state
// NOTE: Using Mutex instead of Spinlock because:
//   1. Shadow registers are only accessed from tasks, not ISRs
//   2. Mutexes allow proper task scheduling instead of disabling interrupts
//   3. More efficient for multi-task synchronization
static SemaphoreHandle_t plc_shadow_mutex = NULL;

// PHASE 5.7: Gemini Fix - Shadow Register Dirty Flag (Mutex Timeout Handling)
// If mutex timeout occurs, shadow register is NOT updated but hardware might be
// fine Dirty flag tracks when shadow register is out of sync with hardware Next
// successful I2C write will re-sync by writing the full shadow register
static bool q73_shadow_dirty = false;
static uint32_t q73_mutex_timeout_count = 0;

#define I2C_RETRIES 3
#define SHADOW_MUTEX_TIMEOUT_MS 100
#define SHADOW_MUTEX_RETRIES 3

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * @brief Safely acquire shadow register mutex with retry logic (Gemini Fix)
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
    // PHASE 5.7: Gemini Fix - Clear dirty flag on successful I2C write
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

  if (plcWriteI2C(ADDR_Q73_OUTPUT, q73_shadow_register, "Init Q73")) {
    logInfo("[PLC] Q73 Output Board OK (Addr 0x%02X)", ADDR_Q73_OUTPUT);
  } else {
    logError("[PLC] [CRITICAL] Q73 Output Board Missing!");
  }
}

// ============================================================================
// OUTPUT CONTROL
// ============================================================================

void elboSetDirection(uint8_t axis, bool forward) {
  if (axis >= 4)
    return;

  // PHASE 5.7: Gemini Fix - Use retry helper instead of simple timeout
  // Prevents shadow register desynchronization on mutex timeout
  if (!plcAcquireShadowMutex()) {
    // All retries failed - critical error
    // Dirty flag is set by helper, will be recovered on next successful write
    logError("[PLC] SetDirection FAILED for axis %d (shadow register dirty)",
             axis);
    return;
  }

  // Use standard shift. Relays 0-3 are X,Y,Z,A Directions
  // Active-low: 0 = relay ON, 1 = relay OFF
  uint8_t mask = (1 << axis);

  if (forward) {
    q73_shadow_register &= ~mask; // Clear bit = ON (active-low)
  } else {
    q73_shadow_register |= mask; // Set bit = OFF (active-low)
  }

  // Make a copy for I2C write before releasing mutex
  uint8_t register_copy = q73_shadow_register;

  xSemaphoreGive(plc_shadow_mutex);

  // Do NOT modify Enable bit here.
  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Direction");
}

void elboSetSpeedProfile(uint8_t profile_index) {
  // PHASE 5.7: Gemini Fix - Use retry helper instead of simple timeout
  if (!plcAcquireShadowMutex()) {
    logError(
        "[PLC] SetSpeedProfile FAILED for profile %d (shadow register dirty)",
        profile_index);
    return;
  }

  // Clear Speed bits (4, 5, 6) = all speed relays OFF (active-low: set bits)
  q73_shadow_register |= ((1 << ELBO_Q73_SPEED_1) | (1 << ELBO_Q73_SPEED_2) |
                          (1 << ELBO_Q73_SPEED_3));

  // Active-low: clear the bit to turn ON the selected speed relay
  switch (profile_index) {
  case 0:
    q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_1);
    break;
  case 1:
    q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_2);
    break;
  case 2:
    q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_3);
    break;
  default:
    break;
  }

  // Make a copy for I2C write before releasing mutex
  uint8_t register_copy = q73_shadow_register;

  xSemaphoreGive(plc_shadow_mutex);

  plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Speed");
}

// PHASE 3.1: Added getter to read current speed profile
// Allows LCD and diagnostics to display active speed profile
uint8_t elboGetSpeedProfile() {
  // Read speed profile bits (4, 5, 6) from shadow register
  // FIXED: Now using mutex for thread-safe read operation
  if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    logWarning("[PLC] Failed to acquire shadow mutex for GetSpeedProfile");
    return 0xFF; // Return error on mutex timeout
  }

  uint8_t speed_bits = (q73_shadow_register >> ELBO_Q73_SPEED_1) & 0x07;

  xSemaphoreGive(plc_shadow_mutex);

  // Decode speed bits to profile index
  if (speed_bits & (1 << (ELBO_Q73_SPEED_1 - ELBO_Q73_SPEED_1)))
    return 0; // Profile 0
  if (speed_bits & (1 << (ELBO_Q73_SPEED_2 - ELBO_Q73_SPEED_1)))
    return 1; // Profile 1
  if (speed_bits & (1 << (ELBO_Q73_SPEED_3 - ELBO_Q73_SPEED_1)))
    return 2; // Profile 2

  return 0xFF; // No profile set or error
}

void elboQ73SetRelay(uint8_t relay_bit, bool state) {
  if (relay_bit > 7)
    return;

  // PHASE 5.7: Gemini Fix - Use retry helper instead of simple timeout
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

  // PHASE 5.7: Gemini Fix - Display shadow register health
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

// PHASE 5.7: Gemini Fix - Shadow Register Health Monitoring
uint32_t elboGetMutexTimeoutCount() { return q73_mutex_timeout_count; }

bool elboIsShadowRegisterDirty() { return q73_shadow_dirty; }
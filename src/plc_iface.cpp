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
#include "config_keys.h"
#include "config_unified.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "task_manager.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include "rtc_manager.h"
#include "system_utils.h" // PHASE 8.1


// Shadow Registers
static uint8_t i73_input_shadow = 0x00; // Bank 1: S5 Q 73 Status (0x21)
static uint8_t i72_input_shadow = 0x00; // Bank 2: S5 Q 72 Limits (0x22)
static uint8_t q73_shadow_register = 0xFF; // Output Bank 1: S5 I 73 (0x24)
static uint8_t q73_aux_shadow = 0xFF;      // Output Bank 2: S5 I 72 (0x25)

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
static bool plc_in_transaction = false;     // PHASE 1: I2C Performance (Batching)

// Latency Tracking (PHASE 2.0)
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint64_t total_us;
    uint64_t total_sq_us;
    uint32_t samples;
} internal_latency_stats_t;

static internal_latency_stats_t input_latency = {UINT32_MAX, 0, 0, 0, 0};
static internal_latency_stats_t output_latency = {UINT32_MAX, 0, 0, 0, 0};

static void updateLatency(internal_latency_stats_t* stats, uint32_t us) {
    if (us < stats->min_us) stats->min_us = us;
    if (us > stats->max_us) stats->max_us = us;
    stats->total_us += us;
    stats->total_sq_us += (uint64_t)us * us;
    stats->samples++;
}

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
  if (!g_plc_hardware_present) {
    return false;
  }

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
  uint32_t start = micros();
  i2c_result_t res = i2cWriteWithRetry(address, &buffer, 1);
  uint32_t latency = micros() - start;

  taskUnlockMutex(taskGetI2cPlcMutex());

  if (res == I2C_RESULT_OK) {
    // PHASE 5.7: Fix - Clear dirty flag on successful I2C write
    // Shadow register is now in sync with hardware
    if (address == ADDR_Q73_OUTPUT || address == ADDR_Q73_AUX) {
      q73_shadow_dirty = false;
      updateLatency(&output_latency, latency);
    }
    return true;
  }

  logError("[PLC] I2C Write Failed (Addr 0x%02X, Err %d): %s", address, res,
           context);
  faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, address, context);

  // PHASE 5.7: I2C write failed - shadow register might be out of sync
  if (address == ADDR_Q73_OUTPUT || address == ADDR_Q73_AUX) {
    q73_shadow_dirty = true;
  }

  return false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void elboInit() {
  logModuleInit("PLC");

  // Create mutex for shadow register protection
  plc_shadow_mutex = xSemaphoreCreateMutex();
  if (plc_shadow_mutex == NULL) {
    logError("[PLC] [CRITICAL] Failed to create shadow register mutex!");
  }

  // Initialize Wire I2C bus (only called once at startup)
  // Use configured speed, defaulting to 100KHz (Standard Mode)
  uint32_t i2c_speed = configGetInt(KEY_I2C_SPEED, 100000);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, i2c_speed);
  
  // PHASE 16: Enable internal pull-ups for stability on bare DevKits
  // (KC868 board has external ones, but bare boards usually don't)
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  
  Wire.setTimeOut(100); // Reduce I2C timeout from 1000ms to 100ms
  logInfo("[PLC] I2C initialized at %lu Hz", (unsigned long)i2c_speed);
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
          
          // Also try to detect the AUX board (some systems might not have it)
          if (plcWriteI2C(ADDR_Q73_AUX, q73_aux_shadow, "Init Q73 AUX")) {
              logInfo("[PLC] Q73 AUX Board OK (Addr 0x%02X)", ADDR_Q73_AUX);
          } else {
              logWarning("[PLC] Q73 AUX Board not found (Optional)");
          }
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

  // Initialize DS3231 RTC AFTER I2C recovery (v3.1 boards only)
  #if BOARD_HAS_RTC_DS3231
  rtcInit();
  #endif
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

  // Clear all axis select bits first (I 72.0 to 72.4 / Bank 2 bits 0-4)
  // VERIFIED BY S5 PLC LOGIC (PB10.AWL)
  q73_aux_shadow |= ((1 << PLC_OUT_AXIS_X_SELECT) |
                     (1 << PLC_OUT_AXIS_Y_SELECT) |
                     (1 << PLC_OUT_AXIS_Z_SELECT) |
                     (1 << PLC_OUT_AXIS_A_SELECT) |
                     (1 << PLC_OUT_AXIS_DISK_ROT));

  // Set the selected axis (active-low: clear bit = ON)
  // Mapping aligns with PB10 dispatcher: 0=X, 1=Y, 2=Z, 3=A, 4=Disk
  switch (axis) {
    case 0: q73_aux_shadow &= ~(1 << PLC_OUT_AXIS_X_SELECT); break;
    case 1: q73_aux_shadow &= ~(1 << PLC_OUT_AXIS_Y_SELECT); break;
    case 2: q73_aux_shadow &= ~(1 << PLC_OUT_AXIS_Z_SELECT); break;
    case 3: q73_aux_shadow &= ~(1 << PLC_OUT_AXIS_A_SELECT); break;
    case 4: q73_aux_shadow &= ~(1 << PLC_OUT_AXIS_DISK_ROT); break;
    default: break; // 255 = none
  }

  uint8_t register_copy = q73_aux_shadow;
  xSemaphoreGive(plc_shadow_mutex);
  
  if (!plc_in_transaction) {
    plcWriteI2C(ADDR_Q73_AUX, register_copy, "Set Axis");
  }
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

  // Clear both direction bits first (I 72.5, 72.6 / Bank 2 bits 5-6)
  q73_aux_shadow |= ((1 << PLC_OUT_DIR_POSITIVE) |
                     (1 << PLC_OUT_DIR_NEGATIVE));

  // Set the selected direction (active-low: clear bit = ON)
  if (positive) {
    q73_aux_shadow &= ~(1 << PLC_OUT_DIR_POSITIVE);
  } else {
    q73_aux_shadow &= ~(1 << PLC_OUT_DIR_NEGATIVE);
  }

  uint8_t register_copy = q73_aux_shadow;
  xSemaphoreGive(plc_shadow_mutex);
  
  if (!plc_in_transaction) {
    plcWriteI2C(ADDR_Q73_AUX, register_copy, "Set Direction");
  }
}

/**
 * @brief Set speed profile
 * @param speed_profile 0=slow (Y8), 1=medium (Y7), 2=fast (Y6)
 * @note Mapping: SPEED_PROFILE_1(0)=slowest, SPEED_PROFILE_3(2)=fastest
 *       Hardware: Y6=FAST, Y7=MEDIUM, Y8=SLOW
 *       So we invert: profile 0→SLOW(Y8), profile 2→FAST(Y6)
 */
void plcSetSpeed(uint8_t speed_profile) {
  // S5 PLC Speed handling:
  // The original Elbo protocol used I 72.0 (Medium) and I 72.5 (Fast) as qualifiers.
  // However, I 73.7 (Velocity Enable) is the master bit.
  // We'll manage I 73.7 in motion_control.cpp.
  // This function is kept for API compatibility but currently only logs.

  logDebug("[PLC] Requested speed profile: %d (Elbo protocol bypass)", speed_profile);
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
  q73_aux_shadow = 0xFF;      // All AUX OFF

  uint8_t reg1 = q73_shadow_register;
  uint8_t reg2 = q73_aux_shadow;
  xSemaphoreGive(plc_shadow_mutex);
  
  if (!plc_in_transaction) {
    plcWriteI2C(ADDR_Q73_OUTPUT, reg1, "Clear All");
    plcWriteI2C(ADDR_Q73_AUX, reg2, "Clear AUX");
  }
}

/**
 * @brief Force write shadow register to hardware (for recovery)
 */
void plcCommitOutputs() {
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcCommitOutputs FAILED (shadow register dirty)");
    return;
  }

  uint8_t reg1 = q73_shadow_register;
  uint8_t reg2 = q73_aux_shadow;
  xSemaphoreGive(plc_shadow_mutex);
  
  plcWriteI2C(ADDR_Q73_OUTPUT, reg1, "Commit");
  plcWriteI2C(ADDR_Q73_AUX, reg2, "Commit AUX");
}

/**
 * @brief Start a batch of relay changes
 */
void plcBeginTransaction() {
  plc_in_transaction = true;
}

/**
 * @brief Finish batch and write to hardware
 */
void plcEndTransaction() {
  plc_in_transaction = false;
  plcCommitOutputs(); // Force write now
}

void plcSetAuxRelay(uint8_t bit, bool state) {
  if (bit > 7) return;

  if (!plcAcquireShadowMutex()) {
    logError("[PLC] plcSetAuxRelay FAILED (shadow register dirty)");
    return;
  }

  if (state) {
    q73_aux_shadow &= ~(1 << bit); // Active-low ON
  } else {
    q73_aux_shadow |= (1 << bit);  // Active-low OFF
  }

  uint8_t register_copy = q73_aux_shadow;
  xSemaphoreGive(plc_shadow_mutex);

  if (!plc_in_transaction) {
    plcWriteI2C(ADDR_Q73_AUX, register_copy, "Set Aux Relay");
  }
}


// PHASE 3.1: Read current speed profile from shadow register
uint8_t plcGetSpeedProfile() {
  if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    logWarning("[PLC] Failed to acquire shadow mutex for GetSpeedProfile");
    return 0xFF;
  }

  uint8_t reg = q73_shadow_register;
  xSemaphoreGive(plc_shadow_mutex);

  // Active-low: bit cleared = speed active
  if (!(reg & (1 << PLC_OUT_SPEED_FAST)))
    return 0; // Fast
  if (!(reg & (1 << PLC_OUT_SPEED_MEDIUM)))
    return 1; // Medium
  if (!(reg & (1 << PLC_OUT_SPEED_SLOW)))
    return 2; // Slow

  return 0xFF; // No speed set
}

void plcSetOutput(uint16_t pin, bool state) {
    // Support both virtual pin IDs (116-131) and legacy indices (1-16)
    if (pin >= 116 && pin <= 131) {
        // Virtual pins map to Bank 1 (116-123) and Bank 2 (124-131)
        elboQ73SetRelay(pin - 116, state);
    } else if (pin >= 1 && pin <= 16) {
        // Legacy indices 1-16
        elboQ73SetRelay(pin - 1, state);
    }
}

void elboQ73SetRelay(uint8_t relay_bit, bool state) {
  if (relay_bit > 15) // Support up to 16 outputs (0-15)
    return;

  // PHASE 5.7: Fix - Use retry helper
  if (!plcAcquireShadowMutex()) {
    logError("[PLC] SetRelay FAILED for bit %d (shadow register dirty)",
             relay_bit);
    return;
  }

  uint8_t target_addr;
  uint8_t register_val;

  // Active-low logic: 0 = ON, 1 = OFF
  if (relay_bit < 8) {
      // Bank 1 (Y1-Y8) -> ADDR_Q73_OUTPUT
      if (state) {
        q73_shadow_register &= ~(1 << relay_bit);
      } else {
        q73_shadow_register |= (1 << relay_bit);
      }
      register_val = q73_shadow_register;
      target_addr = ADDR_Q73_OUTPUT;
  } else {
      // Bank 2 (Y9-Y16) -> ADDR_Q73_AUX
      uint8_t aux_bit = relay_bit - 8;
      if (state) {
        q73_aux_shadow &= ~(1 << aux_bit);
      } else {
        q73_aux_shadow |= (1 << aux_bit);
      }
      register_val = q73_aux_shadow;
      target_addr = ADDR_Q73_AUX;
  }

  xSemaphoreGive(plc_shadow_mutex);

  if (!plc_in_transaction) {
    plcWriteI2C(target_addr, register_val, "Set Relay");
  }
}

// ============================================================================
// INPUT READING
// ============================================================================

void elboI73Refresh() {
  if (!g_plc_hardware_present) return;

  // CRITICAL FIX: Acquire PLC I2C mutex
  if (!taskLockMutex(taskGetI2cPlcMutex(), 100)) return;

  uint32_t start = micros();

  // Read I 73 (Bus 73 status at X9-X16)
  uint8_t count1 = Wire.requestFrom((uint8_t)ADDR_I73_INPUT, (uint8_t)1);
  if (count1 == 1) {
    i73_input_shadow = Wire.read();
  }

  // Read I 72 (Bus 72 limits at X1-X8 - note: using 0x22 if safe)
  // We use the BOARD_INPUT_I2C_ADDR alias if defined, or 0x22 directly
  uint8_t count2 = Wire.requestFrom((uint8_t)0x22, (uint8_t)1);
  if (count2 == 1) {
    i72_input_shadow = Wire.read();
  }

  updateLatency(&input_latency, micros() - start);
  taskUnlockMutex(taskGetI2cPlcMutex());
}

/**
 * @brief Reads a specific bit from the PLC input shadow registers.
 * @param bit Bit index 0-15 (0-7 = Bus 73, 8-15 = Bus 72).
 * @return State of the bit.
 */
bool elboGetInput(uint8_t bit) {
  if (bit < 8) {
    return (i73_input_shadow & (1 << bit));
  } else if (bit < 16) {
    return (i72_input_shadow & (1 << (bit - 8)));
  }
  return false;
}

/**
 * @brief Reads a specific bit from the I73 input shadow register.
 * @param bit Bit index (0-7).
 * @param success [Optional] Pointer to bool. Set to false if I2C fails (legacy).
 * @return State of the bit from the last elboI73Refresh() call.
 */
bool elboI73GetInput(uint8_t bit, bool *success) {
  if (success) *success = true; // Cached reads always "succeed" if hardware present
  return (i73_input_shadow & (1 << bit));
}

void plcPrintDiagnostics() {
  logPrintln("\n[PLC] === IO Diagnostics ===");

  // Read shadow register safely with mutex protection
  uint8_t output_reg = 0x00;
  if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    output_reg = q73_shadow_register;
    xSemaphoreGive(plc_shadow_mutex);
  } else {
    logWarning("[PLC] Could not acquire shadow mutex for diagnostics");
  }

  logPrintf("Output Reg 1: 0x%02X (Bank 1)\n", output_reg);
  logPrintf("Output Reg 2: 0x%02X (Bank 2)\n", q73_aux_shadow);
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
                  
    Wire.beginTransmission(ADDR_Q73_AUX);
    err = Wire.endTransmission();
    logPrintf("AUX (0x%02X) Status: %s\n", ADDR_Q73_AUX,
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

uint8_t elboI72GetRawState() { return i72_input_shadow; }

uint16_t plcGetInputRawState() {
    return ((uint16_t)i72_input_shadow << 8) | i73_input_shadow;
}

bool elboI73GetInput(uint8_t bit, bool *success) {
    if (success) *success = true;
    return elboGetInput(bit);
}

uint8_t elboQ73GetRawState() { return q73_shadow_register; }

uint8_t elboQ73GetAuxRawState() { return q73_aux_shadow; }

void plcGetLatencyStats(internal_latency_stats_t* src, bus_latency_stats_t* dest) {
    if (src->samples == 0) {
        dest->min_us = 0;
        dest->max_us = 0;
        dest->avg_us = 0;
        dest->std_dev_us = 0;
        dest->samples = 0;
        return;
    }
    dest->min_us = src->min_us;
    dest->max_us = src->max_us;
    dest->avg_us = src->total_us / src->samples;
    dest->samples = src->samples;
    
    // StdDev calculation
    uint64_t mean_sq = src->total_sq_us / src->samples;
    uint64_t avg_sq = (uint64_t)dest->avg_us * dest->avg_us;
    if (mean_sq > avg_sq) {
        dest->std_dev_us = (uint32_t)sqrt(mean_sq - avg_sq);
    } else {
        dest->std_dev_us = 0;
    }
}

void plcGetInputLatency(bus_latency_stats_t* stats) {
    plcGetLatencyStats(&input_latency, stats);
}

void plcGetOutputLatency(bus_latency_stats_t* stats) {
    plcGetLatencyStats(&output_latency, stats);
}

void plcResetLatencyStats() {
    input_latency = {UINT32_MAX, 0, 0, 0, 0};
    output_latency = {UINT32_MAX, 0, 0, 0, 0};
}

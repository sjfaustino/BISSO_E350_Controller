/**
 * @file plc_iface.cpp
 * @brief Hardware Abstraction Layer for ELBO PLC (Gemini v3.5.21)
 * @details Implements robust I2C drivers with error checking.
 *          CRITICAL FIX: Added spinlock protection for shadow register access
 *          to prevent race conditions when multiple tasks modify the relay states.
 */

#include "plc_iface.h"
#include "system_constants.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "task_manager.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>

// Shadow Registers
static uint8_t i73_input_shadow = 0xFF;
static uint8_t q73_shadow_register = 0x00;

// CRITICAL FIX: Mutex to protect shadow register access
// Multiple tasks can call elboSetDirection(), elboSetSpeedProfile(), elboQ73SetRelay()
// Without protection, race conditions can corrupt relay state
// NOTE: Using Mutex instead of Spinlock because:
//   1. Shadow registers are only accessed from tasks, not ISRs
//   2. Mutexes allow proper task scheduling instead of disabling interrupts
//   3. More efficient for multi-task synchronization
static SemaphoreHandle_t plc_shadow_mutex = NULL; 

#define I2C_RETRIES 3

// ============================================================================
// INTERNAL I2C HELPER
// ============================================================================

static bool plcWriteI2C(uint8_t address, uint8_t data, const char* context) {
    // CRITICAL FIX: Acquire PLC I2C mutex to prevent bus contention
    // Timeout: 200ms (PLC operations are time-sensitive but not critical)
    if (!taskLockMutex(taskGetI2cPlcMutex(), 200)) {
        logWarning("[PLC] PLC I2C mutex timeout - skipping write: %s", context);
        return false;
    }

    uint8_t error = 0;
    for (int i = 0; i < I2C_RETRIES; i++) {
        Wire.beginTransmission(address);
        Wire.write(data);
        error = Wire.endTransmission();

        if (error == 0) {
            taskUnlockMutex(taskGetI2cPlcMutex());
            return true; // Success
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    taskUnlockMutex(taskGetI2cPlcMutex());
    logError("[PLC] I2C Write Failed (Addr 0x%02X, Err %d): %s", address, error, context);
    faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, address, context);
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
    delay(10); // Allow bus to settle

    // Reset Outputs (Safe State: All OFF)
    q73_shadow_register = 0x00;

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
    if (axis >= 4) return;

    // CRITICAL FIX: Use mutex to protect shadow register modification
    // Race condition: Motion task and CLI task could both call this function,
    // causing one task's modification to overwrite the other's
    if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logWarning("[PLC] Failed to acquire shadow mutex for SetDirection");
        return;
    }

    // Use standard shift. Relays 0-3 are X,Y,Z,A Directions
    uint8_t mask = (1 << axis);

    if (forward) {
        q73_shadow_register |= mask;
    } else {
        q73_shadow_register &= ~mask;
    }

    // Make a copy for I2C write before releasing mutex
    uint8_t register_copy = q73_shadow_register;

    xSemaphoreGive(plc_shadow_mutex);

    // Do NOT modify Enable bit here.
    plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Direction");
}

void elboSetSpeedProfile(uint8_t profile_index) {
    // CRITICAL FIX: Use mutex to protect shadow register modification
    // Race condition: Motion task and other tasks could modify simultaneously
    if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logWarning("[PLC] Failed to acquire shadow mutex for SetSpeedProfile");
        return;
    }

    // Clear Speed bits (4, 5, 6)
    q73_shadow_register &= ~( (1<<ELBO_Q73_SPEED_1) | (1<<ELBO_Q73_SPEED_2) | (1<<ELBO_Q73_SPEED_3) );

    switch(profile_index) {
        case 0: q73_shadow_register |= (1 << ELBO_Q73_SPEED_1); break;
        case 1: q73_shadow_register |= (1 << ELBO_Q73_SPEED_2); break;
        case 2: q73_shadow_register |= (1 << ELBO_Q73_SPEED_3); break;
        default: break;
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
        return 0xFF;  // Return error on mutex timeout
    }

    uint8_t speed_bits = (q73_shadow_register >> ELBO_Q73_SPEED_1) & 0x07;

    xSemaphoreGive(plc_shadow_mutex);

    // Decode speed bits to profile index
    if (speed_bits & (1 << (ELBO_Q73_SPEED_1 - ELBO_Q73_SPEED_1))) return 0;  // Profile 0
    if (speed_bits & (1 << (ELBO_Q73_SPEED_2 - ELBO_Q73_SPEED_1))) return 1;  // Profile 1
    if (speed_bits & (1 << (ELBO_Q73_SPEED_3 - ELBO_Q73_SPEED_1))) return 2;  // Profile 2

    return 0xFF;  // No profile set or error
}

void elboQ73SetRelay(uint8_t relay_bit, bool state) {
    if (relay_bit > 7) return;

    // CRITICAL FIX: Use mutex to protect shadow register modification
    // Race condition: Multiple relay commands could be lost if not protected
    if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logWarning("[PLC] Failed to acquire shadow mutex for SetRelay");
        return;
    }

    if (state) {
        q73_shadow_register |= (1 << relay_bit);
    } else {
        q73_shadow_register &= ~(1 << relay_bit);
    }

    // Make a copy for I2C write before releasing mutex
    uint8_t register_copy = q73_shadow_register;

    xSemaphoreGive(plc_shadow_mutex);

    plcWriteI2C(ADDR_Q73_OUTPUT, register_copy, "Set Relay");
}

// ============================================================================
// INPUT READING
// ============================================================================

bool elboI73GetInput(uint8_t bit, bool* success) {
    // CRITICAL FIX: Acquire PLC I2C mutex to prevent bus contention
    // Timeout: 200ms (PLC operations are time-sensitive but not critical)
    if (!taskLockMutex(taskGetI2cPlcMutex(), 200)) {
        logWarning("[PLC] PLC I2C mutex timeout - using cached input");
        if (success) *success = false;
        return (i73_input_shadow & (1 << bit));
    }

    uint8_t count = Wire.requestFrom((uint8_t)ADDR_I73_INPUT, (uint8_t)1);

    if (count == 1) {
        i73_input_shadow = Wire.read();
        if (success) *success = true;
    } else {
        if (success) *success = false;

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
    Serial.println("\n[PLC] === IO Diagnostics ===");

    // Read shadow register safely with mutex protection
    uint8_t output_reg = 0x00;
    if (xSemaphoreTake(plc_shadow_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        output_reg = q73_shadow_register;
        xSemaphoreGive(plc_shadow_mutex);
    } else {
        Serial.println("Warning: Could not acquire shadow mutex for diagnostics");
    }

    Serial.printf("Output Register: 0x%02X\n", output_reg);
    Serial.printf("Input Register:  0x%02X\n", i73_input_shadow);

    // CRITICAL FIX: Acquire PLC I2C mutex for diagnostics
    if (taskLockMutex(taskGetI2cPlcMutex(), 500)) {
        Wire.beginTransmission(ADDR_Q73_OUTPUT);
        uint8_t err = Wire.endTransmission();
        Serial.printf("Q73 (0x%02X) Status: %s\n", ADDR_Q73_OUTPUT, (err == 0) ? "OK" : "ERROR");
        taskUnlockMutex(taskGetI2cPlcMutex());
    } else {
        Serial.println("Q73: Could not acquire I2C mutex for diagnostics");
    }
}
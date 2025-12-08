/**
 * @file plc_iface.cpp
 * @brief Hardware Abstraction Layer for ELBO PLC (Gemini v3.5.21)
 * @details Implements robust I2C drivers with error checking.
 */

#include "plc_iface.h" 
#include "system_constants.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "task_manager.h"
#include <Wire.h>

// Shadow Registers
static uint8_t i73_input_shadow = 0xFF; 
static uint8_t q73_shadow_register = 0x00; 

#define I2C_RETRIES 3

// ============================================================================
// INTERNAL I2C HELPER
// ============================================================================

static bool plcWriteI2C(uint8_t address, uint8_t data, const char* context) {
    uint8_t error = 0;
    for (int i = 0; i < I2C_RETRIES; i++) {
        Wire.beginTransmission(address);
        Wire.write(data);
        error = Wire.endTransmission();
        
        if (error == 0) return true; // Success
        
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }

    logError("[PLC] I2C Write Failed (Addr 0x%02X, Err %d): %s", address, error, context);
    faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, address, context);
    return false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void elboInit() {
    logInfo("[PLC] Initializing I2C Bus...");
    
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

    // Use standard shift. Relays 0-3 are X,Y,Z,A Directions
    uint8_t mask = (1 << axis);

    if (forward) {
        q73_shadow_register |= mask;  
    } else {
        q73_shadow_register &= ~mask; 
    }

    // Do NOT modify Enable bit here.
    plcWriteI2C(ADDR_Q73_OUTPUT, q73_shadow_register, "Set Direction");
}

void elboSetSpeedProfile(uint8_t profile_index) {
    // Clear Speed bits (4, 5, 6)
    q73_shadow_register &= ~( (1<<ELBO_Q73_SPEED_1) | (1<<ELBO_Q73_SPEED_2) | (1<<ELBO_Q73_SPEED_3) );

    switch(profile_index) {
        case 0: q73_shadow_register |= (1 << ELBO_Q73_SPEED_1); break;
        case 1: q73_shadow_register |= (1 << ELBO_Q73_SPEED_2); break;
        case 2: q73_shadow_register |= (1 << ELBO_Q73_SPEED_3); break;
        default: break; 
    }

    plcWriteI2C(ADDR_Q73_OUTPUT, q73_shadow_register, "Set Speed");
}

void elboQ73SetRelay(uint8_t relay_bit, bool state) {
    if (relay_bit > 7) return;

    if (state) {
        q73_shadow_register |= (1 << relay_bit);
    } else {
        q73_shadow_register &= ~(1 << relay_bit);
    }

    plcWriteI2C(ADDR_Q73_OUTPUT, q73_shadow_register, "Set Relay");
}

// ============================================================================
// INPUT READING
// ============================================================================

bool elboI73GetInput(uint8_t bit, bool* success) {
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
    
    // Check bit state (Returns cached value on failure)
    return (i73_input_shadow & (1 << bit));
}

void elboDiagnostics() {
    Serial.println("\n[PLC] === IO Diagnostics ===");
    Serial.printf("Output Register: 0x%02X\n", q73_shadow_register);
    Serial.printf("Input Register:  0x%02X\n", i73_input_shadow);
    
    Wire.beginTransmission(ADDR_Q73_OUTPUT);
    uint8_t err = Wire.endTransmission();
    Serial.printf("Q73 (0x%02X) Status: %s\n", ADDR_Q73_OUTPUT, (err == 0) ? "OK" : "ERROR");
}
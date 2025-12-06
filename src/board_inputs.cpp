#include "board_inputs.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "hardware_config.h" // HAL for dynamic pin mapping
#include <Wire.h>

static uint8_t input_cache = 0xFF; 

// --- FAST PATH CACHE ---
// Bitmasks are resolved during init to avoid expensive lookups 
// inside the high-frequency (5ms) safety loop.
static uint8_t mask_estop = 0;
static uint8_t mask_pause = 0;
static uint8_t mask_resume = 0;

void boardInputsInit() {
    logInfo("[INPUTS] Initializing...");
    
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
        logError("[INPUTS] Invalid E-Stop Pin %d. Defaulting to P3 (X4).", pin_estop);
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

    logInfo("[INPUTS] Fast Path Mapped: Estop=0x%02X, Pause=0x%02X, Resume=0x%02X", 
            mask_estop, mask_pause, mask_resume);

    // 3. Hardware Bus Init
    uint8_t temp_data = 0xFF;
    i2c_result_t result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &temp_data, 1);
    
    if (result == I2C_RESULT_OK) {
        input_cache = temp_data;
        logInfo("[INPUTS] Board detected (0x24)");
    } else {
        logError("[INPUTS] Board NOT detected (0x24)");
        faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR, "Inputs init failed");
    }
}

button_state_t boardInputsUpdate() {
    button_state_t state = {false, false, false, false};
    i2c_result_t result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    
    if (result != I2C_RESULT_OK) {
        state.connection_ok = false;
        return state;
    }
    state.connection_ok = true;
    
    // --- FAST PATH EXECUTION ---
    // Apply cached masks. No function calls, no branches.
    // Logic:
    // E-STOP: NC (Normally Closed). Active = High (Open Circuit/Pressed)
    // Buttons: NO (Normally Open). Active = Low (Short to Ground)
    
    state.estop_active = (input_cache & mask_estop);       
    state.pause_pressed = !(input_cache & mask_pause);     
    state.resume_pressed = !(input_cache & mask_resume);   
    
    return state;
}

void boardInputsDiagnostics() {
    Serial.println("\n[INPUTS] === Physical Inputs (0x24) ===");
    i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    Serial.printf("Raw Byte: 0x%02X\n", input_cache);
    
    // Decode using current maps for diagnostics
    bool estop = (input_cache & mask_estop);
    bool pause = !(input_cache & mask_pause);
    bool resume = !(input_cache & mask_resume);
    
    Serial.printf("  E-STOP (Mask 0x%02X): %s\n", mask_estop, estop ? "TRIPPED (OPEN)" : "OK (CLOSED)");
    Serial.printf("  PAUSE  (Mask 0x%02X): %s\n", mask_pause, pause ? "PRESSED" : "RELEASED");
    Serial.printf("  RESUME (Mask 0x%02X): %s\n", mask_resume, resume ? "PRESSED" : "RELEASED");
}
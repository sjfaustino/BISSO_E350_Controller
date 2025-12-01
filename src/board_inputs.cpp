#include "board_inputs.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include <Wire.h>

static uint8_t input_cache = 0xFF; // Default to all high (inputs open)

void boardInputsInit() {
    Serial.println("[INPUTS] Initializing Board Inputs (KC868-A16 X1-X8)...");
    
    // Initial read to clear cache and verify connection
    // We assume i2cRecoveryInit() has already been called by main/plc_iface
    uint8_t temp_data = 0xFF;
    i2c_result_t result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &temp_data, 1);
    
    if (result == I2C_RESULT_OK) {
        input_cache = temp_data;
        Serial.println("[INPUTS] ✅ Input board (0x24) detected");
    } else {
        Serial.println("[INPUTS] ❌ Input board (0x24) NOT detected");
        faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR, 
                      "Board Inputs init failed (Result: %d)", result);
    }
}

button_state_t boardInputsUpdate() {
    button_state_t state = {false, false, false, false};
    
    // Read PCF8574
    // Note: PCF8574 inputs are Quasi-bidirectional. 
    // Writing 1 allows them to be used as inputs (weak pull-up).
    // However, usually just reading is sufficient if they haven't been written low.
    
    i2c_result_t result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    
    if (result != I2C_RESULT_OK) {
        state.connection_ok = false;
        return state;
    }
    
    state.connection_ok = true;
    
    // --- Logic Mapping ---
    // The KC868-A16 inputs are Opto-isolated.
    // Grounding the input (Closing the switch) -> Logic 0 (Low)
    // Open circuit (Button released/NC open)   -> Logic 1 (High)
    
    // 1. E-Stop (Normally Closed / NC)
    // - Normal Operation (Not Pressed): Switch Closed -> Input LOW (0)
    // - Emergency (Pressed): Switch Open -> Input HIGH (1)
    // Therefore: Active if Bit is 1
    state.estop_active = (input_cache & (1 << INPUT_BIT_ESTOP));
    
    // 2. Pause (Normally Open / NO)
    // - Normal (Released): Switch Open -> Input HIGH (1)
    // - Action (Pressed): Switch Closed -> Input LOW (0)
    // Therefore: Active if Bit is 0
    state.pause_pressed = !(input_cache & (1 << INPUT_BIT_PAUSE));
    
    // 3. Resume (Normally Open / NO)
    // - Normal (Released): Switch Open -> Input HIGH (1)
    // - Action (Pressed): Switch Closed -> Input LOW (0)
    // Therefore: Active if Bit is 0
    state.resume_pressed = !(input_cache & (1 << INPUT_BIT_RESUME));
    
    return state;
}

void boardInputsDiagnostics() {
    Serial.println("\n[INPUTS] === Physical Inputs (0x24) ===");
    
    // Perform a fresh read for diagnostics
    i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    
    Serial.printf("Raw Byte: 0x%02X\n", input_cache);
    
    bool estop = (input_cache & (1 << INPUT_BIT_ESTOP));
    bool pause = !(input_cache & (1 << INPUT_BIT_PAUSE));
    bool resume = !(input_cache & (1 << INPUT_BIT_RESUME));
    
    Serial.printf("  E-STOP (NC/X4): %s\n", estop ? "TRIGGERED (Open)" : "NORMAL (Closed)");
    Serial.printf("  PAUSE  (NO/X5): %s\n", pause ? "PRESSED" : "RELEASED");
    Serial.printf("  RESUME (NO/X6): %s\n", resume ? "PRESSED" : "RELEASED");
}
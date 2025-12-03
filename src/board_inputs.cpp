#include "board_inputs.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include <Wire.h>

static uint8_t input_cache = 0xFF; 

void boardInputsInit() {
    Serial.println("[INPUTS] Initializing...");
    
    uint8_t temp_data = 0xFF;
    i2c_result_t result = i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &temp_data, 1);
    
    if (result == I2C_RESULT_OK) {
        input_cache = temp_data;
        Serial.println("[INPUTS] [OK] Board (0x24) detected.");
    } else {
        Serial.println("[INPUTS] [FAIL] Board (0x24) NOT detected.");
        faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR, 
                      "Board Inputs init failed (Result: %d)", result);
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
    state.estop_active = (input_cache & (1 << INPUT_BIT_ESTOP));
    state.pause_pressed = !(input_cache & (1 << INPUT_BIT_PAUSE));
    state.resume_pressed = !(input_cache & (1 << INPUT_BIT_RESUME));
    return state;
}

void boardInputsDiagnostics() {
    Serial.println("\n[INPUTS] === Physical Inputs (0x24) ===");
    i2cReadWithRetry(BOARD_INPUT_I2C_ADDR, &input_cache, 1);
    Serial.printf("Raw Byte: 0x%02X\n", input_cache);
    
    bool estop = (input_cache & (1 << INPUT_BIT_ESTOP));
    bool pause = !(input_cache & (1 << INPUT_BIT_PAUSE));
    bool resume = !(input_cache & (1 << INPUT_BIT_RESUME));
    
    Serial.printf("  E-STOP (NC): %s\n", estop ? "TRIGGERED (Open)" : "NORMAL (Closed)");
    Serial.printf("  PAUSE  (NO): %s\n", pause ? "PRESSED" : "RELEASED");
    Serial.printf("  RESUME (NO): %s\n", resume ? "PRESSED" : "RELEASED");
}
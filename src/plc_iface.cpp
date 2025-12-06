/**
 * @file plc_iface.cpp
 * @brief Implementation of PLC I/O Interface via I2C
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h" 
#include <Wire.h>

// Debounce Configuration
#define PLC_DEBOUNCE_REQUIRED_READS 2

// PLC Interface State - manages three PCF8574 expanders
struct {
  uint8_t I72_byte;    // Speed profile output 
  uint8_t I73_byte;    // Axis/direction/mode output
  uint8_t Q73_byte;    // PLC consenso input
  plc_status_t status;
  uint32_t last_read_ms;
  uint32_t error_count;
  uint32_t read_count;
} plc_state = {
  0xFF, 
  0xFF, 
  0x00, 
  PLC_OK,
  0,
  0,
  0
};

// State for Debounce Logic
static uint8_t input_debounce_count = 0;
static uint8_t input_last_stable_byte = 0x00;

void plcIfaceInit() {
  logInfo("[PLC] Initializing...");
  
  // Initialize I2C Bus Manager
  i2cRecoveryInit();
  
  // OPTIMIZATION: Set global I2C clock to 400kHz
  Wire.setClock(PLC_I2C_SPEED);

  // --- SAFETY: FORCE SAFE STATE (Active LOW Logic: 1 = OFF) ---
  uint8_t safe_output = 0xFF; 
  
  i2c_result_t res1 = i2cWriteWithRetry(PCF8574_I72_ADDR, &safe_output, 1);
  if (res1 != I2C_RESULT_OK) {
      logWarning("[PLC] Failed to set safe state on I72 (Speed)");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I72_ADDR, "Init Safe State I72 Failed");
  }

  i2c_result_t res2 = i2cWriteWithRetry(PCF8574_I73_ADDR, &safe_output, 1);
  if (res2 != I2C_RESULT_OK) {
      logWarning("[PLC] Failed to set safe state on I73 (Axis/Dir)");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR, "Init Safe State I73 Failed");
  }
  
  plc_state.I72_byte = 0xFF;
  plc_state.I73_byte = 0xFF;

  plc_state.status = PLC_OK;
  plc_state.last_read_ms = millis();
  plc_state.error_count = 0;
  plc_state.read_count = 0;
  
  uint8_t initial_read;
  if (i2cReadWithRetry(PCF8574_Q73_ADDR, &initial_read, 1) == I2C_RESULT_OK) {
      input_last_stable_byte = initial_read;
  }
  
  logInfo("[PLC] Interface ready");
}

void plcIfaceUpdate() {
  static uint32_t last_read = 0;
  if (millis() - last_read < PLC_READ_INTERVAL_MS) {
    return;
  }
  last_read = millis();
  
  uint8_t current_read_byte;
  
  i2c_result_t result = i2cReadWithRetry(PCF8574_Q73_ADDR, &current_read_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    plc_state.read_count++;
    
    if (current_read_byte == input_last_stable_byte) {
        input_debounce_count++;
        
        if (input_debounce_count >= PLC_DEBOUNCE_REQUIRED_READS) {
            plc_state.Q73_byte = current_read_byte;
            plc_state.status = PLC_OK;
            plc_state.last_read_ms = millis();
            input_debounce_count = PLC_DEBOUNCE_REQUIRED_READS; 
        }
    } else {
        input_last_stable_byte = current_read_byte;
        input_debounce_count = 1;
        plc_state.status = PLC_TIMEOUT; 
    }

  } else {
    plc_state.error_count++;
    input_debounce_count = 0;
    plc_state.status = (result == I2C_RESULT_NACK) ? PLC_NOT_FOUND : PLC_TIMEOUT; 
    
    logWarning("[PLC] Q73 read failed: %s", i2cResultToString(result));
    faultLogWarning(FAULT_PLC_COMM_LOSS, "Q73 Consensus Read Failed");
  }
}

// ============================================================================
// Generic bit/byte operations (Deprecated)
// ============================================================================

bool plcGetBit(uint8_t bit) {
  if (bit >= 8) return false;
  return (plc_state.Q73_byte & (1 << bit)) != 0;
}

void plcSetBit(uint8_t bit, bool value) {
  logWarning("[PLC] plcSetBit() deprecated. Use ELBO functions.");
}

uint8_t plcGetByte(uint8_t offset) {
  switch (offset) {
    case 0: return plc_state.Q73_byte;
    case 1: return plc_state.I72_byte;
    case 2: return plc_state.I73_byte;
    default: return 0;
  }
}

void plcSetByte(uint8_t offset, uint8_t value) {
  logWarning("[PLC] plcSetByte() deprecated. Use ELBO functions.");
}

uint16_t plcGetWord(uint8_t offset) { return 0; }
void plcSetWord(uint8_t offset, uint16_t value) {}

// ============================================================================
// ELBO I72 Functions (Speed Control)
// ============================================================================

bool elboI72GetSpeed(uint8_t speed_bit) {
  if (speed_bit >= 8) return false;
  return (plc_state.I72_byte & (1 << speed_bit)) == 0;
}

bool elboI72SetSpeed(uint8_t speed_bit, bool value) {
  if (speed_bit >= 8) return false;
  
  if (value) plc_state.I72_byte &= ~(1 << speed_bit);
  else plc_state.I72_byte |= (1 << speed_bit);
  
  i2c_result_t result = i2cWriteFast(PCF8574_I72_ADDR, &plc_state.I72_byte, 1);
  
  if (result == I2C_RESULT_OK) return true;
  else {
    logError("[ELBO] I72 speed write failed: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I72_ADDR, "I72 Write Fail");
    return false;
  }
}

bool elboI72WriteBatch(uint8_t clear_mask, uint8_t set_bits) {
    uint8_t old_byte = plc_state.I72_byte;
    uint8_t new_byte = (old_byte & ~clear_mask) | (set_bits & clear_mask);
    
    if (new_byte == old_byte) return true; // Optimization: No write needed

    plc_state.I72_byte = new_byte;
    i2c_result_t result = i2cWriteFast(PCF8574_I72_ADDR, &plc_state.I72_byte, 1);

    if (result == I2C_RESULT_OK) return true;
    
    plc_state.error_count++;
    logError("[ELBO] I72 Batch Write Failed: %s", i2cResultToString(result));
    return false;
}

// ============================================================================
// ELBO I73 Functions (Axis/Direction/Mode)
// ============================================================================

bool elboI73GetAxis(uint8_t axis_bit) {
  if (axis_bit >= 8) return false;
  return (plc_state.I73_byte & (1 << axis_bit)) == 0;
}

bool elboI73SetAxis(uint8_t axis_bit, bool value) {
  if (axis_bit >= 8) return false;
  
  if (value) plc_state.I73_byte &= ~(1 << axis_bit);
  else plc_state.I73_byte |= (1 << axis_bit);
  
  i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) return true;
  else {
    logError("[ELBO] I73 axis write failed: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR, "I73 Write Fail");
    return false;
  }
}

bool elboI73GetDirection(uint8_t dir_bit) {
  if (dir_bit >= 8) return false;
  return (plc_state.I73_byte & (1 << dir_bit)) == 0;
}

bool elboI73SetDirection(uint8_t dir_bit, bool value) {
  if (dir_bit >= 8) return false;
  
  if (value) plc_state.I73_byte &= ~(1 << dir_bit);
  else plc_state.I73_byte |= (1 << dir_bit);
  
  i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) return true;
  else {
    logError("[ELBO] I73 direction write failed: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR, "I73 Write Fail");
    return false;
  }
}

bool elboI73GetVSMode() {
  return (plc_state.I73_byte & (1 << ELBO_I73_V_S_MODE)) == 0;
}

bool elboI73SetVSMode(bool value) {
  if (value) plc_state.I73_byte &= ~(1 << ELBO_I73_V_S_MODE);
  else plc_state.I73_byte |= (1 << ELBO_I73_V_S_MODE);
  
  i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) return true;
  else {
    logError("[ELBO] I2C Error writing I73 V/S mode: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogWarning(FAULT_I2C_ERROR, "I73 V/S Write Failed");
    return false;
  }
}

bool elboI73WriteBatch(uint8_t clear_mask, uint8_t set_bits) {
    uint8_t old_byte = plc_state.I73_byte;
    // Apply changes: Clear bits in mask, then set new bits
    // Note: PCF8574 is Active LOW. 'true' means 0 (sink current), 'false' means 1 (float high).
    // logic: 
    // If bit in clear_mask is 1, we want to update it.
    // If corresponding bit in set_bits is 1 (ON), we write 0.
    // If corresponding bit in set_bits is 0 (OFF), we write 1.
    
    // However, the caller will pass "set_bits" where 1=Active (LOW).
    // So we need to invert logic carefully or stick to raw register logic.
    // Let's assume clear_mask/set_bits operate on the RAW REGISTER value (1=OFF, 0=ON).
    // Caller calculates the register value directly.
    
    uint8_t new_byte = (old_byte & ~clear_mask) | (set_bits & clear_mask);
    
    if (new_byte == old_byte) return true; 

    plc_state.I73_byte = new_byte;
    i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);

    if (result == I2C_RESULT_OK) return true;
    
    plc_state.error_count++;
    logError("[ELBO] I73 Batch Write Failed: %s", i2cResultToString(result));
    return false;
}

// ============================================================================
// ELBO Q73 Functions (Consenso Feedback from PLC)
// ============================================================================

bool elboQ73GetConsenso(uint8_t axis) {
  if (axis >= 3) return false;
  return (plc_state.Q73_byte & (1 << axis)) != 0;
}

bool elboQ73GetAutoManual() {
  return (plc_state.Q73_byte & (1 << ELBO_Q73_AUTO_MANUAL)) != 0;
}

// ============================================================================
// Diagnostics
// ============================================================================

plc_status_t plcGetStatus() { return plc_state.status; }
uint32_t plcGetLastReadTime() { return plc_state.last_read_ms; }
uint32_t plcGetErrorCount() { return plc_state.error_count; }

void plcDiagnostics() {
  Serial.println("\n[PLC] === Diagnostics ===");
  Serial.printf("Status: %d\n", plc_state.status);
  Serial.printf("Errors: %lu\n", (unsigned long)plc_state.error_count); 
  Serial.printf("Reads: %lu\n", (unsigned long)plc_state.read_count);
  Serial.printf("Last read: %lu ms ago\n", (unsigned long)(millis() - plc_state.last_read_ms));
  
  Serial.printf("\nI72 Output: 0x%02X\n", plc_state.I72_byte);
  Serial.printf("  FAST (P0): %s\n", elboI72GetSpeed(ELBO_I72_FAST) ? "ON" : "OFF");
  
  Serial.printf("\nI73 Output: 0x%02X\n", plc_state.I73_byte);
  Serial.printf("  AXIS_X (P1): %s\n", elboI73GetAxis(ELBO_I73_AXIS_X) ? "ON" : "OFF");
  
  Serial.println("=== End Diagnostics ===\n");
}
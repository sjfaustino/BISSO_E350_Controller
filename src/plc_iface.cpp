#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h" 
#include <Wire.h>

#define PLC_DEBOUNCE_REQUIRED_READS 2

struct {
  uint8_t I72_byte;    
  uint8_t I73_byte;    
  uint8_t Q73_byte;    
  plc_status_t status;
  uint32_t last_read_ms;
  uint32_t error_count;
  uint32_t read_count;
} plc_state = {0xFF, 0xFF, 0x00, PLC_OK, 0, 0, 0};

static uint8_t input_debounce_count = 0;
static uint8_t input_last_stable_byte = 0x00;

void plcIfaceInit() {
  logInfo("[PLC] Initializing...");
  i2cRecoveryInit();

  // FORCE SAFE STATE (Active LOW: 1 = OFF)
  uint8_t safe_output = 0xFF; 
  
  // 1. Safe State for Speed (I72)
  if (i2cWriteWithRetry(PCF8574_I72_ADDR, &safe_output, 1) != I2C_RESULT_OK) {
      logWarning("[PLC] Failed to set safe state I72");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I72_ADDR, "Init Safe State I72 Failed");
  }

  // 2. Safe State for Axis/Dir (I73)
  if (i2cWriteWithRetry(PCF8574_I73_ADDR, &safe_output, 1) != I2C_RESULT_OK) {
      logWarning("[PLC] Failed to set safe state I73");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR, "Init Safe State I73 Failed");
  }
  
  plc_state.I72_byte = 0xFF;
  plc_state.I73_byte = 0xFF;
  plc_state.status = PLC_OK;
  plc_state.last_read_ms = millis();
  
  uint8_t initial_read;
  if (i2cReadWithRetry(PCF8574_Q73_ADDR, &initial_read, 1) == I2C_RESULT_OK) {
      input_last_stable_byte = initial_read;
  }
  
  logInfo("[PLC] Interface ready");
}

void plcIfaceUpdate() {
  static uint32_t last_read = 0;
  if (millis() - last_read < PLC_READ_INTERVAL_MS) return;
  last_read = millis();
  
  uint8_t current_read_byte;
  // Uses retry logic as it runs in lower priority task
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
    logWarning("[PLC] Q73 read failed");
  }
}

// Generic Operations (Stubs for compatibility)
bool plcGetBit(uint8_t bit) { return false; }
void plcSetBit(uint8_t bit, bool value) {}
uint8_t plcGetByte(uint8_t offset) { return 0; }
void plcSetByte(uint8_t offset, uint8_t value) {}
uint16_t plcGetWord(uint8_t offset) { return 0; }
void plcSetWord(uint8_t offset, uint16_t value) {}

// ELBO I72 (Speed) - Using Fast Write for runtime
bool elboI72GetSpeed(uint8_t speed_bit) {
  if (speed_bit >= 8) return false;
  return (plc_state.I72_byte & (1 << speed_bit)) == 0;
}

bool elboI72SetSpeed(uint8_t speed_bit, bool value) {
  if (speed_bit >= 8) return false;
  if (value) plc_state.I72_byte &= ~(1 << speed_bit);
  else plc_state.I72_byte |= (1 << speed_bit);
  
  if (i2cWriteFast(PCF8574_I72_ADDR, &plc_state.I72_byte, 1) == I2C_RESULT_OK) return true;
  
  plc_state.error_count++;
  return false;
}

// ELBO I73 (Axis/Dir) - Using Fast Write
bool elboI73GetAxis(uint8_t axis_bit) {
  if (axis_bit >= 8) return false;
  return (plc_state.I73_byte & (1 << axis_bit)) == 0;
}

bool elboI73SetAxis(uint8_t axis_bit, bool value) {
  if (axis_bit >= 8) return false;
  if (value) plc_state.I73_byte &= ~(1 << axis_bit);
  else plc_state.I73_byte |= (1 << axis_bit);
  
  if (i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1) == I2C_RESULT_OK) return true;
  
  plc_state.error_count++;
  return false;
}

bool elboI73GetDirection(uint8_t dir_bit) {
  if (dir_bit >= 8) return false;
  return (plc_state.I73_byte & (1 << dir_bit)) == 0;
}

bool elboI73SetDirection(uint8_t dir_bit, bool value) {
  if (dir_bit >= 8) return false;
  if (value) plc_state.I73_byte &= ~(1 << dir_bit);
  else plc_state.I73_byte |= (1 << dir_bit);
  
  if (i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1) == I2C_RESULT_OK) return true;
  
  plc_state.error_count++;
  return false;
}

bool elboI73GetVSMode() { return (plc_state.I73_byte & (1 << ELBO_I73_V_S_MODE)) == 0; }

bool elboI73SetVSMode(bool value) {
  if (value) plc_state.I73_byte &= ~(1 << ELBO_I73_V_S_MODE);
  else plc_state.I73_byte |= (1 << ELBO_I73_V_S_MODE);
  
  if (i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1) == I2C_RESULT_OK) return true;
  
  plc_state.error_count++;
  return false;
}

bool elboQ73GetConsenso(uint8_t axis) {
  if (axis >= 3) return false;
  return (plc_state.Q73_byte & (1 << axis)) != 0;
}

bool elboQ73GetAutoManual() {
  return (plc_state.Q73_byte & (1 << ELBO_Q73_AUTO_MANUAL)) != 0;
}

plc_status_t plcGetStatus() { return plc_state.status; }
uint32_t plcGetLastReadTime() { return plc_state.last_read_ms; }
uint32_t plcGetErrorCount() { return plc_state.error_count; }

void plcDiagnostics() {
  Serial.println("\n[PLC] === Diagnostics ===");
  Serial.printf("Status: %d\nErrors: %d\n", plc_state.status, plc_state.error_count);
  Serial.printf("I72: 0x%02X | I73: 0x%02X | Q73: 0x%02X\n", 
    plc_state.I72_byte, plc_state.I73_byte, plc_state.Q73_byte);
}
#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h" 
#include <Wire.h>

#define PLC_DEBOUNCE_REQUIRED_READS 2

struct {
  uint8_t I72_byte;    // Speed profile output 
  uint8_t I73_byte;    // Axis/direction output
  uint8_t Q73_byte;    // PLC consenso input
  plc_status_t status;
  uint32_t last_read_ms;
  uint32_t error_count;
  uint32_t read_count;
} plc_state = {0xFF, 0xFF, 0x00, PLC_OK, 0, 0, 0};

// State for Debounce Logic
static uint8_t input_debounce_count = 0;
static uint8_t input_last_stable_byte = 0x00;

void plcIfaceInit() {
  Serial.println("[PLC] Initializing...");
  
  i2cRecoveryInit();

  // --- SAFETY: FORCE SAFE STATE (Active LOW Logic: 1 = OFF) ---
  // We explicitly write 0xFF to both output expanders to ensure all relays are OFF
  // before any other logic runs. This prevents startup chatter or unsafe motion.
  uint8_t safe_output = 0xFF; 
  
  // 1. Safe State for Speed Profile (I72) - Use Retry logic for robust init
  i2c_result_t res1 = i2cWriteWithRetry(PCF8574_I72_ADDR, &safe_output, 1);
  if (res1 != I2C_RESULT_OK) {
      Serial.println("[PLC] [WARN] Failed to set safe state on I72 (Speed)");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I72_ADDR, "Init Safe State I72 Failed");
  }

  // 2. Safe State for Axis/Direction (I73) - Use Retry logic
  i2c_result_t res2 = i2cWriteWithRetry(PCF8574_I73_ADDR, &safe_output, 1);
  if (res2 != I2C_RESULT_OK) {
      Serial.println("[PLC] [WARN] Failed to set safe state on I73 (Axis/Dir)");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR, "Init Safe State I73 Failed");
  }
  
  // Update internal state to match hardware
  plc_state.I72_byte = 0xFF;
  plc_state.I73_byte = 0xFF;

  plc_state.status = PLC_OK;
  plc_state.last_read_ms = millis();
  
  // Set initial stable byte to current state
  uint8_t initial_read;
  if (i2cReadWithRetry(PCF8574_Q73_ADDR, &initial_read, 1) == I2C_RESULT_OK) {
      input_last_stable_byte = initial_read;
  }
  
  Serial.println("[PLC] Interface ready");
}

void plcIfaceUpdate() {
  static uint32_t last_read = 0;
  if (millis() - last_read < PLC_READ_INTERVAL_MS) {
    return;
  }
  last_read = millis();
  
  uint8_t current_read_byte;
  
  // Input reading runs in the PLC task, so we can use standard retry logic
  i2c_result_t result = i2cReadWithRetry(PCF8574_Q73_ADDR, &current_read_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    plc_state.read_count++;
    
    // --- DEBOUNCE LOGIC ---
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
  
  // FIX: Use Fast Write for runtime motion updates (2ms timeout)
  i2c_result_t result = i2cWriteFast(PCF8574_I72_ADDR, &plc_state.I72_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I72 speed write failed: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I72_ADDR,
                  "I72 Speed Write Failed (Bit %d, Res %d)", speed_bit, result);
    return false;
  }
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
  
  // FIX: Use Fast Write
  i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I73 axis write failed: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR,
                  "I73 Axis Write Failed (Bit %d, Res %d)", axis_bit, result);
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
  
  // FIX: Use Fast Write
  i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I73 direction write failed: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR,
                  "I73 Dir Write Failed (Bit %d, Res %d)", dir_bit, result);
    return false;
  }
}

bool elboI73GetVSMode() {
  return (plc_state.I73_byte & (1 << ELBO_I73_V_S_MODE)) == 0;
}

bool elboI73SetVSMode(bool value) {
  if (value) plc_state.I73_byte &= ~(1 << ELBO_I73_V_S_MODE);
  else plc_state.I73_byte |= (1 << ELBO_I73_V_S_MODE);
  
  // FIX: Use Fast Write
  i2c_result_t result = i2cWriteFast(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I2C Error writing I73 V/S mode: %s", i2cResultToString(result));
    plc_state.error_count++;
    faultLogWarning(FAULT_I2C_ERROR, "I73 V/S Write Failed");
    return false;
  }
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
// Diagnostics & Compatibility
// ============================================================================

plc_status_t plcGetStatus() { return plc_state.status; }
uint32_t plcGetLastReadTime() { return plc_state.last_read_ms; }
uint32_t plcGetErrorCount() { return plc_state.error_count; }

// Deprecated stubs to satisfy legacy linkage if needed
bool plcGetBit(uint8_t bit) { return false; }
void plcSetBit(uint8_t bit, bool value) {}
uint8_t plcGetByte(uint8_t offset) { return 0; }
void plcSetByte(uint8_t offset, uint8_t value) {}
uint16_t plcGetWord(uint8_t offset) { return 0; }
void plcSetWord(uint8_t offset, uint16_t value) {}

void plcDiagnostics() {
  Serial.println("\n[PLC] === Diagnostics ===");
  Serial.printf("Status: %d (0=OK, 1=TIMEOUT, 2=NOT_FOUND)\n", plc_state.status);
  Serial.printf("Debounce Count: %d (Req: %d)\n", input_debounce_count, PLC_DEBOUNCE_REQUIRED_READS);
  Serial.printf("Errors: %d\n", plc_state.error_count);
  Serial.printf("Reads: %d\n", plc_state.read_count);
  
  Serial.printf("\nI72 Output: 0x%02X\n", plc_state.I72_byte);
  Serial.printf("  FAST: %s | MED: %s\n", 
    elboI72GetSpeed(ELBO_I72_FAST) ? "ON" : "OFF",
    elboI72GetSpeed(ELBO_I72_MED) ? "ON" : "OFF");
  
  Serial.printf("\nI73 Output: 0x%02X\n", plc_state.I73_byte);
  Serial.printf("  AXIS: X=%s Y=%s Z=%s | DIR: +%s -%s | VS: %s\n",
    elboI73GetAxis(ELBO_I73_AXIS_X) ? "ON" : "OFF",
    elboI73GetAxis(ELBO_I73_AXIS_Y) ? "ON" : "OFF",
    elboI73GetAxis(ELBO_I73_AXIS_Z) ? "ON" : "OFF",
    elboI73GetDirection(ELBO_I73_DIRECTION_PLUS) ? "ON" : "OFF",
    elboI73GetDirection(ELBO_I73_DIRECTION_MINUS) ? "ON" : "OFF",
    elboI73GetVSMode() ? "ON" : "OFF");
  
  Serial.printf("\nQ73 Input: 0x%02X\n", plc_state.Q73_byte);
  Serial.printf("  CONSENSO: X=%s Y=%s Z=%s | MODE: %s\n",
    elboQ73GetConsenso(1) ? "OK" : "NO",
    elboQ73GetConsenso(0) ? "OK" : "NO",
    elboQ73GetConsenso(2) ? "OK" : "NO",
    elboQ73GetAutoManual() ? "AUTO" : "MANUAL");
  
  Serial.println("[PLC] === End Diagnostics ===");
}
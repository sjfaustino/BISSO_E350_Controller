#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "fault_logging.h" 
#include <Wire.h>

// Debounce Configuration
#define PLC_DEBOUNCE_REQUIRED_READS 2

// PLC Interface State - manages three PCF8574 expanders
struct {
  uint8_t I72_byte;    // Speed profile output (PCF8574 @ 0x20)
  uint8_t I73_byte;    // Axis/direction/mode output (PCF8574 @ 0x21)
  uint8_t Q73_byte;    // PLC consenso input (PCF8574 @ 0x22)
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
  Serial.println("[PLC] PLC interface initializing...");
  
  i2cRecoveryInit();

  plc_state.status = PLC_OK;
  plc_state.last_read_ms = millis();
  plc_state.error_count = 0;
  plc_state.read_count = 0;
  
  // Set initial stable byte to current state
  uint8_t initial_read;
  if (i2cReadWithRetry(PCF8574_Q73_ADDR, &initial_read, 1) == I2C_RESULT_OK) {
      input_last_stable_byte = initial_read;
  }
  
  Serial.print("[PLC] I2C initialized: SDA=GPIO");
  Serial.print(PLC_SDA_PIN);
  Serial.print(" SCL=GPIO");
  Serial.print(PLC_SCL_PIN);
  Serial.print(" @ ");
  Serial.print(PLC_I2C_SPEED / 1000);
  Serial.println(" kHz (Managed by I2C Recovery)");
  
  Serial.println("[PLC] KC868-A16 PCF8574 Expanders:");
  Serial.printf("[PLC]   0x%02X (I72 - Speed:FAST/MED)\n", PCF8574_I72_ADDR);
  Serial.printf("[PLC]   0x%02X (I73 - Axis/Dir/V-S)\n", PCF8574_I73_ADDR);
  Serial.printf("[PLC]   0x%02X (Q73 - Consenso input)\n", PCF8574_Q73_ADDR);
  Serial.println("[PLC] PLC interface ready");
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
    
    // --- DEBOUNCE LOGIC ---
    if (current_read_byte == input_last_stable_byte) {
        // State is the same as the last stable state
        input_debounce_count++;
        
        if (input_debounce_count >= PLC_DEBOUNCE_REQUIRED_READS) {
            // Stability achieved: Update the official state only now
            plc_state.Q73_byte = current_read_byte;
            plc_state.status = PLC_OK;
            plc_state.last_read_ms = millis();
            
            // Lock debounce counter to prevent wrap-around
            input_debounce_count = PLC_DEBOUNCE_REQUIRED_READS; 
        }
    } else {
        // State changed or is bouncing: Reset counter and update the reference byte.
        // The official plc_state.Q73_byte remains UNCHANGED until debounce count is met.
        input_last_stable_byte = current_read_byte;
        input_debounce_count = 1;
        plc_state.status = PLC_TIMEOUT; // Temporarily mark as unstable/timeout while debounce is running
    }
    // --- END DEBOUNCE LOGIC ---

  } else {
    // I2C read failed or timed out: Mark error and reset debounce
    plc_state.error_count++;
    input_debounce_count = 0;
    
    plc_state.status = PLC_TIMEOUT; 
    if (result == I2C_RESULT_NACK) {
        plc_state.status = PLC_NOT_FOUND; 
    }
    
    logWarning("[PLC] Q73 read failed, I2C result: %s", i2cResultToString(result));
    faultLogWarning(FAULT_PLC_COMM_LOSS, "Q73 Consensus Read Failed");
  }
}

// ============================================================================
// Generic bit/byte operations (Deprecated)
// ============================================================================

bool plcGetBit(uint8_t bit) {
  // Q73 only (consenso feedback)
  if (bit >= 8) return false;
  return (plc_state.Q73_byte & (1 << bit)) != 0;
}

void plcSetBit(uint8_t bit, bool value) {
  Serial.println("[PLC] WARNING: plcSetBit() deprecated. Use ELBO functions.");
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
  Serial.println("[PLC] WARNING: plcSetByte() deprecated. Use ELBO functions.");
}

uint16_t plcGetWord(uint8_t offset) {
  return 0;
}

void plcSetWord(uint8_t offset, uint16_t value) {
  // Not used
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
  
  if (value) {
    plc_state.I72_byte &= ~(1 << speed_bit); // Set bit low (ON)
  } else {
    plc_state.I72_byte |= (1 << speed_bit);  // Set bit high (OFF)
  }
  
  i2c_result_t result = i2cWriteWithRetry(PCF8574_I72_ADDR, &plc_state.I72_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I2C Error writing I72 speed: %s", i2cResultToString(result));
    plc_state.error_count++;
    // --- FAULT LOGGING ---
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I72_ADDR,
                  "I72 Speed Write Failed (Bit %d, Result %d)", speed_bit, result);
    // -------------------------
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
  
  if (value) {
    plc_state.I73_byte &= ~(1 << axis_bit);
  } else {
    plc_state.I73_byte |= (1 << axis_bit);
  }
  
  i2c_result_t result = i2cWriteWithRetry(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I2C Error writing I73 axis: %s", i2cResultToString(result));
    plc_state.error_count++;
    // --- FAULT LOGGING ---
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR,
                  "I73 Axis Write Failed (Bit %d)", axis_bit);
    // -------------------------
    return false;
  }
}

bool elboI73GetDirection(uint8_t dir_bit) {
  if (dir_bit >= 8) return false;
  return (plc_state.I73_byte & (1 << dir_bit)) == 0;
}

bool elboI73SetDirection(uint8_t dir_bit, bool value) {
  if (dir_bit >= 8) return false;
  
  if (value) {
    plc_state.I73_byte &= ~(1 << dir_bit);
  } else {
    plc_state.I73_byte |= (1 << dir_bit);
  }
  
  i2c_result_t result = i2cWriteWithRetry(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
  if (result == I2C_RESULT_OK) {
    return true;
  } else {
    logError("[ELBO] I2C Error writing I73 direction: %s", i2cResultToString(result));
    plc_state.error_count++;
    // --- FAULT LOGGING ---
    faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, PCF8574_I73_ADDR,
                  "I73 Dir Write Failed (Bit %d)", dir_bit);
    // -------------------------
    return false;
  }
}

bool elboI73GetVSMode() {
  return (plc_state.I73_byte & (1 << ELBO_I73_V_S_MODE)) == 0;
}

bool elboI73SetVSMode(bool value) {
  if (value) {
    plc_state.I73_byte &= ~(1 << ELBO_I73_V_S_MODE);
  } else {
    plc_state.I73_byte |= (1 << ELBO_I73_V_S_MODE);
  }
  
  i2c_result_t result = i2cWriteWithRetry(PCF8574_I73_ADDR, &plc_state.I73_byte, 1);
  
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
// Diagnostics
// ============================================================================

plc_status_t plcGetStatus() {
  return plc_state.status;
}

uint32_t plcGetLastReadTime() {
  return plc_state.last_read_ms;
}

uint32_t plcGetErrorCount() {
  return plc_state.error_count;
}

void plcDiagnostics() {
  Serial.println("\n=== PLC Interface Diagnostics ===");
  Serial.printf("Status: %d (0=OK, 1=TIMEOUT, 2=NOT_FOUND)\n", plc_state.status);
  Serial.printf("Debounce Count: %d (Req: %d)\n", input_debounce_count, PLC_DEBOUNCE_REQUIRED_READS);
  Serial.printf("Errors: %d\n", plc_state.error_count);
  Serial.printf("Reads: %d\n", plc_state.read_count);
  Serial.printf("Last read: %d ms ago\n", millis() - plc_state.last_read_ms);
  
  Serial.printf("\nI72 Output (Speed): 0x%02X\n", plc_state.I72_byte);
  Serial.printf("  FAST (P0): %s\n", elboI72GetSpeed(ELBO_I72_FAST) ? "ON" : "OFF");
  Serial.printf("  MED (P1): %s\n", elboI72GetSpeed(ELBO_I72_MED) ? "ON" : "OFF");
  
  Serial.printf("\nI73 Output (Axis/Dir/Mode): 0x%02X\n", plc_state.I73_byte);
  Serial.printf("  AXIS_Y (P0): %s\n", elboI73GetAxis(ELBO_I73_AXIS_Y) ? "ON" : "OFF");
  Serial.printf("  AXIS_X (P1): %s\n", elboI73GetAxis(ELBO_I73_AXIS_X) ? "ON" : "OFF");
  Serial.printf("  AXIS_Z (P2): %s\n", elboI73GetAxis(ELBO_I73_AXIS_Z) ? "ON" : "OFF");
  Serial.printf("  DIR+ (P5): %s\n", elboI73GetDirection(ELBO_I73_DIRECTION_PLUS) ? "ON" : "OFF");
  Serial.printf("  DIR- (P6): %s\n", elboI73GetDirection(ELBO_I73_DIRECTION_MINUS) ? "ON" : "OFF");
  Serial.printf("  V/S (P7): %s\n", elboI73GetVSMode() ? "ON" : "OFF");
  
  Serial.printf("\nQ73 Input (Consenso): 0x%02X\n", plc_state.Q73_byte);
  Serial.printf("  Consenso Y (P0): %s\n", elboQ73GetConsenso(0) ? "READY" : "NOT READY");
  Serial.printf("  Consenso X (P1): %s\n", elboQ73GetConsenso(1) ? "READY" : "NOT READY");
  Serial.printf("  Consenso Z (P2): %s\n", elboQ73GetConsenso(2) ? "READY" : "NOT READY");
  Serial.printf("  Auto/Manual (P3): %s\n", elboQ73GetAutoManual() ? "AUTO" : "MANUAL");
  
  Serial.println("=== End Diagnostics ===\n");
}
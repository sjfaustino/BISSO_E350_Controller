#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include <Wire.h>

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
  0xFF,  // I72: all bits OFF (active-LOW, so FF = no outputs active)
  0xFF,  // I73: all bits OFF
  0x00,  // Q73: no consenso
  PLC_OK,
  0,
  0,
  0
};

void plcIfaceInit() {
  Serial.println("[PLC] PLC interface initializing...");
  Wire.begin(PLC_SDA_PIN, PLC_SCL_PIN, PLC_I2C_SPEED);
  
  plc_state.status = PLC_OK;
  plc_state.last_read_ms = millis();
  plc_state.error_count = 0;
  plc_state.read_count = 0;
  
  Serial.print("[PLC] I2C initialized: SDA=GPIO");
  Serial.print(PLC_SDA_PIN);
  Serial.print(" SCL=GPIO");
  Serial.print(PLC_SCL_PIN);
  Serial.print(" @ ");
  Serial.print(PLC_I2C_SPEED / 1000);
  Serial.println(" kHz");
  
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
  
  // Read consenso feedback from Q73 (PCF8574 @ 0x22)
  Wire.beginTransmission(PCF8574_Q73_ADDR);
  Wire.write(0x00);
  int result = Wire.endTransmission(false);
  
  if (result != 0) {
    plc_state.status = PLC_TIMEOUT;
    plc_state.error_count++;
    return;
  }
  
  // Read Q73 consenso byte (1 byte read)
  int bytes_read = Wire.requestFrom(PCF8574_Q73_ADDR, 1);
  if (bytes_read == 1) {
    plc_state.Q73_byte = Wire.read();
    plc_state.status = PLC_OK;
    plc_state.last_read_ms = millis();
    plc_state.read_count++;
  } else {
    plc_state.status = PLC_TIMEOUT;
    plc_state.error_count++;
  }
}

// ============================================================================
// Generic bit/byte operations (for compatibility)
// ============================================================================

bool plcGetBit(uint8_t bit) {
  // Q73 only (consenso feedback)
  if (bit >= 8) return false;
  return (plc_state.Q73_byte & (1 << bit)) != 0;
}

void plcSetBit(uint8_t bit, bool value) {
  // This is deprecated - use ELBO functions instead
  Serial.println("[PLC] WARNING: plcSetBit() deprecated. Use ELBO functions.");
}

uint8_t plcGetByte(uint8_t offset) {
  // offset 0 = Q73 (consenso input)
  // offset 1 = I72 (speed - for diagnostics)
  // offset 2 = I73 (axis/dir - for diagnostics)
  switch (offset) {
    case 0: return plc_state.Q73_byte;
    case 1: return plc_state.I72_byte;
    case 2: return plc_state.I73_byte;
    default: return 0;
  }
}

void plcSetByte(uint8_t offset, uint8_t value) {
  // offset 0 = I72 (speed control)
  // offset 1 = I73 (axis/direction/mode)
  if (offset == 0) {
    plc_state.I72_byte = value;
    Wire.beginTransmission(PCF8574_I72_ADDR);
    Wire.write(value);
    Wire.endTransmission();
  } else if (offset == 1) {
    plc_state.I73_byte = value;
    Wire.beginTransmission(PCF8574_I73_ADDR);
    Wire.write(value);
    Wire.endTransmission();
  }
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
    plc_state.I72_byte &= ~(1 << speed_bit);
  } else {
    plc_state.I72_byte |= (1 << speed_bit);
  }
  
  // Write to I2C and return status
  Wire.beginTransmission(PCF8574_I72_ADDR);
  Wire.write(plc_state.I72_byte);
  int result = Wire.endTransmission();
  
  if (result != 0) {
    logError("[ELBO] I2C Error writing I72: error code %d", result);
    plc_state.error_count++;
    
    // CRITICAL: Attempt I2C bus recovery on error
    logError("[ELBO] Attempting I2C bus recovery...");
    i2cRecoverBus();
    
    // Retry transmission once after recovery
    Wire.beginTransmission(PCF8574_I72_ADDR);
    Wire.write(plc_state.I72_byte);
    result = Wire.endTransmission();
    
    if (result != 0) {
      logError("[ELBO] I2C still failed after recovery: error code %d - I72 command lost!", result);
      return false;
    }
    
    logInfo("[ELBO] I2C recovered - I72 retransmitted successfully");
    return true;
  }
  
  return true;
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
  
  Wire.beginTransmission(PCF8574_I73_ADDR);
  Wire.write(plc_state.I73_byte);
  int result = Wire.endTransmission();
  
  if (result != 0) {
    logError("[ELBO] I2C Error writing I73 axis bit: error code %d", result);
    plc_state.error_count++;
    
    // CRITICAL: Attempt I2C bus recovery
    logError("[ELBO] Attempting I2C bus recovery...");
    i2cRecoverBus();
    
    // Retry once after recovery
    Wire.beginTransmission(PCF8574_I73_ADDR);
    Wire.write(plc_state.I73_byte);
    result = Wire.endTransmission();
    
    if (result != 0) {
      logError("[ELBO] I2C still failed after recovery: error code %d - I73 axis command lost!", result);
      return false;
    }
    
    logInfo("[ELBO] I2C recovered - I73 axis retransmitted successfully");
    return true;
  }
  
  return true;
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
  
  Wire.beginTransmission(PCF8574_I73_ADDR);
  Wire.write(plc_state.I73_byte);
  int result = Wire.endTransmission();
  
  if (result != 0) {
    logError("[ELBO] I2C Error writing I73 direction bit: error code %d", result);
    plc_state.error_count++;
    
    // CRITICAL: Attempt I2C bus recovery
    logError("[ELBO] Attempting I2C bus recovery...");
    i2cRecoverBus();
    
    // Retry once
    Wire.beginTransmission(PCF8574_I73_ADDR);
    Wire.write(plc_state.I73_byte);
    result = Wire.endTransmission();
    
    if (result != 0) {
      logError("[ELBO] I2C still failed after recovery: error code %d - I73 direction command lost!", result);
      return false;
    }
    
    logInfo("[ELBO] I2C recovered - I73 direction retransmitted successfully");
    return true;
  }
  
  return true;
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
  
  Wire.beginTransmission(PCF8574_I73_ADDR);
  Wire.write(plc_state.I73_byte);
  int result = Wire.endTransmission();
  
  if (result != 0) {
    logError("[ELBO] I2C Error writing I73 V/S mode: error code %d", result);
    plc_state.error_count++;
    
    // CRITICAL: Attempt I2C bus recovery
    logError("[ELBO] Attempting I2C bus recovery...");
    i2cRecoverBus();
    
    // Retry once
    Wire.beginTransmission(PCF8574_I73_ADDR);
    Wire.write(plc_state.I73_byte);
    result = Wire.endTransmission();
    
    if (result != 0) {
      logError("[ELBO] I2C still failed after recovery: error code %d - I73 V/S command lost!", result);
      return false;
    }
    
    logInfo("[ELBO] I2C recovered - I73 V/S mode retransmitted successfully");
    return true;
  }
  
  return true;
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

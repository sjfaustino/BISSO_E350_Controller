#include "plc_iface.h"
#include <Wire.h>

struct {
  uint8_t input_byte[4];
  uint8_t output_byte[4];
  plc_status_t status;
  uint32_t last_read_ms;
  uint32_t error_count;
  uint32_t read_count;
} plc_state = {
  {0, 0, 0, 0},
  {0, 0, 0, 0},
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
  
  // Initialize output bytes
  memset(plc_state.output_byte, 0, sizeof(plc_state.output_byte));
  
  Serial.print("[PLC] I2C initialized: SDA=GPIO");
  Serial.print(PLC_SDA_PIN);
  Serial.print(" SCL=GPIO");
  Serial.print(PLC_SCL_PIN);
  Serial.print(" @ ");
  Serial.print(PLC_I2C_SPEED / 1000);
  Serial.println(" kHz");
  Serial.println("[PLC] PLC interface ready");
}

void plcIfaceUpdate() {
  static uint32_t last_read = 0;
  if (millis() - last_read < PLC_READ_INTERVAL_MS) {
    return;
  }
  last_read = millis();
  
  // Attempt to read from PLC
  Wire.beginTransmission(PLC_I2C_ADDR);
  Wire.write(0x00); // Read from register 0
  int result = Wire.endTransmission(false);
  
  if (result != 0) {
    plc_state.status = PLC_TIMEOUT;
    plc_state.error_count++;
    return;
  }
  
  // Read data
  int bytes_read = Wire.requestFrom(PLC_I2C_ADDR, 4);
  if (bytes_read == 4) {
    for (int i = 0; i < 4; i++) {
      plc_state.input_byte[i] = Wire.read();
    }
    plc_state.status = PLC_OK;
    plc_state.last_read_ms = millis();
    plc_state.read_count++;
  } else {
    plc_state.status = PLC_TIMEOUT;
    plc_state.error_count++;
  }
}

bool plcGetBit(uint8_t bit) {
  if (bit >= 32) return false;
  uint8_t byte_idx = bit / 8;
  uint8_t bit_idx = bit % 8;
  return (plc_state.input_byte[byte_idx] & (1 << bit_idx)) != 0;
}

void plcSetBit(uint8_t bit, bool value) {
  if (bit >= 32) return;
  uint8_t byte_idx = bit / 8;
  uint8_t bit_idx = bit % 8;
  
  if (value) {
    plc_state.output_byte[byte_idx] |= (1 << bit_idx);
  } else {
    plc_state.output_byte[byte_idx] &= ~(1 << bit_idx);
  }
  
  // Write to PLC
  Wire.beginTransmission(PLC_I2C_ADDR);
  Wire.write(0x20); // Write to register 0x20
  Wire.write(plc_state.output_byte[byte_idx]);
  Wire.endTransmission();
}

uint8_t plcGetByte(uint8_t offset) {
  if (offset < 4) return plc_state.input_byte[offset];
  return 0;
}

void plcSetByte(uint8_t offset, uint8_t value) {
  if (offset >= 4) return;
  plc_state.output_byte[offset] = value;
  
  Wire.beginTransmission(PLC_I2C_ADDR);
  Wire.write(0x20 + offset);
  Wire.write(value);
  Wire.endTransmission();
}

uint16_t plcGetWord(uint8_t offset) {
  if (offset + 1 < 4) {
    return (plc_state.input_byte[offset + 1] << 8) | plc_state.input_byte[offset];
  }
  return 0;
}

void plcSetWord(uint8_t offset, uint16_t value) {
  if (offset + 1 >= 4) return;
  plcSetByte(offset, value & 0xFF);
  plcSetByte(offset + 1, (value >> 8) & 0xFF);
}

plc_status_t plcGetStatus() {
  return plc_state.status;
}

uint32_t plcGetLastReadTime() {
  return millis() - plc_state.last_read_ms;
}

uint32_t plcGetErrorCount() {
  return plc_state.error_count;
}

void plcDiagnostics() {
  Serial.println("\n[PLC] === PLC Interface Diagnostics ===");
  Serial.print("Status: ");
  switch(plc_state.status) {
    case PLC_OK: Serial.println("OK"); break;
    case PLC_TIMEOUT: Serial.println("TIMEOUT"); break;
    case PLC_NOT_FOUND: Serial.println("NOT_FOUND"); break;
    case PLC_CRC_ERROR: Serial.println("CRC_ERROR"); break;
    case PLC_INVALID_DATA: Serial.println("INVALID_DATA"); break;
    default: Serial.println("UNKNOWN");
  }
  
  Serial.print("Address: 0x");
  Serial.println(PLC_I2C_ADDR, HEX);
  Serial.print("Last Read: ");
  Serial.print(plcGetLastReadTime());
  Serial.println(" ms ago");
  
  Serial.print("Read Count: ");
  Serial.println(plc_state.read_count);
  Serial.print("Error Count: ");
  Serial.println(plc_state.error_count);
  
  Serial.println("Input Bytes:");
  for (int i = 0; i < 4; i++) {
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] = 0x");
    Serial.println(plc_state.input_byte[i], HEX);
  }
  
  Serial.println("Output Bytes:");
  for (int i = 0; i < 4; i++) {
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] = 0x");
    Serial.println(plc_state.output_byte[i], HEX);
  }
}

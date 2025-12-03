#include "i2c_bus_recovery.h"
#include "fault_logging.h"
#include "system_constants.h" 
#include <Wire.h>

#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

static i2c_stats_t stats = {0};
static i2c_retry_config_t retry_config = {3, 10, 100, 2.0f};

const char* i2cResultToString(i2c_result_t result) {
  switch(result) {
    case I2C_RESULT_OK: return "OK";
    case I2C_RESULT_NACK: return "NACK";
    case I2C_RESULT_TIMEOUT: return "TIMEOUT";
    case I2C_RESULT_BUS_ERROR: return "BUS_ERROR";
    case I2C_RESULT_ARBITRATION_LOST: return "ARB_LOST";
    case I2C_RESULT_DEVICE_BUSY: return "BUSY";
    case I2C_RESULT_UNKNOWN_ERROR: return "UNKNOWN";
    default: return "???";
  }
}

const char* i2cBusStatusToString(i2c_bus_status_t status) {
  switch(status) {
    case I2C_BUS_OK: return "OK";
    case I2C_BUS_BUSY: return "BUSY";
    case I2C_BUS_STUCK_SDA: return "STUCK_SDA";
    case I2C_BUS_STUCK_SCL: return "STUCK_SCL";
    case I2C_BUS_ERROR: return "ERROR";
    case I2C_BUS_TIMEOUT: return "TIMEOUT";
    default: return "UNKNOWN";
  }
}

void i2cRecoveryInit() {
  Serial.println("[I2C] Initializing recovery system...");
  i2c_bus_status_t status = i2cCheckBusStatus();
  if (status != I2C_BUS_OK) {
    Serial.println("[I2C] [WARN] Bus error detected at boot. Recovering...");
    i2cRecoverBus();
  }
  memset(&stats, 0, sizeof(stats));
  Serial.println("[I2C] [OK] Ready");
}

i2c_bus_status_t i2cCheckBusStatus() {
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  delay(10);
  bool sda_level = digitalRead(I2C_SDA_PIN);
  bool scl_level = digitalRead(I2C_SCL_PIN);
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  
  if (!scl_level && !sda_level) return I2C_BUS_STUCK_SDA; 
  else if (!scl_level) return I2C_BUS_STUCK_SCL; 
  else if (!sda_level) return I2C_BUS_STUCK_SDA; 
  if (Wire.available()) return I2C_BUS_BUSY;
  return I2C_BUS_OK;
}

void i2cRecoverBus() {
  Serial.println("[I2C] Executing recovery sequence...");
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  
  for (int i = 0; i < 9; i++) {
    pinMode(I2C_SCL_PIN, OUTPUT);
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(5);
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);
    delayMicroseconds(5);
    if (digitalRead(I2C_SDA_PIN)) break;
  }
  
  pinMode(I2C_SDA_PIN, OUTPUT); digitalWrite(I2C_SDA_PIN, LOW); delayMicroseconds(5);
  pinMode(I2C_SCL_PIN, OUTPUT); digitalWrite(I2C_SCL_PIN, LOW); delayMicroseconds(5);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP); delayMicroseconds(5);
  pinMode(I2C_SDA_PIN, INPUT_PULLUP); delayMicroseconds(5);
  
  Serial.println("[I2C] [OK] Recovery complete.");
  stats.bus_recoveries++;
  i2cSoftReset();
}

void i2cSoftReset() {
  Serial.println("[I2C] Soft reset...");
  Wire.end();
  delay(10);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000); 
  Serial.println("[I2C] [OK] Reset done.");
}

void i2cHardReset() {
  Serial.println("[I2C] Hard reset...");
  Wire.end();
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  delay(100);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  Serial.println("[I2C] [OK] Reset done.");
}

i2c_result_t i2cTransactionWithRetry(uint8_t address, uint8_t* data, uint8_t len, bool read) {
  uint16_t backoff_ms = retry_config.initial_backoff_ms;
  i2c_result_t result = I2C_RESULT_UNKNOWN_ERROR;
  
  for (uint8_t attempt = 0; attempt <= retry_config.max_retries; attempt++) {
    stats.transactions_total++;
    if (attempt > 0) {
      stats.retries_performed++;
      if (i2cCheckBusStatus() != I2C_BUS_OK) i2cRecoverBus();
      delay(backoff_ms);
      backoff_ms = (uint16_t)((float)backoff_ms * retry_config.backoff_multiplier);
      if (backoff_ms > retry_config.max_backoff_ms) backoff_ms = retry_config.max_backoff_ms;
    }
    
    Wire.beginTransmission(address);
    if (read) {
      Wire.requestFrom(address, len);
      if (Wire.available() >= len) {
        for (uint8_t i = 0; i < len; i++) data[i] = Wire.read();
        result = I2C_RESULT_OK;
        stats.transactions_success++;
        return result;
      }
    } else {
      Wire.write(data, len);
      uint8_t error = Wire.endTransmission(true);
      if (error == 0) {
        result = I2C_RESULT_OK;
        stats.transactions_success++;
        return result;
      } else {
          if (error == 1) { result = I2C_RESULT_BUS_ERROR; stats.error_bus++; }
          else if (error == 2) { result = I2C_RESULT_NACK; stats.error_nack++; }
          else if (error == 3) { result = I2C_RESULT_ARBITRATION_LOST; stats.error_arbitration++; }
          else if (error == 4) { result = I2C_RESULT_TIMEOUT; stats.error_timeout++; }
      }
    }
  }
  stats.transactions_failed++;
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, result, "I2C Fail 0x%02X: %s", address, i2cResultToString(result));
  if (i2cCheckBusStatus() != I2C_BUS_OK) i2cRecoverBus();
  return result;
}

i2c_result_t i2cWriteWithRetry(uint8_t address, const uint8_t* data, uint8_t len) {
  uint8_t buffer[len];
  memcpy(buffer, data, len);
  return i2cTransactionWithRetry(address, buffer, len, false);
}

i2c_result_t i2cReadWithRetry(uint8_t address, uint8_t* data, uint8_t len) {
  return i2cTransactionWithRetry(address, data, len, true);
}

i2c_result_t i2cWriteFast(uint8_t address, const uint8_t* data, uint8_t len) {
    stats.transactions_total++;
    Wire.setTimeOut(2); // 2ms timeout for real-time
    Wire.beginTransmission(address);
    Wire.write(data, len);
    uint8_t error = Wire.endTransmission(true);
    Wire.setTimeOut(I2C_TRANSACTION_TIMEOUT_MS); // Restore default
    
    if (error == 0) {
        stats.transactions_success++;
        return I2C_RESULT_OK;
    }
    stats.transactions_failed++;
    if (error == 4) stats.error_timeout++;
    else if (error == 2) stats.error_nack++;
    return (error == 2) ? I2C_RESULT_NACK : I2C_RESULT_TIMEOUT;
}

void i2cSetRetryConfig(i2c_retry_config_t config) {
  retry_config = config;
  Serial.println("[I2C] Retry configuration updated");
}

i2c_retry_config_t i2cGetRetryConfig() {
  return retry_config;
}

void i2cResetStats() {
  memset(&stats, 0, sizeof(stats));
  Serial.println("[I2C] Statistics reset");
}

i2c_stats_t i2cGetStats() {
  if (stats.transactions_total > 0) stats.success_rate = (float)stats.transactions_success / stats.transactions_total * 100.0f;
  return stats;
}

void i2cShowStats() {
  i2c_stats_t current = i2cGetStats();
  Serial.println("\n[I2C] === Statistics ===");
  Serial.printf("Total: %lu | OK: %lu (%.1f%%) | Fail: %lu\n", 
    current.transactions_total, current.transactions_success, current.success_rate, current.transactions_failed);
  Serial.printf("Retries: %lu | Recoveries: %lu\n", current.retries_performed, current.bus_recoveries);
  Serial.printf("Errors: NACK=%lu Time=%lu Bus=%lu Arb=%lu\n", 
    current.error_nack, current.error_timeout, current.error_bus, current.error_arbitration);
}

void i2cMonitorBusHealth() {
  i2c_bus_status_t status = i2cCheckBusStatus();
  if (status != I2C_BUS_OK) {
    faultLogEntry(FAULT_WARNING, FAULT_PLC_COMM_LOSS, -1, status, "I2C Degraded: %s", i2cBusStatusToString(status));
  }
}
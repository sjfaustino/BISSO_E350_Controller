#include "i2c_bus_recovery.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_constants.h" 
#include <Wire.h>

// ESP32-S3 I²C pins (KC868-A16 Standard)
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

static i2c_stats_t stats = {0};
static i2c_retry_config_t retry_config = {
  .max_retries = 3,
  .initial_backoff_ms = 10,
  .max_backoff_ms = 100,
  .backoff_multiplier = 2.0f
};

const char* i2cResultToString(i2c_result_t result) {
  switch(result) {
    case I2C_RESULT_OK: return "OK";
    case I2C_RESULT_NACK: return "NACK";
    case I2C_RESULT_TIMEOUT: return "TIMEOUT";
    case I2C_RESULT_BUS_ERROR: return "BUS_ERROR";
    case I2C_RESULT_ARBITRATION_LOST: return "ARBITRATION_LOST";
    case I2C_RESULT_DEVICE_BUSY: return "DEVICE_BUSY";
    case I2C_RESULT_UNKNOWN_ERROR: return "UNKNOWN_ERROR";
    default: return "UNDEFINED";
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
  logInfo("[I2C] Initializing recovery system...");
  
  // Check initial bus status
  i2c_bus_status_t status = i2cCheckBusStatus();
  if (status != I2C_BUS_OK) {
    logWarning("[I2C] Bus error detected at boot (%s). Attempting recovery...", i2cBusStatusToString(status));
    i2cRecoverBus();
  }
  
  memset(&stats, 0, sizeof(stats));
  logInfo("[I2C] Ready");
}

i2c_bus_status_t i2cCheckBusStatus() {
  // Check if bus lines are stuck
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  
  delay(10);
  
  bool sda_level = digitalRead(I2C_SDA_PIN);
  bool scl_level = digitalRead(I2C_SCL_PIN);
  
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  
  // Analyze bus state
  if (!scl_level && !sda_level) {
    return I2C_BUS_STUCK_SDA;  // Both stuck low (usually implies SDA stuck)
  } else if (!scl_level) {
    return I2C_BUS_STUCK_SCL;  // SCL stuck low
  } else if (!sda_level) {
    return I2C_BUS_STUCK_SDA;  // SDA stuck low
  }
  
  // Check if Wire library thinks it's busy
  if (Wire.available()) {
    return I2C_BUS_BUSY;
  }
  
  return I2C_BUS_OK;
}

void i2cRecoverBus() {
  logInfo("[I2C] Executing bus recovery procedure...");
  
  // Release bus lines
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  
  // Clock pulses to recover stuck SDA (up to 9 clocks)
  for (int i = 0; i < 9; i++) {
    pinMode(I2C_SCL_PIN, OUTPUT);
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(5);
    
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);
    delayMicroseconds(5);
    
    // Check if SDA released
    if (digitalRead(I2C_SDA_PIN)) {
      break;
    }
  }
  
  // Generate STOP condition
  pinMode(I2C_SDA_PIN, OUTPUT);
  digitalWrite(I2C_SDA_PIN, LOW);
  delayMicroseconds(5);
  
  pinMode(I2C_SCL_PIN, OUTPUT);
  digitalWrite(I2C_SCL_PIN, LOW);
  delayMicroseconds(5);
  
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  delayMicroseconds(5);
  
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  delayMicroseconds(5);
  
  logInfo("[I2C] Recovery sequence complete.");
  stats.bus_recoveries++;
  
  // Reinitialize Wire
  i2cSoftReset();
}

void i2cSoftReset() {
  logInfo("[I2C] Performing soft reset...");
  Wire.end();
  delay(10);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000); 
  logInfo("[I2C] Soft reset complete");
}

void i2cHardReset() {
  logInfo("[I2C] Performing hard reset...");
  Wire.end();
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  delay(100);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  logInfo("[I2C] Hard reset complete");
}

i2c_result_t i2cTransactionWithRetry(uint8_t address, uint8_t* data, uint8_t len, bool read) {
  uint16_t backoff_ms = retry_config.initial_backoff_ms;
  i2c_result_t result = I2C_RESULT_UNKNOWN_ERROR;
  
  for (uint8_t attempt = 0; attempt <= retry_config.max_retries; attempt++) {
    stats.transactions_total++;
    
    if (attempt > 0) {
      stats.retries_performed++;
      
      // Check bus status before retry
      i2c_bus_status_t bus_status = i2cCheckBusStatus();
      if (bus_status != I2C_BUS_OK) {
        logWarning("[I2C] Bus error (%s) - recovering", i2cBusStatusToString(bus_status));
        i2cRecoverBus();
      }
      
      // Exponential backoff
      delay(backoff_ms);
      backoff_ms = (uint16_t)((float)backoff_ms * retry_config.backoff_multiplier);
      if (backoff_ms > retry_config.max_backoff_ms) {
        backoff_ms = retry_config.max_backoff_ms;
      }
    }
    
    Wire.beginTransmission(address);
    
    if (read) {
      Wire.requestFrom(address, len);
      if (Wire.available() >= len) {
        for (uint8_t i = 0; i < len; i++) {
          data[i] = Wire.read();
        }
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
      } else if (error == 1) {
        result = I2C_RESULT_BUS_ERROR;
        stats.error_bus++;
      } else if (error == 2) {
        result = I2C_RESULT_NACK;
        stats.error_nack++;
      } else if (error == 3) {
        result = I2C_RESULT_ARBITRATION_LOST;
        stats.error_arbitration++;
      } else if (error == 4) {
        result = I2C_RESULT_TIMEOUT;
        stats.error_timeout++;
      } else {
        result = I2C_RESULT_UNKNOWN_ERROR;
      }
    }
  }
  
  // All retries exhausted
  stats.transactions_failed++;
  
  // Log detailed fault
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, result, 
                "I2C Fail 0x%02X: %s (Retries: %d)", 
                address, i2cResultToString(result), retry_config.max_retries);
  
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

// --- FAST WRITE for Real-Time Tasks ---
// 2ms timeout, no retries, no blocking recovery
i2c_result_t i2cWriteFast(uint8_t address, const uint8_t* data, uint8_t len) {
    stats.transactions_total++;
    
    // Set extremely short timeout for real-time safety
    Wire.setTimeOut(2); 
    
    Wire.beginTransmission(address);
    Wire.write(data, len);
    uint8_t error = Wire.endTransmission(true);
    
    // Restore standard timeout from system constants
    Wire.setTimeOut(I2C_TRANSACTION_TIMEOUT_MS); 
    
    if (error == 0) {
        stats.transactions_success++;
        return I2C_RESULT_OK;
    }
    
    stats.transactions_failed++;
    
    // Map Wire errors
    if (error == 4) stats.error_timeout++;
    else if (error == 2) stats.error_nack++;
    else if (error == 1) stats.error_bus++;
    
    // Return simplified result for caller
    if (error == 2) return I2C_RESULT_NACK;
    if (error == 4) return I2C_RESULT_TIMEOUT;
    return I2C_RESULT_BUS_ERROR;
}

void i2cSetRetryConfig(i2c_retry_config_t config) {
  retry_config = config;
  logInfo("[I2C] Retry config updated");
}

i2c_retry_config_t i2cGetRetryConfig() {
  return retry_config;
}

void i2cResetStats() {
  memset(&stats, 0, sizeof(stats));
  logInfo("[I2C] Stats reset");
}

i2c_stats_t i2cGetStats() {
  if (stats.transactions_total > 0) {
    stats.success_rate = (float)stats.transactions_success / stats.transactions_total * 100.0f;
  } else {
    stats.success_rate = 0.0f;
  }
  return stats;
}

void i2cShowStats() {
  i2c_stats_t current = i2cGetStats();
  
  Serial.println("\n=== I2C STATISTICS ===");
  Serial.printf("Total: %lu | OK: %lu (%.1f%%) | Fail: %lu\n", 
    current.transactions_total, current.transactions_success, current.success_rate, current.transactions_failed);
  
  Serial.printf("Retries: %lu | Recoveries: %lu\n", 
    current.retries_performed, current.bus_recoveries);
  
  Serial.printf("Errors: NACK=%lu Time=%lu Bus=%lu Arb=%lu\n", 
    current.error_nack, current.error_timeout, current.error_bus, current.error_arbitration);
}

void i2cMonitorBusHealth() {
  i2c_bus_status_t status = i2cCheckBusStatus();
  if (status != I2C_BUS_OK) {
    faultLogEntry(FAULT_WARNING, FAULT_PLC_COMM_LOSS, -1, status, 
                  "I2C Degraded: %s", i2cBusStatusToString(status));
  }
}
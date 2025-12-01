#include "i2c_bus_recovery.h"
#include "fault_logging.h"
#include <Wire.h>

// ESP32-S3 I²C pins
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
  Serial.println("[I2C] I²C Bus Recovery system initializing...");
  
  // Check initial bus status
  i2c_bus_status_t status = i2cCheckBusStatus();
  Serial.print("[I2C] Initial bus status: ");
  Serial.println(i2cBusStatusToString(status));
  
  if (status != I2C_BUS_OK) {
    Serial.println("[I2C] Bus error detected during init - attempting recovery...");
    i2cRecoverBus();
  }
  
  memset(&stats, 0, sizeof(stats));
  Serial.println("[I2C] ✅ I²C Bus Recovery ready");
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
    return I2C_BUS_STUCK_SDA;  // Both stuck low
  } else if (!scl_level) {
    return I2C_BUS_STUCK_SCL;  // SCL stuck low
  } else if (!sda_level) {
    return I2C_BUS_STUCK_SDA;  // SDA stuck low
  }
  
  // Check if Wire is ready
  if (Wire.available()) {
    return I2C_BUS_BUSY;
  }
  
  return I2C_BUS_OK;
}

void i2cRecoverBus() {
  Serial.println("[I2C] Executing I²C bus recovery procedure...");
  
  // Save current pin state
  uint8_t sda_pin = I2C_SDA_PIN;
  uint8_t scl_pin = I2C_SCL_PIN;
  
  // Release bus lines
  pinMode(sda_pin, INPUT_PULLUP);
  pinMode(scl_pin, INPUT_PULLUP);
  
  // Clock pulses to recover stuck SDA
  for (int i = 0; i < 9; i++) {
    pinMode(scl_pin, OUTPUT);
    digitalWrite(scl_pin, LOW);
    delayMicroseconds(5);
    
    pinMode(scl_pin, INPUT_PULLUP);
    delayMicroseconds(5);
    
    // Check if SDA released
    if (digitalRead(sda_pin)) {
      break;
    }
  }
  
  // Generate STOP condition
  pinMode(sda_pin, OUTPUT);
  digitalWrite(sda_pin, LOW);
  delayMicroseconds(5);
  
  pinMode(scl_pin, OUTPUT);
  digitalWrite(scl_pin, LOW);
  delayMicroseconds(5);
  
  pinMode(scl_pin, INPUT_PULLUP);
  delayMicroseconds(5);
  
  pinMode(sda_pin, INPUT_PULLUP);
  delayMicroseconds(5);
  
  Serial.println("[I2C] ✅ Bus recovery complete");
  stats.bus_recoveries++;
  
  // Reinitialize Wire
  i2cSoftReset();
}

void i2cSoftReset() {
  Serial.println("[I2C] Performing soft reset...");
  
  // End current Wire session
  Wire.end();
  
  // Small delay
  delay(10);
  
  // Reinitialize with standard configuration
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);  // 100kHz
  
  Serial.println("[I2C] ✅ Soft reset complete");
}

void i2cHardReset() {
  Serial.println("[I2C] Performing hard reset...");
  
  // End Wire
  Wire.end();
  
  // Reset pins
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  
  // Wait
  delay(100);
  
  // Reinitialize
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  
  Serial.println("[I2C] ✅ Hard reset complete");
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
        Serial.print("[I2C] Bus error detected (");
        Serial.print(i2cBusStatusToString(bus_status));
        Serial.println(") - attempting recovery");
        i2cRecoverBus();
      }
      
      // Exponential backoff
      delay(backoff_ms);
      backoff_ms = (uint16_t)((float)backoff_ms * retry_config.backoff_multiplier);
      if (backoff_ms > retry_config.max_backoff_ms) {
        backoff_ms = retry_config.max_backoff_ms;
      }
    }
    
    // Attempt transaction
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
  
  // Log detailed fault using the new API
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, result, 
                "I²C transaction failed (addr=0x%02X, retries=%d) - Result: %s", 
                address, retry_config.max_retries, i2cResultToString(result));
  
  // Check bus status on failure
  i2c_bus_status_t bus_status = i2cCheckBusStatus();
  if (bus_status != I2C_BUS_OK) {
    Serial.print("[I2C] WARNING: Bus status degraded - ");
    Serial.println(i2cBusStatusToString(bus_status));
    
    // Trigger recovery for bus errors
    if (bus_status == I2C_BUS_STUCK_SDA || bus_status == I2C_BUS_STUCK_SCL) {
      Serial.println("[I2C] Initiating bus recovery...");
      i2cRecoverBus();
    }
  }
  
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
  if (stats.transactions_total > 0) {
    stats.success_rate = (float)stats.transactions_success / stats.transactions_total * 100.0f;
  } else {
    stats.success_rate = 0.0f;
  }
  return stats;
}

void i2cShowStats() {
  i2c_stats_t current = i2cGetStats();
  
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║              I²C BUS STATISTICS & DIAGNOSTICS                  ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("[I2C_STATS] Transaction Summary:");
  Serial.print("  Total Transactions: ");
  Serial.println(current.transactions_total);
  
  Serial.print("  Successful: ");
  Serial.print(current.transactions_success);
  Serial.print(" (");
  Serial.print(current.success_rate);
  Serial.println("%)");
  
  Serial.print("  Failed: ");
  Serial.println(current.transactions_failed);
  
  Serial.println("\n[I2C_STATS] Retry Information:");
  Serial.print("  Total Retries: ");
  Serial.println(current.retries_performed);
  
  Serial.print("  Bus Recoveries: ");
  Serial.println(current.bus_recoveries);
  
  Serial.println("\n[I2C_STATS] Error Breakdown:");
  Serial.print("  NACK Errors: ");
  Serial.println(current.error_nack);
  
  Serial.print("  Timeout Errors: ");
  Serial.println(current.error_timeout);
  
  Serial.print("  Bus Errors: ");
  Serial.println(current.error_bus);
  
  Serial.print("  Arbitration Loss: ");
  Serial.println(current.error_arbitration);
  
  Serial.println("\n[I2C_STATS] Retry Configuration:");
  Serial.print("  Max Retries: ");
  Serial.println(retry_config.max_retries);
  
  Serial.print("  Initial Backoff: ");
  Serial.print(retry_config.initial_backoff_ms);
  Serial.println(" ms");
  
  Serial.print("  Max Backoff: ");
  Serial.print(retry_config.max_backoff_ms);
  Serial.println(" ms");
  
  Serial.print("  Backoff Multiplier: ");
  Serial.println(retry_config.backoff_multiplier);
  
  // Health assessment
  Serial.println("\n[I2C_STATS] Bus Health Assessment:");
  i2c_bus_status_t status = i2cCheckBusStatus();
  Serial.print("  Current Status: ");
  Serial.println(i2cBusStatusToString(status));
  
  if (current.success_rate > 99.0f) {
    Serial.println("  Overall Health: ✅ EXCELLENT");
  } else if (current.success_rate > 95.0f) {
    Serial.println("  Overall Health: ✅ GOOD");
  } else if (current.success_rate > 90.0f) {
    Serial.println("  Overall Health: ⚠️  WARNING");
  } else {
    Serial.println("  Overall Health: ❌ CRITICAL");
  }
  
  Serial.println();
}

void i2cMonitorBusHealth() {
  // Periodic health check
  i2c_bus_status_t status = i2cCheckBusStatus();
  
  if (status != I2C_BUS_OK) {
    faultLogEntry(FAULT_WARNING, FAULT_PLC_COMM_LOSS, -1, status, 
                  "I²C bus degraded: %s", 
                  i2cBusStatusToString(status));
  }
}
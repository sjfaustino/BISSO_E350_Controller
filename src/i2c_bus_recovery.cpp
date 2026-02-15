#include "i2c_bus_recovery.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include <Wire.h>

// FIX: Fully initialized struct to suppress -Wmissing-field-initializers
static i2c_stats_t stats = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f};
// Reduced retries to prevent bus starvation when devices are missing
// 1 retry, 5ms initial backoff, 20ms max, 2.0x multiplier
static i2c_retry_config_t retry_config = {1, 5, 20, 2.0f};

const char *i2cResultToString(i2c_result_t result) {
  switch (result) {
  case I2C_RESULT_OK:
    return "OK";
  case I2C_RESULT_NACK:
    return "NACK";
  case I2C_RESULT_TIMEOUT:
    return "TIMEOUT";
  case I2C_RESULT_BUS_ERROR:
    return "BUS_ERROR";
  case I2C_RESULT_ARBITRATION_LOST:
    return "ARB_LOST";
  case I2C_RESULT_DEVICE_BUSY:
    return "BUSY";
  case I2C_RESULT_UNKNOWN_ERROR:
    return "UNKNOWN";
  default:
    return "???";
  }
}

const char *i2cBusStatusToString(i2c_bus_status_t status) {
  switch (status) {
  case I2C_BUS_OK:
    return "OK";
  case I2C_BUS_BUSY:
    return "BUSY";
  case I2C_BUS_STUCK_SDA:
    return "STUCK_SDA";
  case I2C_BUS_STUCK_SCL:
    return "STUCK_SCL";
  case I2C_BUS_ERROR:
    return "ERROR";
  case I2C_BUS_TIMEOUT:
    return "TIMEOUT";
  default:
    return "UNKNOWN";
  }
}

void i2cRecoveryInit() {
  logInfo("[I2C] Initializing recovery system...");
  i2c_bus_status_t status = i2cCheckBusStatus();
  if (status != I2C_BUS_OK) {
    logWarning("[I2C] Bus error at boot. Recovering...");
    i2cRecoverBus();
  }
  memset(&stats, 0, sizeof(stats));
  logInfo("[I2C] Ready");
}

i2c_bus_status_t i2cCheckBusStatus() {
  // FIX: Don't call pinMode() on I2C pins - this disrupts Wire library
  // Instead, just check if the bus is in a good state by probing a known device
  // A simple beginTransmission/endTransmission to a known device is sufficient

  // Quick probe to PLC output board (usually present on KC868-A16)
  // NOTE: On a vanilla ESP32 devkit with no I2C devices, this will return NACK (2).
  //       This is NORMAL and does NOT indicate a bus error.
  Wire.beginTransmission(0x24); // ADDR_Q73_OUTPUT
  uint8_t err = Wire.endTransmission();

  if (err == 0) {
    return I2C_BUS_OK;
  } else if (err == 2) {
    // NACK - device not present, but bus is OK
    return I2C_BUS_OK;
  } else if (err == 4) {
    // Unknown error - bus might be stuck
    return I2C_BUS_STUCK_SDA;
  }

  return I2C_BUS_ERROR;
}

void i2cRecoverBus() {
  logInfo("[I2C] Executing recovery...");

  // PHASE 5.10: Signal I2C error event before recovery
  systemEventsSystemSet(EVENT_SYSTEM_I2C_ERROR);

  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);

  // Perform 9 clock pulses to clear stuck SDA
  // NOTE: delayMicroseconds() is used for precise I2C bit-banging. 
  // Blocking for ~150us is intentional and acceptable for this rare recovery event.
  for (int i = 0; i < 9; i++) {
    pinMode(PIN_I2C_SCL, OUTPUT);
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(5);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(5);
    if (digitalRead(PIN_I2C_SDA))
      break;
  }

  pinMode(PIN_I2C_SDA, OUTPUT);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  pinMode(PIN_I2C_SCL, OUTPUT);
  digitalWrite(PIN_I2C_SCL, LOW);
  delayMicroseconds(5);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(5);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  delayMicroseconds(5);

  logInfo("[I2C] Recovery complete.");
  stats.bus_recoveries++;
  i2cSoftReset();

  // PHASE 5.10: Clear I2C error event after successful recovery
  systemEventsSystemClear(EVENT_SYSTEM_I2C_ERROR);
}

void i2cSoftReset() {
  logInfo("[I2C] Soft reset...");
  Wire.end();
  delay(10);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  logInfo("[I2C] Reset done.");
}

void i2cHardReset() {
  logInfo("[I2C] Hard reset...");
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT);
  pinMode(PIN_I2C_SCL, INPUT);
  delay(100);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  logInfo("[I2C] Reset done.");
}

i2c_result_t i2cTransactionWithRetry(uint8_t address, uint8_t *data,
                                     uint8_t len, bool read) {
  uint16_t backoff_ms = retry_config.initial_backoff_ms;
  i2c_result_t result = I2C_RESULT_UNKNOWN_ERROR;

  for (uint8_t attempt = 0; attempt <= retry_config.max_retries; attempt++) {
    stats.transactions_total++;
    if (attempt > 0) {
      stats.retries_performed++;
      if (i2cCheckBusStatus() != I2C_BUS_OK)
        i2cRecoverBus();
      delay(backoff_ms);
      backoff_ms =
          (uint16_t)((float)backoff_ms * retry_config.backoff_multiplier);
      if (backoff_ms > retry_config.max_backoff_ms)
        backoff_ms = retry_config.max_backoff_ms;
    }

    if (read) {
      // FIX: requestFrom() must NOT be called after beginTransmission()
      // Use requestFrom() directly for read operations
      Wire.requestFrom((uint8_t)address, (size_t)len,
                       true); // true = send STOP after read
      if (Wire.available() >= len) {
        for (uint8_t i = 0; i < len; i++)
          data[i] = Wire.read();
        result = I2C_RESULT_OK;
        stats.transactions_success++;
        return result;
      }
    } else {
      // Write operation: beginTransmission -> write -> endTransmission
      Wire.beginTransmission(address);
      Wire.write(data, len);
      uint8_t error = Wire.endTransmission(true);
      if (error == 0) {
        result = I2C_RESULT_OK;
        stats.transactions_success++;
        return result;
      } else {
        if (error == 1) {
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
        }
      }
    }
  }
  stats.transactions_failed++;
  // Fault log only after max retries fail
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, result,
                "I2C Fail 0x%02X: %s", address, i2cResultToString(result));
  return result;
}

i2c_result_t i2cWriteWithRetry(uint8_t address, const uint8_t *data,
                               uint8_t len) {
  uint8_t buffer[len];
  memcpy(buffer, data, len);
  return i2cTransactionWithRetry(address, buffer, len, false);
}

i2c_result_t i2cReadWithRetry(uint8_t address, uint8_t *data, uint8_t len) {
  return i2cTransactionWithRetry(address, data, len, true);
}

i2c_result_t i2cWriteFast(uint8_t address, const uint8_t *data, uint8_t len) {
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
  if (error == 4)
    stats.error_timeout++;
  else if (error == 2)
    stats.error_nack++;
  return (error == 2) ? I2C_RESULT_NACK : I2C_RESULT_TIMEOUT;
}

void i2cSetRetryConfig(i2c_retry_config_t config) { retry_config = config; }
i2c_retry_config_t i2cGetRetryConfig() { return retry_config; }
void i2cResetStats() { memset(&stats, 0, sizeof(stats)); }
i2c_stats_t i2cGetStats() {
  if (stats.transactions_total > 0)
    stats.success_rate =
        (float)stats.transactions_success / stats.transactions_total * 100.0f;
  return stats;
}

void i2cShowStats() {
  serialLoggerLock();
  logPrintln("\r\n[I2C] === Statistics ===");
  logPrintf("Total: %lu | OK: %lu (%.1f%%) | Fail: %lu\r\n",
                (unsigned long)stats.transactions_total,
                (unsigned long)stats.transactions_success, stats.success_rate,
                (unsigned long)stats.transactions_failed);
  logPrintf("Retries: %lu | Recoveries: %lu\r\n",
                (unsigned long)stats.retries_performed,
                (unsigned long)stats.bus_recoveries);
  logPrintf(
      "Errors: NACK=%lu Time=%lu Bus=%lu\r\n", (unsigned long)stats.error_nack,
      (unsigned long)stats.error_timeout, (unsigned long)stats.error_bus);
  serialLoggerUnlock();
}

void i2cMonitorBusHealth() {
  i2c_bus_status_t status = i2cCheckBusStatus();
  if (status != I2C_BUS_OK) {
    faultLogEntry(FAULT_WARNING, FAULT_PLC_COMM_LOSS, -1, status,
                  "I2C Degraded");
  }
}

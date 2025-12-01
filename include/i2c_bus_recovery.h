#ifndef I2C_BUS_RECOVERY_H
#define I2C_BUS_RECOVERY_H

#include <Arduino.h>

// I²C bus status
typedef enum {
  I2C_BUS_OK = 0,
  I2C_BUS_BUSY = 1,
  I2C_BUS_STUCK_SDA = 2,
  I2C_BUS_STUCK_SCL = 3,
  I2C_BUS_ERROR = 4,
  I2C_BUS_TIMEOUT = 5
} i2c_bus_status_t;

// I²C transaction result
typedef enum {
  I2C_RESULT_OK = 0,
  I2C_RESULT_NACK = 1,
  I2C_RESULT_TIMEOUT = 2,
  I2C_RESULT_BUS_ERROR = 3,
  I2C_RESULT_ARBITRATION_LOST = 4,
  I2C_RESULT_DEVICE_BUSY = 5,
  I2C_RESULT_UNKNOWN_ERROR = 6
} i2c_result_t;

// I²C statistics
typedef struct {
  uint32_t transactions_total;      // Total I²C transactions
  uint32_t transactions_success;    // Successful transactions
  uint32_t transactions_failed;     // Failed transactions
  uint32_t retries_performed;       // Total retries
  uint32_t bus_recoveries;          // Bus recovery procedures
  uint32_t error_nack;              // NACK errors
  uint32_t error_timeout;           // Timeout errors
  uint32_t error_bus;               // Bus errors
  uint32_t error_arbitration;       // Arbitration loss
  float success_rate;               // Success percentage
} i2c_stats_t;

// Retry configuration
typedef struct {
  uint8_t max_retries;              // Max retry attempts
  uint16_t initial_backoff_ms;      // Initial backoff (ms)
  uint16_t max_backoff_ms;          // Max backoff (ms)
  float backoff_multiplier;         // Exponential backoff factor
} i2c_retry_config_t;

// Initialize I²C recovery system
void i2cRecoveryInit();

// Bus diagnostics and recovery
i2c_bus_status_t i2cCheckBusStatus();
void i2cRecoverBus();
void i2cSoftReset();
void i2cHardReset();

// Retry with exponential backoff
i2c_result_t i2cTransactionWithRetry(uint8_t address, uint8_t* data, uint8_t len, bool read);
i2c_result_t i2cWriteWithRetry(uint8_t address, const uint8_t* data, uint8_t len);
i2c_result_t i2cReadWithRetry(uint8_t address, uint8_t* data, uint8_t len);

// Configuration
void i2cSetRetryConfig(i2c_retry_config_t config);
i2c_retry_config_t i2cGetRetryConfig();

// Statistics
void i2cResetStats();
i2c_stats_t i2cGetStats();
void i2cShowStats();

// Bus monitoring
void i2cMonitorBusHealth();

// Error code to string
const char* i2cResultToString(i2c_result_t result);
const char* i2cBusStatusToString(i2c_bus_status_t status);

#endif

#ifndef FAULT_LOGGING_H
#define FAULT_LOGGING_H

#include <Arduino.h>

// Fault severity levels
typedef enum {
  FAULT_NONE = 0,
  FAULT_WARNING = 1,
  FAULT_ERROR = 2,
  FAULT_CRITICAL = 3
} fault_severity_t;

// Fault codes
typedef enum {
  FAULT_NONE_CODE = 0x00,
  FAULT_ENCODER_TIMEOUT = 0x01,
  FAULT_PLC_COMM_LOSS = 0x02,
  FAULT_MOTION_STALL = 0x03,
  FAULT_SAFETY_INTERLOCK = 0x04,
  FAULT_SOFT_LIMIT_EXCEEDED = 0x05,
  FAULT_ESTOP_ACTIVATED = 0x06,
  FAULT_POWER_LOSS = 0x07,
  FAULT_TEMPERATURE_HIGH = 0x08,
  FAULT_CALIBRATION_MISSING = 0x09,
  FAULT_CONFIGURATION_INVALID = 0x0A,
  FAULT_WATCHDOG_TIMEOUT = 0x0B,
  FAULT_BOOT_FAILED = 0x0C,
  FAULT_BOOT_RECOVERY_ATTEMPTED = 0x0D,
  FAULT_CRITICAL_SYSTEM_ERROR = 0x0E,
  FAULT_EMERGENCY_HALT = 0x0F,
  FAULT_GRACEFUL_SHUTDOWN = 0x10,
  FAULT_ENCODER_SPIKE = 0x11,
  FAULT_I2C_ERROR = 0x12,
  FAULT_TASK_HUNG = 0x13
} fault_code_t;

typedef struct {
  uint32_t timestamp;           // Boot timestamp (ms since system start)
  fault_severity_t severity;    // Severity level
  fault_code_t code;            // Fault code
  int32_t axis;                 // Axis affected (-1 for system)
  int32_t value;                // Associated value (encoder pos, voltage, etc)
  char message[64];             // Human-readable message
} fault_entry_t;

// Boot validation results
typedef struct {
  bool config_ok;               // Configuration system initialized
  bool safety_ok;               // Safety system initialized
  bool motion_ok;               // Motion system initialized
  bool encoder_ok;              // Encoder system initialized
  bool plc_ok;                  // PLC interface initialized
  bool lcd_ok;                  // LCD interface initialized
  bool cli_ok;                  // CLI system initialized
  uint32_t boot_time_ms;        // Total boot time
} boot_status_t;

void faultLoggingInit();
void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* message);
void faultLogWarning(fault_code_t code, const char* message);
void faultLogError(fault_code_t code, const char* message);
void faultLogCritical(fault_code_t code, const char* message);
void faultShowHistory();
void faultClearHistory();
const char* faultCodeToString(fault_code_t code);
const char* faultSeverityToString(fault_severity_t severity);

// Boot validation
void bootValidationInit();
bool bootValidateAllSystems();
void bootShowStatus();

// Emergency stop recovery
void emergencyStopSetActive(bool active);
bool emergencyStopIsActive();
bool emergencyStopRequestRecovery();
void emergencyStopClearRecovery();

#endif

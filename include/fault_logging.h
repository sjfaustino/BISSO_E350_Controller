#ifndef FAULT_LOGGING_H
#define FAULT_LOGGING_H

#include <Arduino.h>
#include <stdarg.h> // New: Required for variable arguments

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

// NOTE: The message size here implicitly defines FAULT_MESSAGE_MAX_LEN
typedef struct {
  uint32_t timestamp;           // Boot timestamp (ms since system start)
  fault_severity_t severity;    // Severity level
  fault_code_t code;            // Fault code
  int32_t axis;                 // Axis affected (-1 for system)
  int32_t value;                // Associated value (encoder pos, voltage, etc)
  char message[64];             // Human-readable message
} fault_entry_t;

// --- REFACTORED SIGNATURE ---
void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* format, ...);

// NOTE: The simple wrappers (faultLogWarning, faultLogError, etc.) are now less useful.
// We will update them to use the new signature, passing the simple message as the format string.
void faultLogWarning(fault_code_t code, const char* message);
void faultLogError(fault_code_t code, const char* message);
void faultLogCritical(fault_code_t code, const char* message);

// Initialization and utility functions remain unchanged
void faultLoggingInit();
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
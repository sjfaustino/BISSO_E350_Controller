#ifndef FAULT_LOGGING_H
#define FAULT_LOGGING_H

#include <Arduino.h>
#include <stdarg.h> 

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
  FAULT_TASK_HUNG = 0x13,
  FAULT_MOTION_TIMEOUT = 0x14,  // PHASE 5.1: Motion mutex timeout
  FAULT_SPINDLE_OVERCURRENT = 0x15,  // PHASE 5.1: Spindle overcurrent detection
  FAULT_SPINDLE_STALL = 0x16,  // Spindle stall (prolonged overload)
  FAULT_SPINDLE_TOOLBREAK = 0x17,  // Tool breakage (sudden current drop)
  FAULT_CODE_MAX = 0x18  // Maximum fault code value (for array sizing)
} fault_code_t;

// Fault Statistics Structure (Must match usage in cli_diag.cpp)
typedef struct {
    uint32_t total_faults;
    uint32_t encoder_faults; // F_ENCODER_TIMEOUT, F_ENCODER_SPIKE
    uint32_t motion_faults;  // F_MOTION_STALL, F_SOFT_LIMIT_EXCEEDED
    uint32_t safety_faults;  // F_ESTOP_ACTIVATED, F_SAFETY_INTERLOCK, F_EMERGENCY_HALT
    uint32_t config_faults;  // F_CONFIGURATION_INVALID, F_CALIBRATION_MISSING, F_BOOT_FAILED
    uint32_t plc_faults;     // F_PLC_COMM_LOSS, F_I2C_ERROR
    uint32_t system_faults;  // F_WATCHDOG_TIMEOUT, F_TASK_HUNG, F_CRITICAL_SYSTEM_ERROR
    uint32_t other_faults;   // Catch-all
    uint32_t last_fault_time_ms;
    uint32_t first_fault_time_ms;
} fault_stats_t;

typedef struct {
  uint32_t timestamp;           
  fault_severity_t severity;    
  fault_code_t code;            
  int32_t axis;                 
  int32_t value;                
  char message[64];             
} fault_entry_t;

// Public Logging API
void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* format, ...);

// Worker API
void faultLogToNVS(const fault_entry_t* entry);

// Wrappers
void faultLogWarning(fault_code_t code, const char* message);
void faultLogError(fault_code_t code, const char* message);
void faultLogCritical(fault_code_t code, const char* message);

// Initialization and utility functions
void faultLoggingInit();
void faultShowHistory();
void faultClearHistory();
const char* faultCodeToString(fault_code_t code);
const char* faultSeverityToString(fault_severity_t severity);

// Fault Statistics Access
fault_stats_t faultGetStats();

// Emergency stop management
void emergencyStopSetActive(bool active);
bool emergencyStopIsActive();
bool emergencyStopRequestRecovery();
void emergencyStopClearRecovery();

// PHASE 5.1: Ring buffer fallback diagnostics
uint32_t faultGetRingBufferDropCount();
uint8_t faultGetRingBufferEntryCount();
const fault_entry_t* faultGetRingBufferEntry(uint8_t index);

#endif

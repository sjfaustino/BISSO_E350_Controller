/**
 * @file boot_validation.h
 * @brief Boot sequence validation and error recovery system
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 * * This module provides comprehensive boot validation including:
 * - Subsystem initialization tracking
 * - Error detection and logging
 * - Graceful degradation modes (normal/degraded/halted)
 * - Recovery attempt coordination
 * - Persistent error history (NVS)
 */

#ifndef BOOT_VALIDATION_H
#define BOOT_VALIDATION_H

#include <Arduino.h>

// ============================================================================
// BOOT VALIDATION STATUS CODES
// ============================================================================

typedef enum {
  BOOT_OK = 0,                           
  BOOT_ERROR_FAULT_LOGGING = 1,         
  BOOT_ERROR_WATCHDOG = 2,              
  BOOT_ERROR_TIMEOUT_MANAGER = 3,       
  BOOT_ERROR_CONFIG = 4,                
  BOOT_ERROR_SCHEMA = 5,                
  BOOT_ERROR_SAFETY = 6,                
  BOOT_ERROR_MOTION = 7,                
  BOOT_ERROR_ENCODER = 8,               
  BOOT_ERROR_ENCODER_CALIB = 9,         
  BOOT_ERROR_PLC_IFACE = 10,            
  BOOT_ERROR_CLI = 11,                  
  BOOT_ERROR_TASK_MANAGER = 12,         
  BOOT_CRITICAL_ERROR = 255,            
} boot_status_code_t;

// ============================================================================
// SUBSYSTEM HEALTH CHECK
// ============================================================================

typedef struct {
  const char* subsystem_name;
  bool initialized;
  bool healthy;
  uint32_t init_time_ms;
  uint32_t last_check_ms;
  uint32_t error_count;
  const char* last_error;
  boot_status_code_t status;
} subsystem_health_t;

// ============================================================================
// BOOT SEQUENCE TRACKING
// ============================================================================

typedef struct {
  uint32_t boot_start_time;
  uint32_t boot_complete_time;
  uint32_t total_boot_time_ms;
  uint8_t systems_initialized;
  uint8_t systems_healthy;
  uint8_t systems_failed;
  boot_status_code_t overall_status;
  bool boot_validation_passed;
} boot_sequence_t;

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

void bootValidationInit();
void bootRegisterSubsystem(const char* name);
void bootMarkInitialized(const char* name);
void bootMarkFailed(const char* name, const char* error_msg, boot_status_code_t status);
void bootMarkHealthy(const char* name);
void bootMarkUnhealthy(const char* name, const char* error_msg);

// ============================================================================
// VALIDATION & DIAGNOSTICS
// ============================================================================

bool bootValidateAllSystems();
bool bootIsSubsystemHealthy(const char* name);
boot_status_code_t bootGetStatus();
void bootShowStatus();
void bootShowDetailedReport();
void bootShowSubsystemHealth();
void bootShowInitTimes();

// ============================================================================
// ERROR RECOVERY
// ============================================================================

bool bootAttemptRecovery(const char* subsystem);
void bootHandleCriticalError(const char* error_msg);
void bootRebootSystem();
void bootEmergencyHalt(const char* reason);

// ============================================================================
// DEGRADED MODE OPERATION
// ============================================================================

bool bootCanOperateDegraded();
void bootSetDegradedMode(bool enabled);
bool bootIsDegradedMode();
void bootShowDegradedModeStatus();

// ============================================================================
// BOOT DIAGNOSTICS & LOGGING
// ============================================================================

void bootLogErrors();
const char* bootGetLastError();
uint32_t bootGetErrorCount();
uint32_t bootGetConsecutiveSuccesses();
void bootShowRecoveryHistory();

// ============================================================================
// SAFE INITIALIZATION WRAPPERS
// ============================================================================

// Safe initialization wrapper with error checking
#define BOOT_INIT_SUBSYSTEM(subsystem_name, init_function, error_code) \
  do { \
    Serial.print("[BOOT] Initializing "); \
    Serial.print(subsystem_name); \
    Serial.print("... "); \
    bootRegisterSubsystem(subsystem_name); \
    uint32_t init_start = millis(); \
    bool init_result = init_function(); \
    uint32_t init_time = millis() - init_start; \
    if (init_result) { \
      Serial.print("[OK] ("); \
      Serial.print(init_time); \
      Serial.println("ms)"); \
      bootMarkInitialized(subsystem_name); \
    } else { \
      Serial.print("[FAIL] ("); \
      Serial.print(init_time); \
      Serial.println("ms)"); \
      bootMarkFailed(subsystem_name, "Initialization returned false", error_code); \
    } \
  } while(0)

// ============================================================================
// GRACEFUL SHUTDOWN
// ============================================================================

void bootGracefulShutdown(const char* reason);
bool bootIsShuttingDown();

#endif
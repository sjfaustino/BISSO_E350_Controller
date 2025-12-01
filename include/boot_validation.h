/**
 * @file boot_validation.h
 * @brief Boot sequence validation and error recovery system
 * 
 * This module provides comprehensive boot validation including:
 * - Subsystem initialization tracking
 * - Error detection and logging
 * - Graceful degradation modes (normal/degraded/halted)
 * - Recovery attempt coordination
 * - Persistent error history (NVS)
 * 
 * @section boot_flow Boot Flow
 * 
 * The complete boot sequence:
 * @code
 * Boot Start (setup())
 *   │
 *   ├─► bootValidationInit()      - Initialize boot tracking
 *   │
 *   ├─► Register & Initialize Each Subsystem
 *   │   ├─ bootRegisterSubsystem("name")
 *   │   ├─ [run initialization code]
 *   │   ├─ bootMarkInitialized("name")  OR  bootMarkFailed("name", error, status)
 *   │   └─ [repeat for all subsystems]
 *   │
 *   ├─► bootValidateAllSystems() - Validate all subsystems
 *   │   ├─ 0 failures   → Normal Mode
 *   │   ├─ 1-2 failures → Degraded Mode
 *   │   └─ 3+ failures  → Emergency Halt
 *   │
 *   ├─► taskManagerInit()         - Create FreeRTOS tasks
 *   │
 *   └─► FreeRTOS Scheduler Takes Over
 * @endcode
 * 
 * @section error_codes Error Codes
 * 
 * Each error code represents a specific subsystem failure:
 * - BOOT_ERROR_FAULT_LOGGING - Fault logging initialization failed
 * - BOOT_ERROR_WATCHDOG - Watchdog timer setup failed
 * - BOOT_ERROR_CONFIG - Configuration system failed
 * - BOOT_ERROR_SAFETY - Safety system failed
 * - BOOT_CRITICAL_ERROR - Unrecoverable error (halt immediately)
 * 
 * @author Sergio (BISSO v4.2)
 * @version 1.0
 * @date 2025-01
 */

#ifndef BOOT_VALIDATION_H
#define BOOT_VALIDATION_H

#include <Arduino.h>

// ============================================================================
// BOOT VALIDATION STATUS CODES
// ============================================================================

/**
 * @enum boot_status_code_t
 * @brief Status codes for boot sequence and subsystem initialization
 * 
 * Used to track which subsystem failed during boot, or if all systems
 * initialized successfully.
 */
typedef enum {
  BOOT_OK = 0,                           /*!< All systems initialized successfully */
  BOOT_ERROR_FAULT_LOGGING = 1,         /*!< Fault logging system failed */
  BOOT_ERROR_WATCHDOG = 2,              /*!< Watchdog initialization failed */
  BOOT_ERROR_TIMEOUT_MANAGER = 3,       /*!< Timeout manager failed */
  BOOT_ERROR_CONFIG = 4,                /*!< Configuration system failed */
  BOOT_ERROR_SCHEMA = 5,                /*!< Schema versioning failed */
  BOOT_ERROR_SAFETY = 6,                /*!< Safety system failed */
  BOOT_ERROR_MOTION = 7,                /*!< Motion system failed */
  BOOT_ERROR_ENCODER = 8,               /*!< Encoder system failed */
  BOOT_ERROR_ENCODER_CALIB = 9,         /*!< Encoder calibration failed */
  BOOT_ERROR_PLC_IFACE = 10,            /*!< PLC interface failed */
  BOOT_ERROR_CLI = 11,                  /*!< CLI interface failed */
  BOOT_ERROR_TASK_MANAGER = 12,         /*!< Task manager failed */
  BOOT_CRITICAL_ERROR = 255,            /*!< Critical unrecoverable error */
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
// INITIALIZATION FUNCTIONS WITH ERROR CHECKING
// ============================================================================

// Initialize boot validation system
void bootValidationInit();

// Register subsystem for monitoring
void bootRegisterSubsystem(const char* name);

// Mark subsystem as initialized
void bootMarkInitialized(const char* name);

// Mark subsystem as failed with error message
void bootMarkFailed(const char* name, const char* error_msg, boot_status_code_t status);

// Mark subsystem as healthy
void bootMarkHealthy(const char* name);

// Mark subsystem as unhealthy
void bootMarkUnhealthy(const char* name, const char* error_msg);

// ============================================================================
// VALIDATION & DIAGNOSTICS
// ============================================================================

// Validate all subsystems are initialized
bool bootValidateAllSystems();

// Check if specific subsystem is healthy
bool bootIsSubsystemHealthy(const char* name);

// Get overall boot status
boot_status_code_t bootGetStatus();

// Show boot status (safe to call anytime)
void bootShowStatus();

// Show detailed boot report
void bootShowDetailedReport();

// Show subsystem health
void bootShowSubsystemHealth();

// Check initialization times
void bootShowInitTimes();

// ============================================================================
// ERROR RECOVERY
// ============================================================================

// Attempt to recover from subsystem failure
bool bootAttemptRecovery(const char* subsystem);

// Gracefully handle critical error
void bootHandleCriticalError(const char* error_msg);

// Reboot system (graceful reset)
void bootRebootSystem();

// Emergency halt (something is seriously wrong)
void bootEmergencyHalt(const char* reason);

// ============================================================================
// DEGRADED MODE OPERATION
// ============================================================================

// Can system operate in degraded mode?
bool bootCanOperateDegraded();

// Set degraded mode flag
void bootSetDegradedMode(bool enabled);

// Get degraded mode status
bool bootIsDegradedMode();

// Show which systems are unavailable in degraded mode
void bootShowDegradedModeStatus();

// ============================================================================
// BOOT DIAGNOSTICS & LOGGING
// ============================================================================

// Log all boot errors to NVS
void bootLogErrors();

// Get last boot error
const char* bootGetLastError();

// Get boot error count
uint32_t bootGetErrorCount();

// Get number of consecutive successful boots
uint32_t bootGetConsecutiveSuccesses();

// Show recovery attempts
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
      Serial.print("✅ OK ("); \
      Serial.print(init_time); \
      Serial.println("ms)"); \
      bootMarkInitialized(subsystem_name); \
    } else { \
      Serial.print("❌ FAILED ("); \
      Serial.print(init_time); \
      Serial.println("ms)"); \
      bootMarkFailed(subsystem_name, "Initialization returned false", error_code); \
    } \
  } while(0)

// ============================================================================
// GRACEFUL SHUTDOWN
// ============================================================================

// Gracefully shutdown all systems
void bootGracefulShutdown(const char* reason);

// Check if system is shutting down
bool bootIsShuttingDown();

#endif

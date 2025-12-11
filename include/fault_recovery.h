/**
 * @file fault_recovery.h
 * @brief Fault Recovery Framework
 * @details Implements automatic recovery strategies for transient faults
 */

#ifndef FAULT_RECOVERY_H
#define FAULT_RECOVERY_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// RECOVERY STRATEGIES
// ============================================================================

typedef enum {
    RECOVERY_RETRY = 0,           // Retry the operation
    RECOVERY_RESET_SUBSYSTEM = 1, // Reset the subsystem (I2C, encoder, etc)
    RECOVERY_FALLBACK = 2,        // Use fallback mode
    RECOVERY_MANUAL = 3,          // Requires manual intervention
    RECOVERY_NONE = 4             // No recovery available
} recovery_strategy_t;

typedef enum {
    FAULT_TYPE_I2C_ERROR = 0,
    FAULT_TYPE_ENCODER_TIMEOUT = 1,
    FAULT_TYPE_ENCODER_DEVIATION = 2,
    FAULT_TYPE_MOTION_STALL = 3,
    FAULT_TYPE_CONFIG_ERROR = 4,
    FAULT_TYPE_UNKNOWN = 5
} fault_type_t;

typedef struct {
    fault_type_t type;
    recovery_strategy_t strategy;
    uint32_t retry_count;           // Number of retries performed
    uint32_t max_retries;           // Maximum retries before giving up
    uint32_t last_recovery_ms;      // Timestamp of last recovery attempt
    uint32_t recovery_count;        // Total number of recoveries
    bool success;                   // Did the last recovery succeed?
    uint32_t backoff_delay_ms;      // Exponential backoff delay
} fault_recovery_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize recovery system
void faultRecoveryInit();

// Attempt recovery for a fault
bool faultRecoveryAttempt(fault_type_t fault_type);

// Get recovery status for a fault type
const fault_recovery_t* faultRecoveryGetStatus(fault_type_t fault_type);

// Reset recovery counters
void faultRecoveryReset(fault_type_t fault_type);
void faultRecoveryResetAll();

// Diagnostics
void faultRecoveryDiagnostics();
const char* faultTypeToString(fault_type_t type);
const char* recoveryStrategyToString(recovery_strategy_t strategy);

#endif // FAULT_RECOVERY_H

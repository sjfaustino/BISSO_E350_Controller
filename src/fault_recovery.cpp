/**
 * @file fault_recovery.cpp
 * @brief Implementation of Fault Recovery Framework
 */

#include "fault_recovery.h"
#include "serial_logger.h"
#include "i2c_bus_recovery.h"
#include "encoder_wj66.h"
#include <string.h>

// ============================================================================
// STATE TRACKING
// ============================================================================

static fault_recovery_t recovery_data[6] = {
    {FAULT_TYPE_I2C_ERROR, RECOVERY_RESET_SUBSYSTEM, 0, 3, 0, 0, false, 10},
    {FAULT_TYPE_ENCODER_TIMEOUT, RECOVERY_RETRY, 0, 5, 0, 0, false, 50},
    {FAULT_TYPE_ENCODER_DEVIATION, RECOVERY_RESET_SUBSYSTEM, 0, 2, 0, 0, false, 100},
    {FAULT_TYPE_MOTION_STALL, RECOVERY_MANUAL, 0, 1, 0, 0, false, 0},
    {FAULT_TYPE_CONFIG_ERROR, RECOVERY_MANUAL, 0, 1, 0, 0, false, 0},
    {FAULT_TYPE_UNKNOWN, RECOVERY_NONE, 0, 0, 0, 0, false, 0}
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char* faultTypeToString(fault_type_t type) {
    switch (type) {
        case FAULT_TYPE_I2C_ERROR:        return "I2C_ERROR";
        case FAULT_TYPE_ENCODER_TIMEOUT:  return "ENCODER_TIMEOUT";
        case FAULT_TYPE_ENCODER_DEVIATION: return "ENCODER_DEVIATION";
        case FAULT_TYPE_MOTION_STALL:     return "MOTION_STALL";
        case FAULT_TYPE_CONFIG_ERROR:     return "CONFIG_ERROR";
        case FAULT_TYPE_UNKNOWN:          return "UNKNOWN";
        default:                          return "INVALID";
    }
}

const char* recoveryStrategyToString(recovery_strategy_t strategy) {
    switch (strategy) {
        case RECOVERY_RETRY:              return "RETRY";
        case RECOVERY_RESET_SUBSYSTEM:    return "RESET_SUBSYSTEM";
        case RECOVERY_FALLBACK:           return "FALLBACK";
        case RECOVERY_MANUAL:             return "MANUAL";
        case RECOVERY_NONE:               return "NONE";
        default:                          return "UNKNOWN";
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void faultRecoveryInit() {
    logInfo("[FAULT_RECOVERY] Initializing recovery system");
    faultRecoveryResetAll();
}

bool faultRecoveryAttempt(fault_type_t fault_type) {
    if (fault_type >= 6) return false;

    fault_recovery_t* recovery = &recovery_data[fault_type];
    uint32_t now = millis();

    logWarning("[FAULT_RECOVERY] Attempting recovery for %s", faultTypeToString(fault_type));

    // Check if we've exceeded max retries
    if (recovery->retry_count >= recovery->max_retries) {
        logError("[FAULT_RECOVERY] Max retries exceeded for %s", faultTypeToString(fault_type));
        recovery->success = false;
        return false;
    }

    // Implement exponential backoff
    if (recovery->last_recovery_ms > 0) {
        uint32_t time_since_last = now - recovery->last_recovery_ms;
        if (time_since_last < recovery->backoff_delay_ms) {
            logInfo("[FAULT_RECOVERY] Backoff delay: waiting %lu ms", (unsigned long)(recovery->backoff_delay_ms - time_since_last));
            return false;
        }
    }

    recovery->last_recovery_ms = now;
    recovery->retry_count++;

    // Perform recovery based on strategy
    bool success = false;

    switch (recovery->strategy) {
        case RECOVERY_RETRY:
            // Simple retry - assuming operation will be retried by caller
            logInfo("[FAULT_RECOVERY] Retry strategy: Attempt %lu/%lu", (unsigned long)recovery->retry_count, (unsigned long)recovery->max_retries);
            success = true;
            break;

        case RECOVERY_RESET_SUBSYSTEM:
            logInfo("[FAULT_RECOVERY] Resetting subsystem: %s", faultTypeToString(fault_type));
            if (fault_type == FAULT_TYPE_I2C_ERROR) {
                i2cRecoverBus();
                success = true;
            } else if (fault_type == FAULT_TYPE_ENCODER_TIMEOUT || fault_type == FAULT_TYPE_ENCODER_DEVIATION) {
                wj66Reset();
                success = true;
            }
            break;

        case RECOVERY_FALLBACK:
            logInfo("[FAULT_RECOVERY] Using fallback mode for %s", faultTypeToString(fault_type));
            success = true;
            break;

        case RECOVERY_MANUAL:
            logError("[FAULT_RECOVERY] Manual intervention required for %s", faultTypeToString(fault_type));
            success = false;
            break;

        case RECOVERY_NONE:
            logError("[FAULT_RECOVERY] No recovery strategy available for %s", faultTypeToString(fault_type));
            success = false;
            break;

        default:
            success = false;
            break;
    }

    recovery->success = success;
    if (success) {
        recovery->recovery_count++;
        logInfo("[FAULT_RECOVERY] Recovery successful for %s", faultTypeToString(fault_type));
        // Increase backoff for next time
        recovery->backoff_delay_ms = (recovery->backoff_delay_ms * 2);
        if (recovery->backoff_delay_ms > 10000) recovery->backoff_delay_ms = 10000; // Cap at 10s
    }

    return success;
}

const fault_recovery_t* faultRecoveryGetStatus(fault_type_t fault_type) {
    if (fault_type >= 6) return NULL;
    return &recovery_data[fault_type];
}

void faultRecoveryReset(fault_type_t fault_type) {
    if (fault_type >= 6) return;
    fault_recovery_t* recovery = &recovery_data[fault_type];
    recovery->retry_count = 0;
    recovery->recovery_count = 0;
    recovery->last_recovery_ms = 0;
    recovery->success = false;
    recovery->backoff_delay_ms = (fault_type == FAULT_TYPE_I2C_ERROR) ? 10 : 50;
    logInfo("[FAULT_RECOVERY] Reset recovery for %s", faultTypeToString(fault_type));
}

void faultRecoveryResetAll() {
    for (int i = 0; i < 6; i++) {
        faultRecoveryReset((fault_type_t)i);
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void faultRecoveryDiagnostics() {
    Serial.println("\n[FAULT_RECOVERY] === Recovery Status ===");

    for (int i = 0; i < 6; i++) {
        const fault_recovery_t* recovery = &recovery_data[i];

        Serial.printf("\n%s:\n", faultTypeToString(recovery->type));
        Serial.printf("  Strategy: %s\n", recoveryStrategyToString(recovery->strategy));
        Serial.printf("  Retries: %lu/%lu\n", (unsigned long)recovery->retry_count, (unsigned long)recovery->max_retries);
        Serial.printf("  Recovery Count: %lu\n", (unsigned long)recovery->recovery_count);
        Serial.printf("  Last Status: %s\n", recovery->success ? "SUCCESS" : "FAILED");
        Serial.printf("  Backoff: %lu ms\n", (unsigned long)recovery->backoff_delay_ms);
    }
}

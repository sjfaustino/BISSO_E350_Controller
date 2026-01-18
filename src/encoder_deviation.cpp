/**
 * @file encoder_deviation.cpp
 * @brief Implementation of Encoder Deviation Detection
 * @details Detects mechanical problems, stalls, and loss of synchronization
 */

#include "encoder_deviation.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "system_constants.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include "motion.h"
#include <math.h>

// ============================================================================
// STATE TRACKING
// ============================================================================

static encoder_deviation_t deviation_data[MOTION_AXES] = {
    {AXIS_OK, 0, 0, 0, 0, 0, 0, 0, 0},
    {AXIS_OK, 0, 0, 0, 0, 0, 0, 0, 0},
    {AXIS_OK, 0, 0, 0, 0, 0, 0, 0, 0},
    {AXIS_OK, 0, 0, 0, 0, 0, 0, 0, 0}
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char* encoderDeviationStatusToString(encoder_deviation_status_t status) {
    switch (status) {
        case AXIS_OK:                  return "OK";
        case AXIS_DEVIATION_WARNING:   return "DEVIATION_WARN";
        case AXIS_DEVIATION_ERROR:     return "DEVIATION_ERR";
        case AXIS_STALLED:             return "STALLED";
        case AXIS_OVERSHOOT:           return "OVERSHOOT";
        default:                       return "UNKNOWN";
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void encoderDeviationInit() {
    logInfo("[ENCODER_DEV] Initializing deviation detection");
    encoderDeviationClearAll();
}

void encoderDeviationUpdate(uint8_t axis, int32_t expected_pos, int32_t actual_pos, float velocity_mm_s) {
    if (axis >= MOTION_AXES) return;

    encoder_deviation_t* dev = &deviation_data[axis];
    uint32_t now = millis();

    // Update position data
    dev->expected_position = expected_pos;
    dev->actual_position = actual_pos;
    dev->deviation_counts = actual_pos - expected_pos;
    dev->last_update_ms = now;

    // Track max deviation
    int32_t abs_deviation = abs(dev->deviation_counts);
    if (abs_deviation > dev->max_deviation) {
        dev->max_deviation = abs_deviation;
    }

    // ============================================================================
    // STATE MACHINE: Determine deviation status
    // ============================================================================

    bool is_moving = (velocity_mm_s > ENCODER_MIN_ACTIVE_VELOCITY_MM_S);
    bool exceeds_tolerance = (abs_deviation > ENCODER_DEVIATION_TOLERANCE_COUNTS);

    switch (dev->status) {
        case AXIS_OK:
            if (is_moving && exceeds_tolerance) {
                // Deviation started
                dev->status = AXIS_DEVIATION_WARNING;
                dev->deviation_start_ms = now;
                dev->deviation_count++;
                logWarning("[ENCODER_DEV] Axis %d deviation detected: %ld counts", axis, (long)dev->deviation_counts);
            }
            break;

        case AXIS_DEVIATION_WARNING:
            if (!exceeds_tolerance) {
                // Deviation corrected
                dev->status = AXIS_OK;
                logInfo("[ENCODER_DEV] Axis %d recovered (was %.1f%% off)",
                    axis, (abs_deviation / (float)ENCODER_DEVIATION_TOLERANCE_COUNTS) * 100.0f);
            }
            else if (now - dev->deviation_start_ms > ENCODER_DEVIATION_TIMEOUT_MS) {
                // Sustained deviation - escalate to error
                dev->status = AXIS_DEVIATION_ERROR;
                dev->alarm_count++;
                faultLogEntry(FAULT_ERROR, FAULT_ENCODER_SPIKE, axis, dev->deviation_counts,
                    "Sustained encoder deviation: %ld counts", (long)dev->deviation_counts);
                logError("[ENCODER_DEV] Axis %d: SUSTAINED DEVIATION ALARM", axis);

                // PHASE 5.10: Signal encoder deviation event
                systemEventsSafetySet(EVENT_SAFETY_ENCODER_DEVIATION);
            }
            else if (!is_moving) {
                // Motion stopped, clear warning
                dev->status = AXIS_OK;
            }
            break;

        case AXIS_DEVIATION_ERROR:
            if (!exceeds_tolerance && now - dev->deviation_start_ms > 2000) {
                // Deviation cleared for 2 seconds
                dev->status = AXIS_OK;
                logInfo("[ENCODER_DEV] Axis %d deviation cleared", axis);

                // PHASE 5.10: Clear encoder deviation event
                systemEventsSafetyClear(EVENT_SAFETY_ENCODER_DEVIATION);
            }
            break;

        case AXIS_STALLED:
            if (is_moving) {
                // Motion resumed
                dev->status = AXIS_OK;
                logInfo("[ENCODER_DEV] Axis %d stall cleared", axis);
            }
            break;

        case AXIS_OVERSHOOT:
            if (!exceeds_tolerance) {
                dev->status = AXIS_OK;
            }
            break;

        default:
            dev->status = AXIS_OK;
            break;
    }
}

encoder_deviation_status_t encoderGetDeviationStatus(uint8_t axis) {
    if (axis >= MOTION_AXES) return AXIS_OK;
    return deviation_data[axis].status;
}

const encoder_deviation_t* encoderGetDeviationData(uint8_t axis) {
    if (axis >= MOTION_AXES) return NULL;
    return &deviation_data[axis];
}

bool encoderHasDeviationAlarm() {
    for (int i = 0; i < MOTION_AXES; i++) {
        if (deviation_data[i].status == AXIS_DEVIATION_ERROR ||
            deviation_data[i].status == AXIS_STALLED) {
            return true;
        }
    }
    return false;
}

void encoderDeviationClear(uint8_t axis) {
    if (axis >= MOTION_AXES) return;
    encoder_deviation_t* dev = &deviation_data[axis];
    dev->status = AXIS_OK;
    dev->deviation_start_ms = 0;
    dev->max_deviation = 0;
    logInfo("[ENCODER_DEV] Axis %d deviation data cleared", axis);
}

void encoderDeviationClearAll() {
    for (int i = 0; i < MOTION_AXES; i++) {
        encoderDeviationClear(i);
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void encoderDeviationDiagnostics() {
    serialLoggerLock();
    logPrintln("\n[ENCODER_DEV] === Deviation Detection Status ===");

    bool has_any_deviation = false;

    for (int axis = 0; axis < MOTION_AXES; axis++) {
        const encoder_deviation_t* dev = &deviation_data[axis];
        const char* axis_name[] = {"X", "Y", "Z", "A"};

        logPrintf("\r\nAxis %s:\r\n", axis_name[axis]);
        logPrintf("  Status: %s\r\n", encoderDeviationStatusToString(dev->status));
        logPrintf("  Expected: %ld, Actual: %ld\r\n", (long)dev->expected_position, (long)dev->actual_position);
        logPrintf("  Deviation: %ld counts (+/-%ld max)\r\n", (long)dev->deviation_counts, (long)dev->max_deviation);
        logPrintf("  Events: %lu deviations, %lu alarms\r\n", (unsigned long)dev->deviation_count, (unsigned long)dev->alarm_count);

        if (dev->status != AXIS_OK) {
            has_any_deviation = true;
            logPrintf("  [ALERT] Axis has active deviation condition!\r\n");
        }
    }

    if (!has_any_deviation) {
        logPrintln("\n[ENCODER_DEV] All axes tracking normally");
    }
    serialLoggerUnlock();
}

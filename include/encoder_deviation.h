/**
 * @file encoder_deviation.h
 * @brief Encoder Deviation Detection System
 * @details Detects when actual encoder position deviates significantly from expected position.
 *          This indicates mechanical problems, stalls, or loss of synchronization.
 */

#ifndef ENCODER_DEVIATION_H
#define ENCODER_DEVIATION_H

#include <stdint.h>
#include <stdbool.h>
#include "system_tuning.h"

// ============================================================================
// DEVIATION DETECTION CONSTANTS
// ============================================================================
// Thresholds and timeouts are now centralized in system_tuning.h
// ENCODER_DEVIATION_TOLERANCE_COUNTS 
// ENCODER_DEVIATION_TIMEOUT_MS (Configurable via KEY_ENC_DEV_TIMEOUT)
// ENCODER_DEVIATION_RECOVERY_MS
// ENCODER_DEVIATION_MIN_ACTIVE_VEL_MM_S

// ============================================================================
// DEVIATION STATUS CODES
// ============================================================================

typedef enum {
    AXIS_OK = 0,                  // Position tracking normally
    AXIS_DEVIATION_WARNING = 1,   // Position deviated temporarily
    AXIS_DEVIATION_ERROR = 2,     // Sustained deviation (alarm condition)
    AXIS_STALLED = 3,             // Motion commanded but encoder not moving
    AXIS_OVERSHOOT = 4            // Target overshooting expected range
} encoder_deviation_status_t;

// ============================================================================
// DEVIATION TRACKING DATA
// ============================================================================

typedef struct {
    // Current state
    encoder_deviation_status_t status;
    int32_t expected_position;      // Where we think we should be
    int32_t actual_position;        // Actual encoder reading
    int32_t deviation_counts;       // Deviation magnitude (signed)

    // Timing
    uint32_t deviation_start_ms;    // When deviation was first detected
    uint32_t last_update_ms;

    // Statistics
    uint32_t deviation_count;       // Number of deviation events
    uint32_t alarm_count;           // Number of times alarm triggered
    int32_t max_deviation;          // Peak deviation magnitude
} encoder_deviation_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize deviation detection
void encoderDeviationInit();

// Update deviation detection (call once per motion update cycle)
void encoderDeviationUpdate(uint8_t axis, int32_t expected_pos, int32_t actual_pos, float velocity_mm_s);

// Get current deviation status for an axis
encoder_deviation_status_t encoderGetDeviationStatus(uint8_t axis);

// Get deviation data for an axis
const encoder_deviation_t* encoderGetDeviationData(uint8_t axis);

// Check if any axis has a critical deviation alarm
bool encoderHasDeviationAlarm();

// Clear deviation counters (after correcting problem)
void encoderDeviationClear(uint8_t axis);
void encoderDeviationClearAll();

// Diagnostics
void encoderDeviationDiagnostics();
const char* encoderDeviationStatusToString(encoder_deviation_status_t status);

#endif // ENCODER_DEVIATION_H

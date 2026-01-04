#ifndef ENCODER_MOTION_INTEGRATION_H
#define ENCODER_MOTION_INTEGRATION_H

#include <Arduino.h>
#include "encoder_wj66.h"
#include "motion.h"

// ============================================================================
// ENCODER-MOTION INTEGRATION
// ============================================================================

/**
 * Position error tracking structure
 */
typedef struct {
  int32_t current_error;        // Current position error (encoder - motion)
  int32_t max_error;            // Maximum error seen
  int32_t error_threshold;      // Error threshold for alarm
  uint32_t error_time_ms;       // Timestamp when error became active (start time)
  uint32_t max_error_time_ms;   // Max allowed error duration
  bool error_active;            // Currently in error state
  uint32_t error_count;         // Number of error events
} position_error_t;

/**
 * Initialize encoder-motion integration
 * * @param error_threshold Maximum allowed position error (in internal units)
 * @param max_error_time_ms Maximum time error can persist before alarm
 */
void encoderMotionInit(int32_t error_threshold, uint32_t max_error_time_ms);

/**
 * Update motion system with encoder feedback
 * * @return true if all encoders valid, false if stale/missing
 */
bool encoderMotionUpdate();

/**
 * Get position error for an axis
 * * @param axis Axis index (0-3)
 * @return Position error (positive = encoder ahead, negative = encoder behind)
 */
int32_t encoderMotionGetPositionError(uint8_t axis);

/**
 * Get maximum position error seen for an axis
 * * @param axis Axis index (0-3)
 * @return Maximum error magnitude
 */
int32_t encoderMotionGetMaxError(uint8_t axis);

/**
 * Get the duration the error has been active for the axis.
 * * @param axis Axis index (0-3)
 * @return Time in milliseconds the error has been active, or 0 if inactive.
 */
uint32_t encoderMotionGetErrorDuration(uint8_t axis); // <-- NEW PROTOYPE

/**
 * Reset error tracking for an axis
 * * @param axis Axis index (0-3)
 */
void encoderMotionResetError(uint8_t axis);

/**
 * Check if axis has position error alarm
 * * @param axis Axis index (0-3)
 * @return true if error exceeds threshold
 */
bool encoderMotionHasError(uint8_t axis);

/**
 * Get total error events for an axis
 * * @param axis Axis index (0-3)
 * @return Count of error events
 */
uint32_t encoderMotionGetErrorCount(uint8_t axis);

/**
 * Enable/disable encoder feedback for position correction
 * * @param enable true to use encoder as position source, false for motion calc only
 */
void encoderMotionEnableFeedback(bool enable);

/**
 * Check if encoder feedback is active
 * * @return true if encoders are being used for position updates
 */
bool encoderMotionIsFeedbackActive();

/**
 * Get encoder-motion diagnostics
 * Prints comprehensive status to serial
 */
void encoderMotionDiagnostics();

#endif // ENCODER_MOTION_INTEGRATION_H

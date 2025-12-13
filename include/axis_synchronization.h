/**
 * @file axis_synchronization.h
 * @brief Per-Axis Motion Validation System (PHASE 5.6)
 * @project BISSO E350 Controller
 * @details Validates individual axis motion quality using VFD frequency feedback and encoder velocities
 *          Single VFD multiplexed across X/Y/Z axes via contactors - validates active axis only
 *          Detects mechanical degradation, bearing wear, and drive system faults
 *
 * Key Features:
 * - VFD/encoder correlation for active axis
 * - Per-axis motion quality scoring
 * - Velocity jitter detection (bearing wear indicator)
 * - Axis stall/jam detection
 * - Historical quality tracking per axis
 */

#ifndef AXIS_SYNCHRONIZATION_H
#define AXIS_SYNCHRONIZATION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SINGLE AXIS METRICS STRUCTURE
// ============================================================================

typedef struct {
    // Current motion state
    float current_velocity_mms;     // Current axis velocity (mm/s)
    float vfd_frequency_hz;         // VFD frequency when this axis is active (Hz)
    float commanded_feedrate_mms;   // Target feedrate for this axis (mm/s)

    // Motion quality indicators
    float velocity_jitter_mms;      // Peak-to-peak velocity variation
    float vfd_encoder_error_percent; // VFD vs encoder mismatch (0-100%)
    bool is_moving;                 // True if axis currently moving
    bool stalled;                   // True if commanded but not moving
    bool jitter_elevated;           // True if jitter above threshold

    // Historical quality metrics
    uint32_t quality_score;         // 0-100 (100 = perfect motion)
    uint32_t good_motion_samples;   // Consecutive good samples
    uint32_t bad_motion_samples;    // Consecutive bad samples
    uint32_t stall_count;           // Total stall events recorded

    // Timing
    uint32_t last_update_ms;        // Timestamp of last validation
    uint32_t active_duration_ms;    // How long this axis has been active
    float max_jitter_recorded_mms;  // Peak jitter amplitude (wear trend)

} axis_metrics_t;

// ============================================================================
// ALL AXES STATE (X, Y, Z)
// ============================================================================

typedef struct {
    axis_metrics_t x_axis;          // X-axis metrics
    axis_metrics_t y_axis;          // Y-axis metrics
    axis_metrics_t z_axis;          // Z-axis metrics
    uint8_t active_axis;            // Currently active axis (0=X, 1=Y, 2=Z, 255=none)
} all_axes_metrics_t;

// ============================================================================
// CONFIGURATION & THRESHOLDS
// ============================================================================

typedef struct {
    // VFD/encoder correlation tolerance (single VFD multiplexed to axes)
    float vfd_encoder_tolerance_percent;    // Max allowed mismatch (default 15%)
    float encoder_stall_threshold_mms;      // Min velocity below threshold = stalled (default 0.1 mm/s)

    // Jitter detection (bearing wear indicator)
    float jitter_threshold_mms;     // Velocity jitter above this triggers alert (default 0.5 mm/s)
    uint32_t jitter_window_ms;      // Rolling window for jitter detection (default 500ms)

    // Quality scoring
    uint32_t good_samples_for_quality;      // Samples needed for "good" rating (default 10)
    uint32_t bad_samples_for_alert;         // Bad samples before alert (default 3)

} axis_sync_config_t;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize axis synchronization validation system
 */
void axisSynchronizationInit(void);

/**
 * @brief Load synchronization configuration from NVS
 * @return true if configuration loaded
 */
bool axisSynchronizationLoadConfig(void);

/**
 * @brief Save synchronization configuration to NVS
 */
void axisSynchronizationSaveConfig(void);

/**
 * @brief Reset synchronization thresholds to defaults
 */
void axisSynchronizationResetDefaults(void);

// ============================================================================
// CONFIGURATION ACCESSORS
// ============================================================================

/**
 * @brief Get current synchronization configuration
 */
const axis_sync_config_t* axisSynchronizationGetConfig(void);

/**
 * @brief Set VFD/encoder correlation tolerance for active axis
 * @param tolerance_percent Maximum allowed error (0-50%, default 15%)
 */
void axisSynchronizationSetVFDEncoderTolerance(float tolerance_percent);

/**
 * @brief Set axis stall detection threshold
 * @param threshold_mms Minimum velocity to not be stalled (default 0.1 mm/s)
 */
void axisSynchronizationSetStallThreshold(float threshold_mms);

/**
 * @brief Set jitter detection threshold
 * @param threshold_mms Jitter amplitude threshold (mm/s)
 */
void axisSynchronizationSetJitterThreshold(float threshold_mms);

// ============================================================================
// REAL-TIME VALIDATION (Called from telemetry task)
// ============================================================================

/**
 * @brief Update axis validation for all axes
 * @details Called every 100-200ms from telemetry task
 * @param active_axis Currently active axis (0=X, 1=Y, 2=Z, 255=none)
 * @param x_velocity_mms X-axis current velocity (mm/s)
 * @param y_velocity_mms Y-axis current velocity (mm/s)
 * @param z_velocity_mms Z-axis current velocity (mm/s)
 * @param vfd_frequency_hz VFD output frequency (Hz)
 * @param commanded_feedrate_mms Target feedrate for active axis (mm/s)
 */
void axisSynchronizationUpdate(uint8_t active_axis,
                               float x_velocity_mms, float y_velocity_mms, float z_velocity_mms,
                               float vfd_frequency_hz, float commanded_feedrate_mms);

/**
 * @brief Check if currently active axis motion is valid
 * @return true if active axis performance is acceptable
 */
bool axisSynchronizationIsValid(void);

/**
 * @brief Get quality score for specific axis
 * @param axis 0=X, 1=Y, 2=Z
 * @return Score 0-100 (100 = excellent)
 */
uint32_t axisSynchronizationGetQualityScore(uint8_t axis);

/**
 * @brief Get metrics for all axes
 * @return Pointer to all axes metrics structure
 */
const all_axes_metrics_t* axisSynchronizationGetAllMetrics(void);

/**
 * @brief Get metrics for specific axis
 * @param axis 0=X, 1=Y, 2=Z
 * @return Pointer to axis metrics
 */
const axis_metrics_t* axisSynchronizationGetAxisMetrics(uint8_t axis);

// ============================================================================
// THREAD SAFETY (For multi-task access)
// ============================================================================

/**
 * @brief Lock axis metrics for safe concurrent access
 * @details Call before reading metrics from multiple tasks
 */
void axisSynchronizationLock(void);

/**
 * @brief Unlock axis metrics
 * @details Call after finished reading metrics
 */
void axisSynchronizationUnlock(void);

// ============================================================================
// SPECIFIC VALIDATIONS (For active axis only)
// ============================================================================

/**
 * @brief Check if VFD frequency matches encoder velocity for active axis
 * @details Validates single VFD is driving the currently selected motor
 * @return true if VFD and active axis encoder are correlated
 */
bool axisSynchronizationCheckVFDEncoderCorrelation(void);

/**
 * @brief Detect velocity jitter on active axis
 * @details Rising jitter often indicates bearing wear or mechanical looseness
 * @return true if jitter above threshold detected on active axis
 */
bool axisSynchronizationDetectJitter(void);

/**
 * @brief Check if active axis is stalled
 * @details Commanded to move but velocity below threshold
 * @return true if axis stalled
 */
bool axisSynchronizationDetectStall(void);

/**
 * @brief Get current VFD/encoder error as percentage for active axis
 * @return Error percentage (0-100%)
 */
float axisSynchronizationGetVFDEncoderError(void);

// ============================================================================
// DIAGNOSTICS & REPORTING
// ============================================================================

/**
 * @brief Print all axes motion quality summary to serial console
 */
void axisSynchronizationPrintSummary(void);

/**
 * @brief Print detailed diagnostics for specific axis
 * @param axis 0=X, 1=Y, 2=Z
 */
void axisSynchronizationPrintAxisDiagnostics(uint8_t axis);

/**
 * @brief Get human-readable quality status for axis
 * @param axis 0=X, 1=Y, 2=Z
 * @return String: "EXCELLENT", "GOOD", "FAIR", or "POOR"
 */
const char* axisSynchronizationGetStatusString(uint8_t axis);

/**
 * @brief Reset quality metrics for specific axis
 * @param axis 0=X, 1=Y, 2=Z
 */
void axisSynchronizationResetAxis(uint8_t axis);

#ifdef __cplusplus
}
#endif

#endif // AXIS_SYNCHRONIZATION_H

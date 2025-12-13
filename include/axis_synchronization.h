/**
 * @file axis_synchronization.h
 * @brief Multi-Axis Synchronization & Validation System (PHASE 5.6)
 * @project BISSO E350 Controller
 * @details Validates axis motion consistency using VFD frequency feedback and encoder velocities
 *          Detects axis skew, speed mismatches, and mechanical degradation patterns
 *
 * Key Features:
 * - VFD frequency vs encoder velocity correlation
 * - Cross-axis synchronization validation (XY axes)
 * - Motion jitter detection (mechanical wear indicator)
 * - Real-time motion quality metrics
 * - Predictive diagnostics for mechanical issues
 */

#ifndef AXIS_SYNCHRONIZATION_H
#define AXIS_SYNCHRONIZATION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MOTION QUALITY METRICS STRUCTURE
// ============================================================================

typedef struct {
    // Axis velocity measurements (mm/s)
    float x_velocity_mms;           // X-axis current velocity
    float y_velocity_mms;           // Y-axis current velocity
    float z_velocity_mms;           // Z-axis current velocity

    // VFD frequency feedback
    float vfd_frequency_hz;         // VFD output frequency (Hz)
    float expected_frequency_hz;    // Expected frequency for given feedrate

    // Synchronization metrics
    float xy_velocity_error_percent;    // Cross-axis velocity mismatch (0-100%)
    float vfd_encoder_correlation;      // VFD vs encoder correlation (0-100%)
    bool axes_synchronized;             // True if all axes within tolerance

    // Motion quality indicators
    float jitter_amplitude_mms;     // Peak-to-peak velocity jitter
    uint32_t jitter_spike_count;    // Number of jitter events in window
    bool jitter_detected;           // True if mechanical wear suspected

    // Mechanical health
    float spindle_load_percent;     // VFD current as % of max rated
    uint32_t motion_quality_score;  // 0-100 (100 = perfect synchronization)

    // Timing
    uint32_t last_update_ms;        // Timestamp of last validation update
    uint32_t synchronized_duration_ms;  // How long axes have been synchronized
    uint32_t desync_count;          // Total desynchronization events

} axis_metrics_t;

// ============================================================================
// CONFIGURATION & THRESHOLDS
// ============================================================================

typedef struct {
    // Synchronization tolerance
    float xy_velocity_tolerance_percent;    // Max allowed XY speed mismatch (default 5%)
    float vfd_encoder_tolerance_percent;    // Max VFD/encoder correlation error (default 10%)

    // Jitter detection
    float jitter_threshold_mms;     // Velocity jitter above this triggers alert (default 0.5 mm/s)
    uint32_t jitter_window_ms;      // Rolling window for jitter detection (default 500ms)

    // Quality scoring
    uint32_t min_synchronized_ms;   // Min duration before "good" quality (default 2000ms)
    uint32_t max_desync_events;     // Max desync events in quality window (default 3)

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
 * @brief Set XY velocity tolerance threshold
 * @param tolerance_percent Maximum allowed mismatch (0-20%)
 */
void axisSynchronizationSetXYTolerance(float tolerance_percent);

/**
 * @brief Set VFD/encoder correlation tolerance
 * @param tolerance_percent Maximum correlation error (0-50%)
 */
void axisSynchronizationSetVFDEncoderTolerance(float tolerance_percent);

/**
 * @brief Set jitter detection threshold
 * @param threshold_mms Jitter amplitude threshold (mm/s)
 */
void axisSynchronizationSetJitterThreshold(float threshold_mms);

// ============================================================================
// REAL-TIME VALIDATION (Called from motion/telemetry tasks)
// ============================================================================

/**
 * @brief Update axis metrics with current motion state
 * @details Call every 10-100ms from telemetry task with current axis velocities
 * @param x_velocity_mms X-axis velocity (mm/s)
 * @param y_velocity_mms Y-axis velocity (mm/s)
 * @param z_velocity_mms Z-axis velocity (mm/s)
 * @param current_feedrate_mms Expected feedrate (mm/s) for motion
 */
void axisSynchronizationUpdate(float x_velocity_mms, float y_velocity_mms,
                               float z_velocity_mms, float current_feedrate_mms);

/**
 * @brief Check if axes are currently synchronized
 * @return true if all axes within tolerance
 */
bool axisSynchronizationIsValid(void);

/**
 * @brief Get current motion quality score
 * @return Score 0-100 (100 = perfect synchronization)
 */
uint32_t axisSynchronizationGetQualityScore(void);

/**
 * @brief Get current metrics snapshot
 * @return Pointer to current axis metrics
 */
const axis_metrics_t* axisSynchronizationGetMetrics(void);

// ============================================================================
// SPECIFIC VALIDATIONS
// ============================================================================

/**
 * @brief Check if XY axes are moving at same speed
 * @return true if synchronized within tolerance
 */
bool axisSynchronizationCheckXYMatch(void);

/**
 * @brief Check if VFD frequency matches encoder velocity
 * @details Validates motor is actually driving the mechanical system
 * @return true if VFD and encoders are correlated
 */
bool axisSynchronizationCheckVFDEncoderCorrelation(void);

/**
 * @brief Detect velocity jitter (mechanical wear indicator)
 * @details Rising jitter often indicates bearing wear or mechanical looseness
 * @return true if jitter above threshold detected
 */
bool axisSynchronizationDetectJitter(void);

/**
 * @brief Get current synchronization error as percentage
 * @param axis 0=X, 1=Y, 2=Z
 * @return Error percentage (0-100%)
 */
float axisSynchronizationGetAxisError(uint8_t axis);

// ============================================================================
// DIAGNOSTICS & REPORTING
// ============================================================================

/**
 * @brief Print axis metrics summary to serial console
 */
void axisSynchronizationPrintSummary(void);

/**
 * @brief Print detailed diagnostics
 */
void axisSynchronizationPrintDiagnostics(void);

/**
 * @brief Get human-readable quality status
 * @return String description of motion quality
 */
const char* axisSynchronizationGetStatusString(void);

/**
 * @brief Reset synchronization metrics for new motion cycle
 */
void axisSynchronizationReset(void);

#ifdef __cplusplus
}
#endif

#endif // AXIS_SYNCHRONIZATION_H

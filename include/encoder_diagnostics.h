/**
 * @file encoder_diagnostics.h
 * @brief Advanced Encoder Diagnostics and Health Monitoring (PHASE 5.3)
 * @details Position variance tracking, drift detection, signal quality
 * @project BISSO E350 Controller
 */

#ifndef ENCODER_DIAGNOSTICS_H
#define ENCODER_DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encoder health status
 */
typedef enum {
    ENCODER_HEALTH_OPTIMAL = 0,     // No issues detected
    ENCODER_HEALTH_NORMAL = 1,      // Minor variance, acceptable
    ENCODER_HEALTH_DEGRADED = 2,    // Noticeable drift, recalibration recommended
    ENCODER_HEALTH_CRITICAL = 3     // Severe drift, calibration required
} encoder_health_t;

/**
 * Encoder diagnostic data for single axis
 */
typedef struct {
    uint8_t axis_id;                // Axis (0=X, 1=Y, 2=Z, 3=A)
    encoder_health_t health;        // Overall health status

    // Position tracking
    float position_mm;              // Current position
    float position_variance_mm;     // Variance from expected (accumulated error)
    float max_variance_mm;          // Peak variance since boot

    // Drift analysis
    float drift_per_hour_mm;        // Estimated drift rate
    float drift_direction;          // +1 for positive drift, -1 for negative
    uint32_t drift_samples;         // Number of drift measurements

    // Signal quality (0-100%)
    uint8_t signal_quality;         // 0-100 signal quality indicator
    uint32_t signal_errors;         // Number of signal errors detected
    uint32_t last_error_ms;         // Time of last error (0 if none)

    // Calibration state
    bool needs_recalibration;       // True if calibration check recommended
    uint32_t last_calibration_age_hours;  // Hours since last calibration

    // Statistics
    uint32_t read_count;            // Number of successful reads
    uint32_t error_count;           // Number of read errors
    float error_rate;               // Percentage of failed reads
    uint32_t last_update_ms;        // Timestamp of last update
} encoder_diagnostic_t;

/**
 * Initialize encoder diagnostics module
 */
void encoderDiagnosticsInit();

/**
 * Update diagnostics for all axes
 * Call periodically from monitor task
 */
void encoderDiagnosticsUpdate();

/**
 * Get diagnostic data for specific axis
 * @param axis_id Axis (0-3)
 * @return Diagnostic data
 */
const encoder_diagnostic_t* encoderDiagnosticsGetAxis(uint8_t axis_id);

/**
 * Get health status for axis
 * @param axis_id Axis (0-3)
 * @return Health status
 */
encoder_health_t encoderDiagnosticsGetHealth(uint8_t axis_id);

/**
 * Run calibration verification test
 * Moves axis small distance and compares encoder vs stepper count
 * @param axis_id Axis to test
 * @param distance_mm Distance to move (typically 10mm)
 * @return true if calibration acceptable
 */
bool encoderDiagnosticsVerifyCalibration(uint8_t axis_id, float distance_mm);

/**
 * Analyze signal quality over measurement period
 * @param axis_id Axis to analyze
 * @param duration_ms How long to analyze (e.g., 10000 for 10 seconds)
 * @return Signal quality 0-100
 */
uint8_t encoderDiagnosticsAnalyzeSignal(uint8_t axis_id, uint32_t duration_ms);

/**
 * Export diagnostics as JSON
 * @param buffer Output buffer
 * @param buffer_size Max size
 * @return Bytes written
 */
size_t encoderDiagnosticsExportJSON(char* buffer, size_t buffer_size);

/**
 * Print diagnostics to serial
 */
void encoderDiagnosticsPrint();

/**
 * Get human-readable health status string
 * @param health Status enum
 * @return Status string
 */
const char* encoderDiagnosticsGetHealthString(encoder_health_t health);

/**
 * Reset diagnostic counters
 * @param axis_id Axis to reset, or 0xFF for all
 */
void encoderDiagnosticsReset(uint8_t axis_id);

#ifdef __cplusplus
}
#endif

#endif // ENCODER_DIAGNOSTICS_H

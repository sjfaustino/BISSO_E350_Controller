/**
 * @file vfd_current_calibration.h
 * @brief VFD Motor Current Calibration System (PHASE 5.5)
 * @project BISSO E350 Controller
 * @details Operator-guided current baseline measurement and stall detection threshold
 *
 * Calibration workflow:
 * 1. Idle baseline - Blade running, no cutting (~5-10A typical)
 * 2. Standard cut baseline - Cutting stone at standard speed (~20-25A typical)
 * 3. (Optional) Heavy load - Higher speed/load for worst-case measurement
 * 4. Calculate stall threshold = heavy_cut_peak + 20% margin
 *
 * All measurements stored in NVS config, persists across reboots.
 */

#ifndef VFD_CURRENT_CALIBRATION_H
#define VFD_CURRENT_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CALIBRATION DATA STRUCTURE
// ============================================================================

typedef struct {
    // Idle baseline (blade spinning, no cutting)
    float idle_rms_amps;                // RMS current average
    float idle_peak_amps;               // Peak current spike

    // Standard cutting baseline (reference load)
    float standard_cut_rms_amps;        // RMS current average
    float standard_cut_peak_amps;       // Peak current spike

    // Heavy load (optional, high speed/load)
    float heavy_cut_rms_amps;           // RMS current average
    float heavy_cut_peak_amps;          // Peak current spike

    // Calculated stall detection threshold
    float stall_threshold_amps;         // Stall detected if current > this
    float stall_margin_percent;         // Margin above max measured (default 20%)

    // Calibration metadata
    uint32_t last_calibration_ms;       // Timestamp of last calibration
    bool is_calibrated;                 // True if valid calibration exists
    uint32_t calibration_count;         // How many times calibrated

} vfd_calibration_data_t;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize calibration system (called at startup)
 */
void vfdCalibrationInit(void);

/**
 * @brief Load calibration data from NVS config
 * @return true if valid calibration loaded
 */
bool vfdCalibrationLoad(void);

/**
 * @brief Save calibration data to NVS config
 * @return true if successful
 */
bool vfdCalibrationSave(void);

// ============================================================================
// MEASUREMENT COLLECTION
// ============================================================================

/**
 * @brief Start measuring current for calibration phase
 * @param duration_ms How long to measure (e.g., 10000 for 10 seconds)
 * @param phase_name Display name ("Idle Baseline", "Standard Cut", etc.)
 */
void vfdCalibrationStartMeasure(uint32_t duration_ms, const char* phase_name);

/**
 * @brief Check if measurement is complete
 * @return true when measurement duration has elapsed
 */
bool vfdCalibrationIsMeasureComplete(void);

/**
 * @brief Get results from last measurement
 * @param out_rms_amps Measured RMS average current
 * @param out_peak_amps Measured peak current
 * @return true if measurement was successful
 */
bool vfdCalibrationGetMeasurement(float* out_rms_amps, float* out_peak_amps);

/**
 * @brief Store measurement result for specific phase
 * @param phase 0=idle, 1=standard cut, 2=heavy load
 * @param rms_amps RMS average current
 * @param peak_amps Peak current
 */
void vfdCalibrationStoreMeasurement(uint8_t phase, float rms_amps, float peak_amps);

/**
 * @brief Calculate stall threshold from collected measurements
 * @param margin_percent Margin above max measurement (default 20)
 * @return true if threshold calculated successfully
 */
bool vfdCalibrationCalculateThreshold(float margin_percent);

// ============================================================================
// STALL DETECTION
// ============================================================================

/**
 * @brief Check if motor current indicates a stall
 * @param current_amps Current motor current from VFD
 * @return true if current exceeds stall threshold
 */
bool vfdCalibrationIsStall(float current_amps);

/**
 * @brief Get configured stall threshold
 * @return Stall threshold in amperes (0.0 if not calibrated)
 */
float vfdCalibrationGetThreshold(void);

/**
 * @brief Get current margin (percentage above baseline)
 * @return Margin in percent (e.g., 20 for 20%)
 */
float vfdCalibrationGetMargin(void);

/**
 * @brief Check if calibration is valid
 * @return true if at least idle and standard cut baselines are set
 */
bool vfdCalibrationIsValid(void);

/**
 * @brief Reset all calibration data
 */
void vfdCalibrationReset(void);

// ============================================================================
// DIAGNOSTICS
// ============================================================================

/**
 * @brief Get calibration data snapshot
 * @return Current calibration structure
 */
const vfd_calibration_data_t* vfdCalibrationGetData(void);

/**
 * @brief Print calibration summary to serial for user confirmation
 */
void vfdCalibrationPrintSummary(void);

#ifdef __cplusplus
}
#endif

#endif // VFD_CURRENT_CALIBRATION_H

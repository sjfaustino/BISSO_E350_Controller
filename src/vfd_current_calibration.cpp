/**
 * @file vfd_current_calibration.cpp
 * @brief VFD Motor Current Calibration System Implementation (PHASE 5.5)
 * @project BISSO E350 Controller
 * @details Operator-guided baseline current measurement for stall detection
 */

#include "vfd_current_calibration.h"
#include "config_keys.h"
#include "config_unified.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ============================================================================
// MEASUREMENT STATE MACHINE
// ============================================================================

typedef struct {
    bool active;                        // Measurement in progress
    uint32_t start_time_ms;             // Timestamp when measurement started
    uint32_t duration_ms;               // Target measurement duration
    char phase_name[32];                // Current measurement phase name

    // Current samples during measurement
    float current_sum;                  // Sum of sampled currents (for RMS)
    float current_max;                  // Maximum current spike
    uint32_t sample_count;              // Number of samples collected
} measure_state_t;

// ============================================================================
// MODULE STATE
// ============================================================================

static vfd_calibration_data_t calib_data = {
    .idle_rms_amps = 0.0f,
    .idle_peak_amps = 0.0f,
    .standard_cut_rms_amps = 0.0f,
    .standard_cut_peak_amps = 0.0f,
    .heavy_cut_rms_amps = 0.0f,
    .heavy_cut_peak_amps = 0.0f,
    .stall_threshold_amps = 0.0f,
    .stall_margin_percent = 20.0f,
    .last_calibration_ms = 0,
    .is_calibrated = false,
    .calibration_count = 0
};

static measure_state_t measure_state = {};  // Initialize to zero/default values

// ============================================================================
// INITIALIZATION & PERSISTENCE
// ============================================================================

void vfdCalibrationInit(void) {
    Serial.println("[VFDCAL] Initializing calibration system");
    memset(&calib_data, 0, sizeof(calib_data));
    memset(&measure_state, 0, sizeof(measure_state));
    calib_data.stall_margin_percent = 20.0f;

    // Attempt to load existing calibration from NVS
    if (!vfdCalibrationLoad()) {
        Serial.println("[VFDCAL] No valid calibration found, starting fresh");
        calib_data.is_calibrated = false;
    } else {
        Serial.println("[VFDCAL] Loaded existing calibration from NVS");
    }
}

bool vfdCalibrationLoad(void) {
    // Try to load each field from NVS config
    int32_t idle_rms_i = configGetInt(KEY_VFD_IDLE_RMS, -1);
    int32_t idle_pk_i = configGetInt(KEY_VFD_IDLE_PEAK, -1);
    int32_t std_rms_i = configGetInt(KEY_VFD_STD_CUT_RMS, -1);
    int32_t std_pk_i = configGetInt(KEY_VFD_STD_CUT_PEAK, -1);
    int32_t valid_i = configGetInt(KEY_VFD_CALIB_VALID, 0);

    // Check if we have minimum calibration (idle + standard cut)
    if (idle_rms_i < 0 || idle_pk_i < 0 || std_rms_i < 0 || std_pk_i < 0) {
        return false;  // Not enough data
    }

    // Load confirmed data
    calib_data.idle_rms_amps = idle_rms_i / 100.0f;
    calib_data.idle_peak_amps = idle_pk_i / 100.0f;
    calib_data.standard_cut_rms_amps = std_rms_i / 100.0f;
    calib_data.standard_cut_peak_amps = std_pk_i / 100.0f;

    // Load optional heavy load data
    int32_t heavy_rms_i = configGetInt(KEY_VFD_HEAVY_RMS, -1);
    int32_t heavy_pk_i = configGetInt(KEY_VFD_HEAVY_PEAK, -1);
    if (heavy_rms_i >= 0 && heavy_pk_i >= 0) {
        calib_data.heavy_cut_rms_amps = heavy_rms_i / 100.0f;
        calib_data.heavy_cut_peak_amps = heavy_pk_i / 100.0f;
    }

    // Load threshold and margin
    int32_t thr_i = configGetInt(KEY_VFD_STALL_THR, -1);
    int32_t marg_i = configGetInt(KEY_VFD_STALL_MARGIN, 20);
    if (thr_i >= 0) {
        calib_data.stall_threshold_amps = thr_i / 100.0f;
    }
    calib_data.stall_margin_percent = marg_i;

    calib_data.is_calibrated = (valid_i == 1);
    calib_data.calibration_count = configGetInt("vfd_calib_cnt", 0);

    Serial.printf("[VFDCAL] Loaded: Idle=%.1f/%.1f A, Std=%.1f/%.1f A, Threshold=%.1f A\n",
                  calib_data.idle_rms_amps, calib_data.idle_peak_amps,
                  calib_data.standard_cut_rms_amps, calib_data.standard_cut_peak_amps,
                  calib_data.stall_threshold_amps);

    return true;
}

bool vfdCalibrationSave(void) {
    // Save all calibration values to NVS config
    configSetInt(KEY_VFD_IDLE_RMS, (int32_t)(calib_data.idle_rms_amps * 100.0f));
    configSetInt(KEY_VFD_IDLE_PEAK, (int32_t)(calib_data.idle_peak_amps * 100.0f));
    configSetInt(KEY_VFD_STD_CUT_RMS, (int32_t)(calib_data.standard_cut_rms_amps * 100.0f));
    configSetInt(KEY_VFD_STD_CUT_PEAK, (int32_t)(calib_data.standard_cut_peak_amps * 100.0f));
    configSetInt(KEY_VFD_HEAVY_RMS, (int32_t)(calib_data.heavy_cut_rms_amps * 100.0f));
    configSetInt(KEY_VFD_HEAVY_PEAK, (int32_t)(calib_data.heavy_cut_peak_amps * 100.0f));
    configSetInt(KEY_VFD_STALL_THR, (int32_t)(calib_data.stall_threshold_amps * 100.0f));
    configSetInt(KEY_VFD_STALL_MARGIN, (int32_t)calib_data.stall_margin_percent);
    configSetInt(KEY_VFD_CALIB_VALID, calib_data.is_calibrated ? 1 : 0);
    configSetInt("vfd_calib_cnt", calib_data.calibration_count);

    // Persist to NVS
    configUnifiedFlush();
    configUnifiedSave();

    Serial.printf("[VFDCAL] Saved calibration data (%u times)\n", calib_data.calibration_count);
    return true;
}

// ============================================================================
// MEASUREMENT COLLECTION
// ============================================================================

void vfdCalibrationStartMeasure(uint32_t duration_ms, const char* phase_name) {
    measure_state.active = true;
    measure_state.start_time_ms = millis();
    measure_state.duration_ms = duration_ms;
    measure_state.current_sum = 0.0f;
    measure_state.current_max = 0.0f;
    measure_state.sample_count = 0;

    strncpy(measure_state.phase_name, phase_name, sizeof(measure_state.phase_name) - 1);
    measure_state.phase_name[sizeof(measure_state.phase_name) - 1] = '\0';

    Serial.printf("[VFDCAL] Starting measurement: %s (duration: %lu ms)\n",
                  measure_state.phase_name, (unsigned long)duration_ms);
}

bool vfdCalibrationIsMeasureComplete(void) {
    if (!measure_state.active) {
        return false;
    }

    uint32_t elapsed = millis() - measure_state.start_time_ms;
    if (elapsed >= measure_state.duration_ms) {
        measure_state.active = false;
        Serial.printf("[VFDCAL] Measurement complete: %lu samples collected\n",
                      (unsigned long)measure_state.sample_count);
        return true;
    }

    return false;
}

bool vfdCalibrationGetMeasurement(float* out_rms_amps, float* out_peak_amps) {
    if (out_rms_amps == NULL || out_peak_amps == NULL) {
        return false;
    }

    if (measure_state.sample_count == 0) {
        return false;  // No samples collected
    }

    // Calculate RMS from sum of samples
    float rms = measure_state.current_sum / measure_state.sample_count;
    *out_rms_amps = rms;
    *out_peak_amps = measure_state.current_max;

    Serial.printf("[VFDCAL] Measurement result: RMS=%.2f A, Peak=%.2f A (samples: %lu)\n",
                  rms, measure_state.current_max, (unsigned long)measure_state.sample_count);

    return true;
}

void vfdCalibrationStoreMeasurement(uint8_t phase, float rms_amps, float peak_amps) {
    switch (phase) {
        case 0:  // Idle baseline
            calib_data.idle_rms_amps = rms_amps;
            calib_data.idle_peak_amps = peak_amps;
            Serial.printf("[VFDCAL] Stored idle baseline: %.2f A (RMS), %.2f A (peak)\n",
                          rms_amps, peak_amps);
            break;

        case 1:  // Standard cut baseline
            calib_data.standard_cut_rms_amps = rms_amps;
            calib_data.standard_cut_peak_amps = peak_amps;
            Serial.printf("[VFDCAL] Stored standard cut baseline: %.2f A (RMS), %.2f A (peak)\n",
                          rms_amps, peak_amps);
            break;

        case 2:  // Heavy load (optional)
            calib_data.heavy_cut_rms_amps = rms_amps;
            calib_data.heavy_cut_peak_amps = peak_amps;
            Serial.printf("[VFDCAL] Stored heavy load baseline: %.2f A (RMS), %.2f A (peak)\n",
                          rms_amps, peak_amps);
            break;

        default:
            return;
    }
}

// ============================================================================
// THRESHOLD CALCULATION
// ============================================================================

bool vfdCalibrationCalculateThreshold(float margin_percent) {
    // Use the highest measurement (prefer heavy load if available, otherwise standard cut)
    float max_peak = calib_data.standard_cut_peak_amps;

    if (calib_data.heavy_cut_peak_amps > 0.0f) {
        max_peak = calib_data.heavy_cut_peak_amps;
    }

    if (max_peak <= 0.0f) {
        Serial.println("[VFDCAL] ERROR: No valid baseline measurements for threshold calculation");
        return false;
    }

    // Calculate stall threshold: max_peak + margin
    float margin_factor = 1.0f + (margin_percent / 100.0f);
    calib_data.stall_threshold_amps = max_peak * margin_factor;
    calib_data.stall_margin_percent = margin_percent;
    calib_data.last_calibration_ms = millis();
    calib_data.is_calibrated = true;
    calib_data.calibration_count++;

    Serial.printf("[VFDCAL] Calculated stall threshold: %.2f A (max peak: %.2f A, margin: %.0f%%)\n",
                  calib_data.stall_threshold_amps, max_peak, margin_percent);

    return vfdCalibrationSave();
}

// ============================================================================
// STALL DETECTION
// ============================================================================

bool vfdCalibrationIsStall(float current_amps) {
    if (!calib_data.is_calibrated) {
        return false;  // Can't detect stall without calibration
    }

    return current_amps > calib_data.stall_threshold_amps;
}

float vfdCalibrationGetThreshold(void) {
    return calib_data.stall_threshold_amps;
}

float vfdCalibrationGetMargin(void) {
    return calib_data.stall_margin_percent;
}

bool vfdCalibrationIsValid(void) {
    return calib_data.is_calibrated &&
           calib_data.idle_rms_amps > 0.0f &&
           calib_data.standard_cut_rms_amps > 0.0f;
}

void vfdCalibrationReset(void) {
    Serial.println("[VFDCAL] Resetting all calibration data");
    memset(&calib_data, 0, sizeof(calib_data));
    calib_data.stall_margin_percent = 20.0f;
    calib_data.is_calibrated = false;

    // Clear from NVS
    configSetInt(KEY_VFD_CALIB_VALID, 0);
    configUnifiedFlush();
    configUnifiedSave();
}

// ============================================================================
// PUBLIC API - FEED CURRENT SAMPLE
// ============================================================================

/**
 * @brief Feed current sample to measurement system (called from telemetry task)
 * @param current_amps Current motor current in amperes
 */
void vfdCalibrationSampleCurrent(float current_amps) {
    if (!measure_state.active) {
        return;  // Not currently measuring
    }

    measure_state.current_sum += current_amps;
    if (current_amps > measure_state.current_max) {
        measure_state.current_max = current_amps;
    }
    measure_state.sample_count++;
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

const vfd_calibration_data_t* vfdCalibrationGetData(void) {
    return &calib_data;
}

void vfdCalibrationPrintSummary(void) {
    Serial.println("\n[VFDCAL] === Current Calibration Summary ===");
    Serial.printf("Status:              %s\n", calib_data.is_calibrated ? "VALID" : "INVALID");
    Serial.printf("Calibration Count:   %lu\n", (unsigned long)calib_data.calibration_count);

    if (calib_data.idle_rms_amps > 0.0f || calib_data.idle_peak_amps > 0.0f) {
        Serial.printf("Idle Baseline:       %.2f A (RMS) / %.2f A (peak)\n",
                      calib_data.idle_rms_amps, calib_data.idle_peak_amps);
    } else {
        Serial.println("Idle Baseline:       Not measured");
    }

    if (calib_data.standard_cut_rms_amps > 0.0f || calib_data.standard_cut_peak_amps > 0.0f) {
        Serial.printf("Standard Cut:        %.2f A (RMS) / %.2f A (peak)\n",
                      calib_data.standard_cut_rms_amps, calib_data.standard_cut_peak_amps);
    } else {
        Serial.println("Standard Cut:        Not measured");
    }

    if (calib_data.heavy_cut_rms_amps > 0.0f || calib_data.heavy_cut_peak_amps > 0.0f) {
        Serial.printf("Heavy Load:          %.2f A (RMS) / %.2f A (peak)\n",
                      calib_data.heavy_cut_rms_amps, calib_data.heavy_cut_peak_amps);
    } else {
        Serial.println("Heavy Load:          Not measured");
    }

    Serial.printf("Stall Threshold:     %.2f A (Margin: %.0f%%)\n",
                  calib_data.stall_threshold_amps, calib_data.stall_margin_percent);

    if (calib_data.last_calibration_ms > 0) {
        Serial.printf("Last Calibration:    %lu ms ago\n",
                      (unsigned long)(millis() - calib_data.last_calibration_ms));
    }
    Serial.println();
}

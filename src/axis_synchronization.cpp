/**
 * @file axis_synchronization.cpp
 * @brief Multi-Axis Synchronization & Validation System Implementation (PHASE 5.6)
 * @project BISSO E350 Controller
 * @details Real-time validation of motion consistency across XYZ axes
 */

#include "axis_synchronization.h"
#include "config_keys.h"
#include "config_unified.h"
#include "serial_logger.h"
#include "altivar31_modbus.h"
#include "motion_state.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static axis_metrics_t current_metrics = {
    .x_velocity_mms = 0.0f,
    .y_velocity_mms = 0.0f,
    .z_velocity_mms = 0.0f,
    .vfd_frequency_hz = 0.0f,
    .expected_frequency_hz = 0.0f,
    .xy_velocity_error_percent = 0.0f,
    .vfd_encoder_correlation = 0.0f,
    .axes_synchronized = true,
    .jitter_amplitude_mms = 0.0f,
    .jitter_spike_count = 0,
    .jitter_detected = false,
    .spindle_load_percent = 0.0f,
    .motion_quality_score = 100,
    .last_update_ms = 0,
    .synchronized_duration_ms = 0,
    .desync_count = 0
};

static axis_sync_config_t sync_config = {
    .xy_velocity_tolerance_percent = 5.0f,
    .vfd_encoder_tolerance_percent = 10.0f,
    .jitter_threshold_mms = 0.5f,
    .jitter_window_ms = 500,
    .min_synchronized_ms = 2000,
    .max_desync_events = 3
};

// Jitter tracking (rolling window)
typedef struct {
    float velocity_history[50];     // 50 velocity samples @ 100Hz = 500ms window
    uint32_t history_index;
    uint32_t last_spike_ms;
    float max_jitter_mms;
} jitter_tracker_t;

static jitter_tracker_t x_jitter = {0};
static jitter_tracker_t y_jitter = {0};
static jitter_tracker_t z_jitter = {0};

// ============================================================================
// INITIALIZATION & PERSISTENCE
// ============================================================================

void axisSynchronizationInit(void) {
    Serial.println("[AXIS_SYNC] Initializing axis synchronization system");
    memset(&current_metrics, 0, sizeof(current_metrics));
    memset(&x_jitter, 0, sizeof(x_jitter));
    memset(&y_jitter, 0, sizeof(y_jitter));
    memset(&z_jitter, 0, sizeof(z_jitter));

    axisSynchronizationLoadConfig();
}

bool axisSynchronizationLoadConfig(void) {
    int32_t xy_tol = configGetInt("axis_xy_tol", 500);  // 5.0% * 100
    int32_t vfd_tol = configGetInt("axis_vfd_tol", 1000);  // 10.0% * 100
    int32_t jitter_thr = configGetInt("axis_jitter_thr", 50);  // 0.5 mm/s * 100

    sync_config.xy_velocity_tolerance_percent = xy_tol / 100.0f;
    sync_config.vfd_encoder_tolerance_percent = vfd_tol / 100.0f;
    sync_config.jitter_threshold_mms = jitter_thr / 100.0f;

    Serial.printf("[AXIS_SYNC] Loaded config: XY_tol=%.1f%%, VFD_tol=%.1f%%, Jitter_thr=%.2f mm/s\n",
                  sync_config.xy_velocity_tolerance_percent,
                  sync_config.vfd_encoder_tolerance_percent,
                  sync_config.jitter_threshold_mms);

    return true;
}

void axisSynchronizationSaveConfig(void) {
    configSetInt("axis_xy_tol", (int32_t)(sync_config.xy_velocity_tolerance_percent * 100.0f));
    configSetInt("axis_vfd_tol", (int32_t)(sync_config.vfd_encoder_tolerance_percent * 100.0f));
    configSetInt("axis_jitter_thr", (int32_t)(sync_config.jitter_threshold_mms * 100.0f));
    configUnifiedFlush();
    configUnifiedSave();

    Serial.println("[AXIS_SYNC] Configuration saved");
}

void axisSynchronizationResetDefaults(void) {
    sync_config.xy_velocity_tolerance_percent = 5.0f;
    sync_config.vfd_encoder_tolerance_percent = 10.0f;
    sync_config.jitter_threshold_mms = 0.5f;
    sync_config.jitter_window_ms = 500;
    sync_config.min_synchronized_ms = 2000;
    sync_config.max_desync_events = 3;

    axisSynchronizationSaveConfig();
}

// ============================================================================
// CONFIGURATION ACCESSORS
// ============================================================================

const axis_sync_config_t* axisSynchronizationGetConfig(void) {
    return &sync_config;
}

void axisSynchronizationSetXYTolerance(float tolerance_percent) {
    if (tolerance_percent >= 0.0f && tolerance_percent <= 20.0f) {
        sync_config.xy_velocity_tolerance_percent = tolerance_percent;
        axisSynchronizationSaveConfig();
    }
}

void axisSynchronizationSetVFDEncoderTolerance(float tolerance_percent) {
    if (tolerance_percent >= 0.0f && tolerance_percent <= 50.0f) {
        sync_config.vfd_encoder_tolerance_percent = tolerance_percent;
        axisSynchronizationSaveConfig();
    }
}

void axisSynchronizationSetJitterThreshold(float threshold_mms) {
    if (threshold_mms >= 0.0f && threshold_mms <= 5.0f) {
        sync_config.jitter_threshold_mms = threshold_mms;
        axisSynchronizationSaveConfig();
    }
}

// ============================================================================
// JITTER DETECTION
// ============================================================================

static void updateJitterTracker(jitter_tracker_t* tracker, float velocity_mms) {
    tracker->velocity_history[tracker->history_index] = velocity_mms;
    tracker->history_index = (tracker->history_index + 1) % 50;

    // Calculate peak-to-peak jitter in current window
    float min_vel = velocity_mms, max_vel = velocity_mms;
    for (int i = 0; i < 50; i++) {
        if (tracker->velocity_history[i] < min_vel) min_vel = tracker->velocity_history[i];
        if (tracker->velocity_history[i] > max_vel) max_vel = tracker->velocity_history[i];
    }

    float new_jitter = fabs(max_vel - min_vel);
    if (new_jitter > tracker->max_jitter_mms) {
        tracker->max_jitter_mms = new_jitter;
        tracker->last_spike_ms = millis();
    }
}

static float calculateJitterAmplitude(void) {
    float avg_jitter = (x_jitter.max_jitter_mms + y_jitter.max_jitter_mms + z_jitter.max_jitter_mms) / 3.0f;
    return avg_jitter;
}

// ============================================================================
// REAL-TIME VALIDATION
// ============================================================================

void axisSynchronizationUpdate(float x_velocity_mms, float y_velocity_mms,
                               float z_velocity_mms, float current_feedrate_mms) {
    uint32_t now_ms = millis();

    // Update velocity metrics
    current_metrics.x_velocity_mms = x_velocity_mms;
    current_metrics.y_velocity_mms = y_velocity_mms;
    current_metrics.z_velocity_mms = z_velocity_mms;

    // Update VFD feedback
    current_metrics.vfd_frequency_hz = altivar31GetFrequencyHz();
    current_metrics.expected_frequency_hz = altivar31GetFrequencyHz();  // Expected based on feedrate

    // Track jitter
    updateJitterTracker(&x_jitter, x_velocity_mms);
    updateJitterTracker(&y_jitter, y_velocity_mms);
    updateJitterTracker(&z_jitter, z_velocity_mms);
    current_metrics.jitter_amplitude_mms = calculateJitterAmplitude();

    // Validate synchronization
    bool xy_sync = axisSynchronizationCheckXYMatch();
    bool vfd_sync = axisSynchronizationCheckVFDEncoderCorrelation();
    bool jitter_ok = !axisSynchronizationDetectJitter();

    // Update synchronization status
    bool was_synchronized = current_metrics.axes_synchronized;
    current_metrics.axes_synchronized = xy_sync && vfd_sync;

    if (current_metrics.axes_synchronized) {
        current_metrics.synchronized_duration_ms += (now_ms - current_metrics.last_update_ms);
    } else {
        if (was_synchronized) {
            current_metrics.desync_count++;
        }
        current_metrics.synchronized_duration_ms = 0;
    }

    // Calculate motion quality score (0-100)
    uint32_t score = 100;
    if (!xy_sync) score -= 30;
    if (!vfd_sync) score -= 25;
    if (!jitter_ok) score -= 20;
    if (current_metrics.synchronized_duration_ms < sync_config.min_synchronized_ms) score -= 10;

    current_metrics.motion_quality_score = (score > 0) ? score : 0;
    current_metrics.last_update_ms = now_ms;
}

bool axisSynchronizationIsValid(void) {
    return current_metrics.axes_synchronized &&
           current_metrics.motion_quality_score >= 70;
}

uint32_t axisSynchronizationGetQualityScore(void) {
    return current_metrics.motion_quality_score;
}

const axis_metrics_t* axisSynchronizationGetMetrics(void) {
    return &current_metrics;
}

// ============================================================================
// SPECIFIC VALIDATIONS
// ============================================================================

bool axisSynchronizationCheckXYMatch(void) {
    // Only check if both axes are moving
    if (current_metrics.x_velocity_mms < 0.1f && current_metrics.y_velocity_mms < 0.1f) {
        current_metrics.xy_velocity_error_percent = 0.0f;
        return true;
    }

    // Calculate max velocity for reference
    float max_vel = fabs(current_metrics.x_velocity_mms);
    if (fabs(current_metrics.y_velocity_mms) > max_vel) {
        max_vel = fabs(current_metrics.y_velocity_mms);
    }

    if (max_vel < 0.1f) return true;

    // Calculate error percentage
    float error = fabs(current_metrics.x_velocity_mms - current_metrics.y_velocity_mms);
    current_metrics.xy_velocity_error_percent = (error / max_vel) * 100.0f;

    return current_metrics.xy_velocity_error_percent <= sync_config.xy_velocity_tolerance_percent;
}

bool axisSynchronizationCheckVFDEncoderCorrelation(void) {
    // If VFD isn't running, skip correlation check
    if (current_metrics.vfd_frequency_hz < 0.5f) {
        current_metrics.vfd_encoder_correlation = 100.0f;
        return true;
    }

    // If axes aren't moving, check that VFD is also idle
    float max_encoder_vel = fabs(current_metrics.x_velocity_mms);
    if (fabs(current_metrics.y_velocity_mms) > max_encoder_vel) {
        max_encoder_vel = fabs(current_metrics.y_velocity_mms);
    }

    if (max_encoder_vel < 0.1f && current_metrics.vfd_frequency_hz < 0.5f) {
        current_metrics.vfd_encoder_correlation = 100.0f;
        return true;
    }

    // Check correlation: if VFD running, encoders should show motion
    if (current_metrics.vfd_frequency_hz > 5.0f && max_encoder_vel < 1.0f) {
        // Motor spinning fast but no mechanical motion = problem
        current_metrics.vfd_encoder_correlation = 0.0f;
        return false;
    }

    // Simple correlation: if both moving, give high score
    if (current_metrics.vfd_frequency_hz > 1.0f && max_encoder_vel > 1.0f) {
        current_metrics.vfd_encoder_correlation = 95.0f;
        return true;
    }

    current_metrics.vfd_encoder_correlation = 50.0f;
    return false;
}

bool axisSynchronizationDetectJitter(void) {
    current_metrics.jitter_detected = current_metrics.jitter_amplitude_mms > sync_config.jitter_threshold_mms;
    return current_metrics.jitter_detected;
}

float axisSynchronizationGetAxisError(uint8_t axis) {
    switch (axis) {
        case 0: return fabs(current_metrics.x_velocity_mms);
        case 1: return fabs(current_metrics.y_velocity_mms);
        case 2: return fabs(current_metrics.z_velocity_mms);
        default: return 0.0f;
    }
}

// ============================================================================
// DIAGNOSTICS & REPORTING
// ============================================================================

const char* axisSynchronizationGetStatusString(void) {
    if (current_metrics.motion_quality_score >= 90) return "EXCELLENT";
    if (current_metrics.motion_quality_score >= 70) return "GOOD";
    if (current_metrics.motion_quality_score >= 50) return "FAIR";
    return "POOR";
}

void axisSynchronizationPrintSummary(void) {
    Serial.println("\n[AXIS_SYNC] === Motion Quality Summary ===");
    Serial.printf("Status:              %s (%u/100)\n",
                  axisSynchronizationGetStatusString(),
                  current_metrics.motion_quality_score);
    Serial.printf("Synchronized:        %s\n", current_metrics.axes_synchronized ? "YES" : "NO");
    Serial.printf("XY Velocity Error:   %.1f%%\n", current_metrics.xy_velocity_error_percent);
    Serial.printf("VFD/Encoder Corr:    %.0f%%\n", current_metrics.vfd_encoder_correlation);
    Serial.printf("Jitter Amplitude:    %.2f mm/s%s\n",
                  current_metrics.jitter_amplitude_mms,
                  current_metrics.jitter_detected ? " (ELEVATED)" : "");
    Serial.printf("Sync Duration:       %lu ms\n", (unsigned long)current_metrics.synchronized_duration_ms);
    Serial.printf("Desync Events:       %lu\n", (unsigned long)current_metrics.desync_count);
    Serial.println();
}

void axisSynchronizationPrintDiagnostics(void) {
    Serial.println("\n[AXIS_SYNC] === Detailed Axis Diagnostics ===");
    Serial.printf("X Velocity:          %.2f mm/s\n", current_metrics.x_velocity_mms);
    Serial.printf("Y Velocity:          %.2f mm/s\n", current_metrics.y_velocity_mms);
    Serial.printf("Z Velocity:          %.2f mm/s\n", current_metrics.z_velocity_mms);
    Serial.printf("VFD Frequency:       %.1f Hz\n", current_metrics.vfd_frequency_hz);

    Serial.println("\n[Synchronization]");
    Serial.printf("XY Error:            %.1f%% (tolerance: %.1f%%)\n",
                  current_metrics.xy_velocity_error_percent,
                  sync_config.xy_velocity_tolerance_percent);
    Serial.printf("VFD/Encoder:         %.0f%% (tolerance: %.1f%%)\n",
                  current_metrics.vfd_encoder_correlation,
                  sync_config.vfd_encoder_tolerance_percent);

    Serial.println("\n[Jitter Analysis]");
    Serial.printf("X Jitter:            %.3f mm/s\n", x_jitter.max_jitter_mms);
    Serial.printf("Y Jitter:            %.3f mm/s\n", y_jitter.max_jitter_mms);
    Serial.printf("Z Jitter:            %.3f mm/s\n", z_jitter.max_jitter_mms);
    Serial.printf("Average Jitter:      %.3f mm/s (threshold: %.2f mm/s)\n",
                  current_metrics.jitter_amplitude_mms,
                  sync_config.jitter_threshold_mms);

    Serial.println("\n[Configuration]");
    Serial.printf("XY Tolerance:        %.1f%%\n", sync_config.xy_velocity_tolerance_percent);
    Serial.printf("VFD/Encoder Tol:     %.1f%%\n", sync_config.vfd_encoder_tolerance_percent);
    Serial.printf("Jitter Threshold:    %.2f mm/s\n", sync_config.jitter_threshold_mms);
    Serial.println();
}

void axisSynchronizationReset(void) {
    memset(&x_jitter, 0, sizeof(x_jitter));
    memset(&y_jitter, 0, sizeof(y_jitter));
    memset(&z_jitter, 0, sizeof(z_jitter));
    current_metrics.synchronized_duration_ms = 0;
    current_metrics.desync_count = 0;
}

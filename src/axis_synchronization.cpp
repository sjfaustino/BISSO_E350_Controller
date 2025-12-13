/**
 * @file axis_synchronization.cpp
 * @brief Per-Axis Motion Validation System Implementation (PHASE 5.6)
 * @project BISSO E350 Controller
 * @details Real-time validation of individual axis motion quality
 *          Multiplexed single VFD across X/Y/Z axes with contactor selection
 *          Validates VFD/encoder correlation and detects mechanical degradation
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
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================================
// MODULE STATE (Per-axis tracking with VFD multiplexing)
// ============================================================================

// BUGFIX: Race condition protection for axis metrics access
// Used by telemetry task (writer) and web server (reader)
static SemaphoreHandle_t axis_metrics_mutex = NULL;

static all_axes_metrics_t all_axes = {
    .x_axis = {
        .current_velocity_mms = 0.0f,
        .vfd_frequency_hz = 0.0f,
        .commanded_feedrate_mms = 0.0f,
        .velocity_jitter_mms = 0.0f,
        .vfd_encoder_error_percent = 0.0f,
        .is_moving = false,
        .stalled = false,
        .jitter_elevated = false,
        .quality_score = 100,
        .good_motion_samples = 0,
        .bad_motion_samples = 0,
        .stall_count = 0,
        .last_update_ms = 0,
        .active_duration_ms = 0,
        .max_jitter_recorded_mms = 0.0f
    },
    .y_axis = {0},  // Same initialization as X
    .z_axis = {0},  // Same initialization as X
    .active_axis = 255  // No axis active initially
};

// Initialize Y and Z to match X
static void initializeAllAxes(void) {
    all_axes.y_axis = all_axes.x_axis;
    all_axes.z_axis = all_axes.x_axis;
}

static axis_sync_config_t sync_config = {
    .vfd_encoder_tolerance_percent = 15.0f,      // Max VFD/encoder mismatch
    .encoder_stall_threshold_mms = 0.1f,         // Min velocity to not be stalled
    .jitter_threshold_mms = 0.5f,                // Jitter alert threshold
    .jitter_window_ms = 500,                     // 500ms rolling window
    .good_samples_for_quality = 10,              // Samples for "good" rating
    .bad_samples_for_alert = 3                   // Bad samples before alert
};

// Jitter tracking per axis (rolling window)
typedef struct {
    float velocity_history[50];     // 50 velocity samples @ 100Hz = 500ms window
    uint32_t history_index;
    float current_jitter_mms;
} jitter_tracker_t;

static jitter_tracker_t x_jitter = {0};
static jitter_tracker_t y_jitter = {0};
static jitter_tracker_t z_jitter = {0};

// ============================================================================
// INITIALIZATION & PERSISTENCE
// ============================================================================

void axisSynchronizationInit(void) {
    Serial.println("[AXIS_SYNC] Initializing per-axis motion validation");
    initializeAllAxes();
    memset(&x_jitter, 0, sizeof(x_jitter));
    memset(&y_jitter, 0, sizeof(y_jitter));
    memset(&z_jitter, 0, sizeof(z_jitter));

    // BUGFIX: Create mutex for thread-safe access
    if (axis_metrics_mutex == NULL) {
        axis_metrics_mutex = xSemaphoreCreateMutex();
        if (axis_metrics_mutex == NULL) {
            Serial.println("[AXIS_SYNC] [ERR] Failed to create metrics mutex");
        }
    }

    axisSynchronizationLoadConfig();
}

bool axisSynchronizationLoadConfig(void) {
    int32_t vfd_tol = configGetInt("axis_vfd_tol", 1500);  // 15.0% * 100
    int32_t stall_thr = configGetInt("axis_stall_thr", 10);  // 0.1 mm/s * 100
    int32_t jitter_thr = configGetInt("axis_jitter_thr", 50);  // 0.5 mm/s * 100

    sync_config.vfd_encoder_tolerance_percent = vfd_tol / 100.0f;
    sync_config.encoder_stall_threshold_mms = stall_thr / 100.0f;
    sync_config.jitter_threshold_mms = jitter_thr / 100.0f;

    Serial.printf("[AXIS_SYNC] Loaded config: VFD_tol=%.1f%%, Stall_thr=%.2f mm/s, Jitter_thr=%.2f mm/s\n",
                  sync_config.vfd_encoder_tolerance_percent,
                  sync_config.encoder_stall_threshold_mms,
                  sync_config.jitter_threshold_mms);

    return true;
}

void axisSynchronizationSaveConfig(void) {
    configSetInt("axis_vfd_tol", (int32_t)(sync_config.vfd_encoder_tolerance_percent * 100.0f));
    configSetInt("axis_stall_thr", (int32_t)(sync_config.encoder_stall_threshold_mms * 100.0f));
    configSetInt("axis_jitter_thr", (int32_t)(sync_config.jitter_threshold_mms * 100.0f));
    configUnifiedFlush();
    configUnifiedSave();

    Serial.println("[AXIS_SYNC] Configuration saved");
}

void axisSynchronizationResetDefaults(void) {
    sync_config.vfd_encoder_tolerance_percent = 15.0f;
    sync_config.encoder_stall_threshold_mms = 0.1f;
    sync_config.jitter_threshold_mms = 0.5f;
    sync_config.jitter_window_ms = 500;
    sync_config.good_samples_for_quality = 10;
    sync_config.bad_samples_for_alert = 3;

    axisSynchronizationSaveConfig();
}

// ============================================================================
// CONFIGURATION ACCESSORS
// ============================================================================

const axis_sync_config_t* axisSynchronizationGetConfig(void) {
    return &sync_config;
}

void axisSynchronizationSetVFDEncoderTolerance(float tolerance_percent) {
    if (tolerance_percent >= 0.0f && tolerance_percent <= 50.0f) {
        sync_config.vfd_encoder_tolerance_percent = tolerance_percent;
        axisSynchronizationSaveConfig();
    }
}

void axisSynchronizationSetStallThreshold(float threshold_mms) {
    if (threshold_mms >= 0.0f && threshold_mms <= 2.0f) {
        sync_config.encoder_stall_threshold_mms = threshold_mms;
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

    tracker->current_jitter_mms = fabs(max_vel - min_vel);
}

// ============================================================================
// REAL-TIME VALIDATION (Per-axis model with VFD multiplexing)
// ============================================================================

void axisSynchronizationUpdate(uint8_t active_axis,
                               float x_velocity_mms, float y_velocity_mms, float z_velocity_mms,
                               float vfd_frequency_hz, float commanded_feedrate_mms) {
    uint32_t now_ms = millis();

    // BUGFIX: Acquire mutex before updating shared state
    if (axis_metrics_mutex != NULL) {
        if (xSemaphoreTake(axis_metrics_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            // Mutex timeout - skip this update to avoid deadlock
            return;
        }
    }

    // Update active axis tracker
    if (active_axis != all_axes.active_axis) {
        // Axis changed - reset duration counter
        all_axes.active_axis = active_axis;
    }

    // Update all axis velocities
    all_axes.x_axis.current_velocity_mms = fabsf(x_velocity_mms);
    all_axes.y_axis.current_velocity_mms = fabsf(y_velocity_mms);
    all_axes.z_axis.current_velocity_mms = fabsf(z_velocity_mms);

    // Track jitter for all axes
    updateJitterTracker(&x_jitter, all_axes.x_axis.current_velocity_mms);
    updateJitterTracker(&y_jitter, all_axes.y_axis.current_velocity_mms);
    updateJitterTracker(&z_jitter, all_axes.z_axis.current_velocity_mms);

    all_axes.x_axis.velocity_jitter_mms = x_jitter.current_jitter_mms;
    all_axes.y_axis.velocity_jitter_mms = y_jitter.current_jitter_mms;
    all_axes.z_axis.velocity_jitter_mms = z_jitter.current_jitter_mms;

    // Only validate active axis against VFD metrics
    if (active_axis < 3) {
        axis_metrics_t* active = (active_axis == 0) ? &all_axes.x_axis :
                               (active_axis == 1) ? &all_axes.y_axis :
                               &all_axes.z_axis;

        // BUGFIX: Save old timestamp BEFORE updating to new one
        uint32_t old_update_ms = active->last_update_ms;
        active->vfd_frequency_hz = vfd_frequency_hz;
        active->commanded_feedrate_mms = commanded_feedrate_mms;
        active->last_update_ms = now_ms;

        // Determine if axis is moving
        active->is_moving = (active->current_velocity_mms > 0.1f);

        if (active->is_moving) {
            // Calculate duration using old timestamp (will be non-zero now)
            uint32_t delta_ms = (now_ms - old_update_ms);
            if (delta_ms > 0 && delta_ms < 10000) {  // Sanity check: ignore if > 10s (overflow)
                active->active_duration_ms += delta_ms;
            }

            // Check for stall: commanded but not moving
            if (commanded_feedrate_mms > 0.1f && active->current_velocity_mms < sync_config.encoder_stall_threshold_mms) {
                active->stalled = true;
                active->stall_count++;
                active->bad_motion_samples++;
                active->good_motion_samples = 0;
            } else {
                active->stalled = false;

                // Check VFD/encoder correlation
                float error = axisSynchronizationGetVFDEncoderErrorForAxis(active_axis);
                active->vfd_encoder_error_percent = error;

                if (error <= sync_config.vfd_encoder_tolerance_percent) {
                    active->good_motion_samples++;
                    active->bad_motion_samples = 0;
                } else {
                    active->bad_motion_samples++;
                    active->good_motion_samples = 0;
                }
            }

            // Check jitter
            active->jitter_elevated = (active->velocity_jitter_mms > sync_config.jitter_threshold_mms);
            if (active->velocity_jitter_mms > active->max_jitter_recorded_mms) {
                active->max_jitter_recorded_mms = active->velocity_jitter_mms;
            }
        } else {
            active->active_duration_ms = 0;
            active->good_motion_samples = 0;
            active->bad_motion_samples = 0;
            active->stalled = false;
        }

        // Calculate quality score
        uint32_t score = 100;
        if (active->stalled) score -= 40;
        if (active->vfd_encoder_error_percent > sync_config.vfd_encoder_tolerance_percent) score -= 25;
        if (active->jitter_elevated) score -= 15;
        if (active->bad_motion_samples >= sync_config.bad_samples_for_alert) score -= 10;

        active->quality_score = (score > 0) ? score : 0;
    }

    // BUGFIX: Release mutex when done
    if (axis_metrics_mutex != NULL) {
        xSemaphoreGive(axis_metrics_mutex);
    }
}

bool axisSynchronizationIsValid(void) {
    if (all_axes.active_axis >= 3) return true;  // No axis active, skip validation

    axis_metrics_t* active = (all_axes.active_axis == 0) ? &all_axes.x_axis :
                           (all_axes.active_axis == 1) ? &all_axes.y_axis :
                           &all_axes.z_axis;

    return !active->stalled && active->quality_score >= 70;
}

uint32_t axisSynchronizationGetQualityScore(uint8_t axis) {
    if (axis >= 3) return 0;
    const axis_metrics_t* metrics = (axis == 0) ? &all_axes.x_axis :
                                   (axis == 1) ? &all_axes.y_axis :
                                   &all_axes.z_axis;
    return metrics->quality_score;
}

const all_axes_metrics_t* axisSynchronizationGetAllMetrics(void) {
    // BUGFIX: Note - caller must be aware this returns pointer to shared state
    // Caller should hold mutex if concurrent access is happening
    return &all_axes;
}

const axis_metrics_t* axisSynchronizationGetAxisMetrics(uint8_t axis) {
    if (axis >= 3) return NULL;
    // BUGFIX: Note - returns pointer to shared state
    // Caller should hold mutex if concurrent access is happening
    return (axis == 0) ? &all_axes.x_axis :
           (axis == 1) ? &all_axes.y_axis :
           &all_axes.z_axis;
}

// BUGFIX: Provide safe mutex lock/unlock functions for callers
void axisSynchronizationLock(void) {
    if (axis_metrics_mutex != NULL) {
        xSemaphoreTake(axis_metrics_mutex, portMAX_DELAY);
    }
}

void axisSynchronizationUnlock(void) {
    if (axis_metrics_mutex != NULL) {
        xSemaphoreGive(axis_metrics_mutex);
    }
}

// ============================================================================
// SPECIFIC VALIDATIONS (Per-axis)
// ============================================================================

static float axisSynchronizationGetVFDEncoderErrorForAxis(uint8_t axis) {
    const axis_metrics_t* metrics = axisSynchronizationGetAxisMetrics(axis);
    if (!metrics) return 100.0f;

    // ========================================================================
    // VFD/ENCODER CORRELATION CALCULATION (PHASE 5.6 BUGFIX)
    // ========================================================================
    // Calculate error as percentage mismatch between VFD and encoder feedback
    // VFD frequency (Hz) should correlate with encoder velocity (mm/s)
    // Rough estimate: 1 Hz ≈ 10-20 mm/s depending on pulley/gear ratio
    // For now, use 1 Hz ≈ 15 mm/s as baseline (user can calibrate)
    // ========================================================================

    // Case 1: VFD not running
    if (metrics->vfd_frequency_hz < 0.5f) {
        // VFD idle - encoder should also be idle
        if (metrics->current_velocity_mms < 0.1f) {
            return 0.0f;  // Both idle - perfect correlation
        }
        // VFD idle but encoder moving - major problem (slipping belt/coupling)
        return 100.0f;
    }

    // Case 2: VFD running but encoder not moving much
    if (metrics->vfd_frequency_hz > 5.0f && metrics->current_velocity_mms < 1.0f) {
        // Motor spinning fast but no mechanical motion - drive failure
        // (slip, broken coupling, jam, etc.)
        return 100.0f;
    }

    // Case 3: Both idle
    if (metrics->current_velocity_mms < 0.1f && metrics->vfd_frequency_hz < 0.5f) {
        return 0.0f;
    }

    // Case 4: Both moving - calculate actual correlation error
    // Expected velocity from VFD frequency (baseline: 1 Hz = 15 mm/s)
    // This is a rough estimate - user can calibrate by observing ratio
    #define VFD_FREQUENCY_TO_VELOCITY_RATIO 15.0f  // mm/s per Hz

    float expected_velocity_mms = metrics->vfd_frequency_hz * VFD_FREQUENCY_TO_VELOCITY_RATIO;
    float actual_velocity_mms = metrics->current_velocity_mms;

    // Percentage error: |expected - actual| / expected * 100
    float velocity_error = fabsf(expected_velocity_mms - actual_velocity_mms) / expected_velocity_mms * 100.0f;

    // Cap at reasonable limits (0-100%)
    if (velocity_error > 100.0f) velocity_error = 100.0f;

    return velocity_error;
}

bool axisSynchronizationCheckVFDEncoderCorrelation(void) {
    if (all_axes.active_axis >= 3) return true;
    return axisSynchronizationGetVFDEncoderErrorForAxis(all_axes.active_axis)
           <= sync_config.vfd_encoder_tolerance_percent;
}

bool axisSynchronizationDetectJitter(void) {
    if (all_axes.active_axis >= 3) return false;
    const axis_metrics_t* metrics = axisSynchronizationGetAxisMetrics(all_axes.active_axis);
    return metrics ? (metrics->velocity_jitter_mms > sync_config.jitter_threshold_mms) : false;
}

bool axisSynchronizationDetectStall(void) {
    if (all_axes.active_axis >= 3) return false;
    const axis_metrics_t* metrics = axisSynchronizationGetAxisMetrics(all_axes.active_axis);
    return metrics ? metrics->stalled : false;
}

float axisSynchronizationGetVFDEncoderError(void) {
    if (all_axes.active_axis >= 3) return 0.0f;
    return axisSynchronizationGetVFDEncoderErrorForAxis(all_axes.active_axis);
}

// ============================================================================
// DIAGNOSTICS & REPORTING (Per-axis)
// ============================================================================

const char* axisSynchronizationGetStatusString(uint8_t axis) {
    uint32_t score = axisSynchronizationGetQualityScore(axis);
    if (score >= 90) return "EXCELLENT";
    if (score >= 70) return "GOOD";
    if (score >= 50) return "FAIR";
    return "POOR";
}

void axisSynchronizationPrintSummary(void) {
    Serial.println("\n[AXIS_SYNC] === Per-Axis Motion Quality Summary ===");
    Serial.println("Active Axis: X                    Y                    Z");

    Serial.print("Status:      ");
    Serial.printf("%-20s %-20s %-20s\n",
                  axisSynchronizationGetStatusString(0),
                  axisSynchronizationGetStatusString(1),
                  axisSynchronizationGetStatusString(2));

    Serial.print("Quality:     ");
    Serial.printf("%-20u %-20u %-20u\n",
                  all_axes.x_axis.quality_score,
                  all_axes.y_axis.quality_score,
                  all_axes.z_axis.quality_score);

    Serial.print("Velocity:    ");
    Serial.printf("%-20.2f %-20.2f %-20.2f mm/s\n",
                  all_axes.x_axis.current_velocity_mms,
                  all_axes.y_axis.current_velocity_mms,
                  all_axes.z_axis.current_velocity_mms);

    Serial.print("Jitter:      ");
    Serial.printf("%-20.3f %-20.3f %-20.3f mm/s\n",
                  all_axes.x_axis.velocity_jitter_mms,
                  all_axes.y_axis.velocity_jitter_mms,
                  all_axes.z_axis.velocity_jitter_mms);

    Serial.print("Stalled:     ");
    Serial.printf("%-20s %-20s %-20s\n",
                  all_axes.x_axis.stalled ? "YES" : "NO",
                  all_axes.y_axis.stalled ? "YES" : "NO",
                  all_axes.z_axis.stalled ? "YES" : "NO");

    Serial.println();
}

void axisSynchronizationPrintAxisDiagnostics(uint8_t axis) {
    if (axis >= 3) {
        Serial.println("[AXIS_SYNC] Invalid axis (0=X, 1=Y, 2=Z)");
        return;
    }

    const char* axis_name = (axis == 0) ? "X" : (axis == 1) ? "Y" : "Z";
    const axis_metrics_t* metrics = axisSynchronizationGetAxisMetrics(axis);

    Serial.printf("\n[AXIS_SYNC] === Axis %s Diagnostics ===\n", axis_name);
    Serial.printf("Status:                  %s (%u/100)\n",
                  axisSynchronizationGetStatusString(axis),
                  metrics->quality_score);
    Serial.printf("Current Velocity:        %.2f mm/s\n", metrics->current_velocity_mms);
    Serial.printf("Commanded Feedrate:      %.2f mm/s\n", metrics->commanded_feedrate_mms);
    Serial.printf("Is Moving:               %s\n", metrics->is_moving ? "YES" : "NO");
    Serial.printf("Stalled:                 %s (count: %lu)\n",
                  metrics->stalled ? "YES" : "NO", (unsigned long)metrics->stall_count);

    Serial.println("\n[VFD/Encoder Correlation]");
    Serial.printf("VFD Frequency:           %.1f Hz\n", metrics->vfd_frequency_hz);
    Serial.printf("Error:                   %.1f%% (tolerance: %.1f%%)\n",
                  metrics->vfd_encoder_error_percent,
                  sync_config.vfd_encoder_tolerance_percent);

    Serial.println("\n[Jitter & Wear]");
    Serial.printf("Current Jitter:          %.3f mm/s\n", metrics->velocity_jitter_mms);
    Serial.printf("Max Jitter Recorded:     %.3f mm/s (wear trend indicator)\n",
                  metrics->max_jitter_recorded_mms);
    Serial.printf("Jitter Elevated:         %s (threshold: %.2f mm/s)\n",
                  metrics->jitter_elevated ? "YES" : "NO",
                  sync_config.jitter_threshold_mms);

    Serial.println("\n[Sampling History]");
    Serial.printf("Good Samples:            %lu\n", (unsigned long)metrics->good_motion_samples);
    Serial.printf("Bad Samples:             %lu\n", (unsigned long)metrics->bad_motion_samples);
    Serial.printf("Active Duration:         %lu ms\n", (unsigned long)metrics->active_duration_ms);

    Serial.println();
}

void axisSynchronizationResetAxis(uint8_t axis) {
    if (axis >= 3) return;
    axis_metrics_t* metrics = (axis == 0) ? &all_axes.x_axis :
                             (axis == 1) ? &all_axes.y_axis :
                             &all_axes.z_axis;
    metrics->good_motion_samples = 0;
    metrics->bad_motion_samples = 0;
    metrics->active_duration_ms = 0;
    metrics->stall_count = 0;
}

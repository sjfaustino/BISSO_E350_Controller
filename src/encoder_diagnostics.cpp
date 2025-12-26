/**
 * @file encoder_diagnostics.cpp
 * @brief Encoder Diagnostics Implementation (PHASE 5.3)
 */

#include "encoder_diagnostics.h"
#include "encoder_wj66.h"
#include "motion.h"
#include "motion_state.h"
#include "serial_logger.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// Diagnostic data for each axis
static encoder_diagnostic_t diagnostics[4];
static uint32_t last_position[4] = {0};
static uint32_t last_update_time = 0;

// Drift tracking
typedef struct {
    float accumulated_drift_mm;
    uint32_t measurement_count;
    uint32_t last_measurement_ms;
} drift_tracker_t;

static drift_tracker_t drift_trackers[4] = {
    {.accumulated_drift_mm = 0.0f, .measurement_count = 0, .last_measurement_ms = 0},
    {.accumulated_drift_mm = 0.0f, .measurement_count = 0, .last_measurement_ms = 0},
    {.accumulated_drift_mm = 0.0f, .measurement_count = 0, .last_measurement_ms = 0},
    {.accumulated_drift_mm = 0.0f, .measurement_count = 0, .last_measurement_ms = 0}
};

const char* encoderDiagnosticsGetHealthString(encoder_health_t health) {
    switch (health) {
        case ENCODER_HEALTH_OPTIMAL: return "OPTIMAL";
        case ENCODER_HEALTH_NORMAL: return "NORMAL";
        case ENCODER_HEALTH_DEGRADED: return "DEGRADED";
        case ENCODER_HEALTH_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

static encoder_health_t calculateHealth(uint8_t axis_id, const encoder_diagnostic_t* diag) {
    // Health based on multiple factors
    if (diag->signal_quality < 50 && diag->signal_errors > 10) {
        return ENCODER_HEALTH_CRITICAL;
    }
    if (fabsf(diag->position_variance_mm) > 5.0f || diag->drift_per_hour_mm > 2.0f) {
        return ENCODER_HEALTH_DEGRADED;
    }
    if (diag->signal_quality < 70 || diag->error_rate > 0.05f) {
        return ENCODER_HEALTH_NORMAL;
    }
    return ENCODER_HEALTH_OPTIMAL;
}

void encoderDiagnosticsInit() {
    memset(diagnostics, 0, sizeof(diagnostics));
    memset(last_position, 0, sizeof(last_position));
    memset(drift_trackers, 0, sizeof(drift_trackers));

    for (int i = 0; i < 4; i++) {
        diagnostics[i].axis_id = i;
        diagnostics[i].signal_quality = 100;  // Optimistic initial value
        diagnostics[i].read_count = 0;
        diagnostics[i].error_count = 0;
        last_position[i] = (uint32_t)(motionGetPositionMM(i) * 100);  // Store as 0.01mm units
        drift_trackers[i].measurement_count = 0;
        drift_trackers[i].accumulated_drift_mm = 0.0f;
    }

    last_update_time = millis();
    logInfo("[ENCODER_DIAG] Initialized");
}

void encoderDiagnosticsUpdate() {
    uint32_t now = millis();
    uint32_t elapsed_ms = (uint32_t)(now - last_update_time);

    if (elapsed_ms < 1000) return;  // Update every 1 second minimum

    for (int axis = 0; axis < 4; axis++) {
        encoder_diagnostic_t* diag = &diagnostics[axis];

        // Get current position
        float current_pos = motionGetPositionMM(axis);
        uint32_t current_pos_units = (uint32_t)(current_pos * 100);

        // Encoder-based position (if available)
        uint32_t encoder_pos_units = (uint32_t)(wj66GetPosition(axis) * 100);

        // Calculate variance (difference between encoder position and expected motion)
        int32_t variance_units = (int32_t)(encoder_pos_units - current_pos_units);
        float variance_mm = variance_units / 100.0f;

        diag->position_mm = current_pos;
        diag->position_variance_mm = variance_mm;

        if (fabsf(variance_mm) > fabsf(diag->max_variance_mm)) {
            diag->max_variance_mm = variance_mm;
        }

        // Track drift
        if (drift_trackers[axis].measurement_count == 0) {
            drift_trackers[axis].last_measurement_ms = now;
        }

        drift_trackers[axis].accumulated_drift_mm += fabsf(variance_mm);
        drift_trackers[axis].measurement_count++;

        if (drift_trackers[axis].measurement_count > 10) {
            uint32_t elapsed_since_first = (uint32_t)(now - drift_trackers[axis].last_measurement_ms);
            if (elapsed_since_first > 3600000) {  // Every hour
                float hours = elapsed_since_first / 3600000.0f;
                diag->drift_per_hour_mm = drift_trackers[axis].accumulated_drift_mm / hours;
                drift_trackers[axis].accumulated_drift_mm = 0.0f;
                drift_trackers[axis].measurement_count = 0;
                drift_trackers[axis].last_measurement_ms = now;
            }
        }

        // Update error tracking
        encoder_status_t status = wj66GetStatus();
        if (status != ENCODER_OK) {
            diag->signal_errors++;
            diag->last_error_ms = now;
            diag->error_count++;
        }

        diag->read_count++;
        diag->error_rate = (float)diag->error_count / diag->read_count;

        // Signal quality (inverse of error rate, 0-100)
        diag->signal_quality = (uint8_t)((1.0f - diag->error_rate) * 100.0f);
        if (diag->signal_quality > 100) diag->signal_quality = 100;

        // Determine if recalibration needed
        diag->needs_recalibration = (fabsf(diag->position_variance_mm) > 2.0f) ||
                                    (diag->drift_per_hour_mm > 1.0f) ||
                                    (diag->signal_quality < 90);

        // Calculate health status
        diag->health = calculateHealth(axis, diag);

        diag->last_update_ms = now;
        last_position[axis] = current_pos_units;
    }

    last_update_time = now;
}

const encoder_diagnostic_t* encoderDiagnosticsGetAxis(uint8_t axis_id) {
    if (axis_id >= 4) return NULL;
    return &diagnostics[axis_id];
}

encoder_health_t encoderDiagnosticsGetHealth(uint8_t axis_id) {
    if (axis_id >= 4) return ENCODER_HEALTH_CRITICAL;
    return diagnostics[axis_id].health;
}

bool encoderDiagnosticsVerifyCalibration(uint8_t axis_id, float distance_mm) {
    if (axis_id >= 4 || motionIsMoving()) return false;

    logInfo("[ENCODER_DIAG] Verifying calibration on axis %d", axis_id);

    // Record starting position
    float start_pos = motionGetPositionMM(axis_id);

    // Move the axis
    motionMoveRelative(
        axis_id == 0 ? distance_mm : 0,
        axis_id == 1 ? distance_mm : 0,
        axis_id == 2 ? distance_mm : 0,
        axis_id == 3 ? distance_mm : 0,
        50.0f  // 50 mm/min speed
    );

    // Wait for motion to complete
    uint32_t timeout = millis() + 30000;  // 30 second timeout
    while (motionIsMoving() && millis() < timeout) {
        delay(100);
    }

    // Check end position
    float end_pos = motionGetPositionMM(axis_id);
    float actual_distance = end_pos - start_pos;

    // Allow 5% tolerance
    float error_percent = fabsf(actual_distance - distance_mm) / distance_mm * 100.0f;
    bool passed = (error_percent < 5.0f);

    logInfo("[ENCODER_DIAG] Axis %d: Moved %.2f mm (expected %.2f mm, error %.1f%%)",
           axis_id, actual_distance, distance_mm, error_percent);

    diagnostics[axis_id].needs_recalibration = !passed;

    return passed;
}

uint8_t encoderDiagnosticsAnalyzeSignal(uint8_t axis_id, uint32_t duration_ms) {
    if (axis_id >= 4) return 0;

    logInfo("[ENCODER_DIAG] Analyzing signal on axis %d for %lu ms", axis_id, (unsigned long)duration_ms);

    uint32_t start_time = millis();
    uint32_t error_count = 0;
    uint32_t read_count = 0;

    while (millis() - start_time < duration_ms) {
        encoder_status_t status = wj66GetStatus();
        if (status != ENCODER_OK) {
            error_count++;
        }
        read_count++;
        delay(100);
    }

    uint8_t quality = (uint8_t)((1.0f - ((float)error_count / read_count)) * 100.0f);
    if (quality > 100) quality = 100;

    logInfo("[ENCODER_DIAG] Signal quality: %u%% (%lu errors / %lu reads)",
           quality, (unsigned long)error_count, (unsigned long)read_count);

    return quality;
}

size_t encoderDiagnosticsExportJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 512) return 0;

    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "{\"encoders\":[");
    // PHASE 5.10: Check for buffer overflow after each snprintf
    if (offset >= buffer_size) return buffer_size - 1;

    for (int i = 0; i < 4; i++) {
        const encoder_diagnostic_t* diag = &diagnostics[i];

        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, ",");
            if (offset >= buffer_size) return buffer_size - 1;
        }

        offset += snprintf(buffer + offset, buffer_size - offset,
            "{\"axis\":%d,\"health\":\"%s\",\"position_mm\":%.2f,"
            "\"variance_mm\":%.2f,\"drift_per_hour\":%.3f,"
            "\"signal_quality\":%u,\"error_rate\":%.2f,"
            "\"needs_recal\":%s}",
            diag->axis_id,
            encoderDiagnosticsGetHealthString(diag->health),
            diag->position_mm,
            diag->position_variance_mm,
            diag->drift_per_hour_mm,
            diag->signal_quality,
            diag->error_rate * 100.0f,
            diag->needs_recalibration ? "true" : "false");
        if (offset >= buffer_size) return buffer_size - 1;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "]}");
    if (offset >= buffer_size) return buffer_size - 1;

    return offset;
}

void encoderDiagnosticsPrint() {
    serialLoggerLock();
    Serial.println("\n[ENCODER_DIAG] === Encoder Diagnostics ===");
    Serial.println("Axis | Health    | Position  | Variance | Drift/h  | Quality | Errors");
    Serial.println("-----|-----------|-----------|----------|----------|---------|--------");

    for (int i = 0; i < 4; i++) {
        const encoder_diagnostic_t* diag = &diagnostics[i];

        Serial.printf("%d    | %-9s | %9.2f | %8.2f | %8.3f | %7u | %lu\n",
            diag->axis_id,
            encoderDiagnosticsGetHealthString(diag->health),
            diag->position_mm,
            diag->position_variance_mm,
            diag->drift_per_hour_mm,
            diag->signal_quality,
            (unsigned long)diag->signal_errors);

        if (diag->needs_recalibration) {
            Serial.printf("     [!] Recalibration recommended\n");
        }
    }

    Serial.println();
    serialLoggerUnlock();
}

void encoderDiagnosticsReset(uint8_t axis_id) {
    if (axis_id == 0xFF) {
        // Reset all
        for (int i = 0; i < 4; i++) {
            diagnostics[i].signal_errors = 0;
            diagnostics[i].error_count = 0;
            diagnostics[i].read_count = 0;
            diagnostics[i].error_rate = 0.0f;
            diagnostics[i].max_variance_mm = 0.0f;
            drift_trackers[i].accumulated_drift_mm = 0.0f;
            drift_trackers[i].measurement_count = 0;
        }
        logInfo("[ENCODER_DIAG] All diagnostics reset");
    } else if (axis_id < 4) {
        diagnostics[axis_id].signal_errors = 0;
        diagnostics[axis_id].error_count = 0;
        diagnostics[axis_id].read_count = 0;
        diagnostics[axis_id].error_rate = 0.0f;
        drift_trackers[axis_id].accumulated_drift_mm = 0.0f;
        drift_trackers[axis_id].measurement_count = 0;
        logInfo("[ENCODER_DIAG] Axis %d diagnostics reset", axis_id);
    }
}

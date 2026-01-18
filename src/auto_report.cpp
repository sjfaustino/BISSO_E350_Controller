/**
 * @file auto_report.cpp
 * @brief Automatic Position Reporting System Implementation
 */

#include "auto_report.h"
#include "motion.h"
#include "calibration.h"
#include "serial_logger.h"
#include <Arduino.h>

// ============================================================================
// AUTO-REPORT STATE
// ============================================================================

static struct {
    bool enabled;                       // Is auto-report active?
    uint32_t interval_ms;               // Reporting interval in milliseconds
    uint32_t last_report_ms;            // Timestamp of last report
} autoReportState = {
    false,      // Initially disabled
    0,          // No interval
    0           // Last report time
};

// ============================================================================
// INITIALIZATION
// ============================================================================

void autoReportInit() {
    autoReportState.enabled = false;
    autoReportState.interval_ms = 0;
    autoReportState.last_report_ms = 0;
    logInfo("[AUTO-REPORT] Initialized");
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool autoReportSetInterval(uint32_t interval_sec) {
    // Convert seconds to milliseconds
    if (interval_sec == 0) {
        // Disable auto-report
        autoReportState.enabled = false;
        autoReportState.interval_ms = 0;
        logInfo("[AUTO-REPORT] Disabled (M154 S0)");
        return true;
    }

    // Enable with new interval
    autoReportState.enabled = true;
    autoReportState.interval_ms = interval_sec * 1000;
    autoReportState.last_report_ms = millis();  // Start from now

    logInfo("[AUTO-REPORT] Enabled - Interval: %lu seconds (%lu ms)",
            (unsigned long)interval_sec, (unsigned long)autoReportState.interval_ms);

    // Report position immediately on enable
    autoReportUpdate();
    return true;
}

uint32_t autoReportGetInterval() {
    if (autoReportState.interval_ms == 0) return 0;
    return autoReportState.interval_ms / 1000;
}

bool autoReportIsEnabled() {
    return autoReportState.enabled && autoReportState.interval_ms > 0;
}

void autoReportUpdate() {
    if (!autoReportIsEnabled()) return;

    uint32_t now = millis();
    uint32_t elapsed = now - autoReportState.last_report_ms;

    // Check if interval has elapsed
    if (elapsed >= autoReportState.interval_ms) {
        autoReportState.last_report_ms = now;

        // Get current position for all axes
        float x_mm = motionGetPositionMM(0);
        float y_mm = motionGetPositionMM(1);
        float z_mm = motionGetPositionMM(2);

        // Convert A axis raw counts to degrees
        int32_t a_counts = motionGetPosition(3);
        float a_deg = 0.0f;

        extern MachineCalibration machineCal;
        if (machineCal.A.pulses_per_degree > 0) {
            a_deg = a_counts / machineCal.A.pulses_per_degree;
        } else {
            a_deg = a_counts / 1000.0f;  // MOTION_POSITION_SCALE_FACTOR_DEG
        }

        // Report in standard Grbl format
        char response[80];
        snprintf(response, sizeof(response),
                 "[POS:X:%.1f Y:%.1f Z:%.1f A:%.1f]",
                 x_mm, y_mm, z_mm, a_deg);

        logPrintln(response);
        logInfo("[AUTO-REPORT] %s", response);
    }
}

void autoReportDisable() {
    if (autoReportState.enabled) {
        autoReportState.enabled = false;
        autoReportState.interval_ms = 0;
        logWarning("[AUTO-REPORT] Disabled (Emergency Stop)");
    }
}

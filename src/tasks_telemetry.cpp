/**
 * @file tasks_telemetry.cpp
 * @brief Telemetry & Web UI Update Task (PHASE 5.4)
 * @details Background telemetry collection on Core 0 to reduce Core 1 load
 * Collects system metrics, encoder diagnostics, and broadcasts to WebSocket
 * clients
 * @author Sergio Faustino
 */

#include "altivar31_modbus.h"     // PHASE 5.5: VFD current monitoring
#include "axis_synchronization.h" // PHASE 5.6: Axis validation
#include "cutting_analytics.h"    // Stone cutting analytics
#include "dashboard_metrics.h"    // PHASE 5.3: Web UI dashboard metrics
#include "encoder_diagnostics.h"  // PHASE 5.3: Encoder health monitoring
#include "load_manager.h"         // PHASE 5.3: Graceful degradation under load
#include "motion.h"
#include "motion_state.h"
#include "safety.h"
#include "serial_logger.h"
#include "vfd_current_calibration.h" // PHASE 5.5: Current calibration
#include "watchdog_manager.h"
#include "web_server.h"
#include "task_manager.h"
#include "system_telemetry.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h> // For isnan() sensor validation

// External reference to the global WebServer instance
extern WebServerManager webServer;

// Static state for telemetry processing

void taskTelemetryFunction(void *parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[TELEMETRY_TASK] [OK] Started on core 0 - Background collection");
  watchdogTaskAdd("Telemetry");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Telemetry");
  
  // Initialize cutting analytics
  cuttingAnalyticsInit();

  while (1) {
    // 1. Update System Telemetry (PHASE 5.1)
    // Collect comprehensive system metrics for API and diagnostics
    telemetryUpdate();

    // 2. Update Phase 5.3 Modules
    // Advanced encoder diagnostics, load management, and dashboard metrics
    encoderDiagnosticsUpdate();
    loadManagerUpdate();
    dashboardMetricsUpdate();
    
    // 2.5. Update Stone Cutting Analytics
    cuttingAnalyticsUpdate();

    // 3. Telemetry Processing (Registry Integrated)
    // VFD polling is now handled by the RS-485 registry.
    // We just process the latest data here.
    float current_amps = altivar31GetCurrentAmps();
    if (!isnan(current_amps) && current_amps > 0.0f && current_amps <= 100.0f) {
        vfdCalibrationSampleCurrent(current_amps);
    }

    // Push VFD telemetry to web UI (PHASE 5.5k)
    // CRITICAL: Sanitize sensor data before sending to web UI
    // Replace NaN/invalid values with safe defaults (0.0) to prevent UI
    // corruption
    float vfd_current = altivar31GetCurrentAmps();
    float vfd_frequency = altivar31GetFrequencyHz();
    int16_t vfd_thermal = altivar31GetThermalState();

    webServer.setVFDCurrent(
        isnan(vfd_current) || vfd_current < 0.0f ? 0.0f : vfd_current);
    webServer.setVFDFrequency(
        isnan(vfd_frequency) || vfd_frequency < 0.0f ? 0.0f : vfd_frequency);
    webServer.setVFDThermalState(
        (vfd_thermal < 0 || vfd_thermal > 200) ? 0 : vfd_thermal);
    webServer.setVFDFaultCode(altivar31GetFaultCode());
    webServer.setVFDCalibrationThreshold(vfdCalibrationGetThreshold());
    webServer.setVFDCalibrationValid(vfdCalibrationIsValid());

    // 4. PHASE 5.6: Axis Synchronization Validation (Per-axis multiplexed
    // VFD) Update axis metrics for active axis only BUGFIX: Use motion
    // controller's active axis instead of velocity heuristic
    // motionGetActiveAxis() returns 0-2 for active axis, 255 if none
    uint8_t active_axis = motionGetActiveAxis();

    // Get current axis positions (velocity calculation would require motion
    // state) For now, use zero velocity as motion timing is handled by motion
    // controller
    float x_vel = 0.0f;
    float y_vel = 0.0f;
    float z_vel = 0.0f;
    float feedrate =
        motionGetFeedOverride(); // Use feed override as proxy for feedrate

    // CRITICAL: Sanitize VFD frequency before passing to axis synchronization
    // Use already-validated vfd_frequency from above (lines 83, 87)
    // If invalid, use 0.0 to prevent axis synchronization corruption
    float vfd_freq_safe =
        (isnan(vfd_frequency) || vfd_frequency < 0.0f) ? 0.0f : vfd_frequency;

    // Update per-axis synchronization metrics
    axisSynchronizationUpdate(active_axis, x_vel, y_vel, z_vel, vfd_freq_safe,
                              feedrate);

    // Push axis metrics to web server (BUGFIX: with mutex protection)
    axisSynchronizationLock();
    const all_axes_metrics_t *all_metrics = axisSynchronizationGetAllMetrics();
    if (all_metrics) {
      for (int axis = 0; axis < 3; axis++) {
        const axis_metrics_t *metrics = axisSynchronizationGetAxisMetrics(axis);
        if (metrics) {
          webServer.setAxisQualityScore(axis, metrics->quality_score);
          webServer.setAxisJitterAmplitude(axis, metrics->velocity_jitter_mms);
          webServer.setAxisStalled(axis, metrics->stalled);
          webServer.setAxisVFDError(axis, metrics->vfd_encoder_error_percent);
        }
      }
    }
    axisSynchronizationUnlock();

    // 5. Web Telemetry Broadcast
    // Push real-time state to the Web UI via WebSockets.

    // Use the Motion State Accessors to get physical units (MM)
    webServer.setAxisPosition('X', motionGetPositionMM(0));
    webServer.setAxisPosition('Y', motionGetPositionMM(1));
    webServer.setAxisPosition('Z', motionGetPositionMM(2));
    webServer.setAxisPosition('A', motionGetPositionMM(3));

    webServer.setSystemUptime(taskGetUptime());

    // Determine high-level system status string
    const char *status = "READY";
    if (motionIsEmergencyStopped()) {
      status = "E-STOP";
    } else if (safetyIsAlarmed()) {
      status = "ALARMED";
    } else if (motionIsMoving()) {
      status = "MOVING";
    }

    webServer.setSystemStatus(status);

    // Trigger the broadcast to all connected clients (with VFD and axis
    // metrics)
    webServer.broadcastState();

    watchdogFeed("Telemetry");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_TELEMETRY));
  }
}

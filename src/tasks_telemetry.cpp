/**
 * @file tasks_telemetry.cpp
 * @brief Telemetry & Web UI Update Task (PHASE 5.4)
 * @details Background telemetry collection on Core 0 to reduce Core 1 load
 * Collects system metrics, encoder diagnostics, and broadcasts to WebSocket clients
 * @author Sergio Faustino
 */

#include "task_manager.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_telemetry.h"  // PHASE 5.1: System telemetry
#include "encoder_diagnostics.h"  // PHASE 5.3: Encoder health monitoring
#include "load_manager.h"  // PHASE 5.3: Graceful degradation under load
#include "dashboard_metrics.h"  // PHASE 5.3: Web UI dashboard metrics
#include "web_server.h"
#include "motion.h"
#include "motion_state.h"
#include "safety.h"
#include "altivar31_modbus.h"  // PHASE 5.5: VFD current monitoring
#include "vfd_current_calibration.h"  // PHASE 5.5: Current calibration
#include "axis_synchronization.h"  // PHASE 5.6: Axis validation
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// External reference to the global WebServer instance
extern WebServerManager webServer;

// Static state for VFD telemetry cycling (PHASE 5.5)
static uint32_t vfd_telemetry_cycle = 0;

void taskTelemetryFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[TELEMETRY_TASK] [OK] Started on core 0 - Background collection");
  watchdogTaskAdd("Telemetry");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Telemetry");

  while (1) {
    // 1. Update System Telemetry (PHASE 5.1)
    // Collect comprehensive system metrics for API and diagnostics
    telemetryUpdate();

    // 2. Update Phase 5.3 Modules
    // Advanced encoder diagnostics, load management, and dashboard metrics
    encoderDiagnosticsUpdate();
    loadManagerUpdate();
    dashboardMetricsUpdate();

    // 3. PHASE 5.5: VFD Current Monitoring
    // Rotate through VFD register queries to avoid flooding the Modbus bus
    // Cycle through: current (every 1s), frequency (every 2s), thermal (every 3s)
    switch (vfd_telemetry_cycle % 3) {
        case 0:  // Query motor current
            altivar31ModbusReadCurrent();
            break;
        case 1:  // Query output frequency
            altivar31ModbusReadFrequency();
            break;
        case 2:  // Query thermal state
            altivar31ModbusReadThermalState();
            break;
    }
    vfd_telemetry_cycle++;

    // Attempt to receive response from pending Modbus query
    if (altivar31ModbusReceiveResponse()) {
        // Successfully received VFD data
        float current_amps = altivar31GetCurrentAmps();

        // Feed current sample to calibration system if measurement is active
        if (current_amps > 0.0f) {
            vfdCalibrationSampleCurrent(current_amps);
        }
    }

    // Push VFD telemetry to web UI (PHASE 5.5k)
    webServer.setVFDCurrent(altivar31GetCurrentAmps());
    webServer.setVFDFrequency(altivar31GetFrequencyHz());
    webServer.setVFDThermalState(altivar31GetThermalState());
    webServer.setVFDFaultCode(altivar31GetFaultCode());
    webServer.setVFDCalibrationThreshold(vfdCalibrationGetThreshold());
    webServer.setVFDCalibrationValid(vfdCalibrationIsValid());

    // 4. PHASE 5.6: Axis Synchronization Validation (Per-axis multiplexed VFD)
    // Update axis metrics for active axis only
    // Get current axis velocities
    float x_vel = fabsf(motionGetVelocityMMPerSec(0));
    float y_vel = fabsf(motionGetVelocityMMPerSec(1));
    float z_vel = fabsf(motionGetVelocityMMPerSec(2));

    // Determine active axis (which one is moving, or last one commanded)
    // NOTE: This assumes motion controller tracks which axis is currently active
    // For now, we use a simple heuristic: non-zero velocity = active
    uint8_t active_axis = 255;  // No active axis
    if (x_vel > 0.01f) active_axis = 0;
    else if (y_vel > 0.01f) active_axis = 1;
    else if (z_vel > 0.01f) active_axis = 2;

    // Current feedrate (target speed for active axis)
    float feedrate = motionGetCurrentFeedrate();
    float vfd_freq = altivar31GetFrequencyHz();

    // Update per-axis synchronization metrics
    axisSynchronizationUpdate(active_axis, x_vel, y_vel, z_vel, vfd_freq, feedrate);

    // Push axis metrics to web server
    const all_axes_metrics_t* all_metrics = axisSynchronizationGetAllMetrics();
    if (all_metrics) {
        for (int axis = 0; axis < 3; axis++) {
            const axis_metrics_t* metrics = axisSynchronizationGetAxisMetrics(axis);
            if (metrics) {
                webServer.setAxisQualityScore(axis, metrics->quality_score);
                webServer.setAxisJitterAmplitude(axis, metrics->velocity_jitter_mms);
                webServer.setAxisStalled(axis, metrics->stalled);
                webServer.setAxisVFDError(axis, metrics->vfd_encoder_error_percent);
            }
        }
    }

    // 5. Web Telemetry Broadcast
    // Push real-time state to the Web UI via WebSockets.

    // Use the Motion State Accessors to get physical units (MM)
    webServer.setAxisPosition('X', motionGetPositionMM(0));
    webServer.setAxisPosition('Y', motionGetPositionMM(1));
    webServer.setAxisPosition('Z', motionGetPositionMM(2));
    webServer.setAxisPosition('A', motionGetPositionMM(3));

    webServer.setSystemUptime(taskGetUptime());

    // Determine high-level system status string
    const char* status = "READY";
    if (motionIsEmergencyStopped()) {
        status = "E-STOP";
    } else if (safetyIsAlarmed()) {
        status = "ALARMED";
    } else if (motionIsMoving()) {
        status = "MOVING";
    }

    webServer.setSystemStatus(status);

    // Trigger the broadcast to all connected clients (with VFD and axis metrics)
    webServer.broadcastState();

    watchdogFeed("Telemetry");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_TELEMETRY));
  }
}

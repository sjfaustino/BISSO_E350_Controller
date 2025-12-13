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

    // 4. Web Telemetry Broadcast
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

    // Trigger the broadcast to all connected clients
    webServer.broadcastState();

    watchdogFeed("Telemetry");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_TELEMETRY));
  }
}

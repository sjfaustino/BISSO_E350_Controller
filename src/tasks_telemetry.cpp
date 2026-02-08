/**
 * @file tasks_telemetry.cpp
 * @brief Telemetry & Web UI Update Task (PHASE 5.4)
 * @details Background telemetry collection on Core 0 to reduce Core 1 load
 * Collects system metrics, encoder diagnostics, and broadcasts to WebSocket
 * clients
 * @author Sergio Faustino
 */

#include "altivar31_modbus.h"     // PHASE 5.5: VFD current monitoring
#include "jxk10_modbus.h"         // PHASE 5.0: JXK-10 Spindle Current
#include "axis_synchronization.h" // PHASE 5.6: Axis validation
#include "cutting_analytics.h"    // Stone cutting analytics
#include "spindle_current_monitor.h" // Added for load % telemetry
#include "dashboard_metrics.h"    // PHASE 5.3: Web UI dashboard metrics
#include "encoder_diagnostics.h"  // PHASE 5.3: Encoder health monitoring
#include "encoder_wj66.h"         // Fix: Include for ENCODER_OK
#include "config_keys.h"          // Helper for config keys
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
#include "task_performance_monitor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <esp_now.h>
#include "telemetry_packet.h"
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
    perfMonitorTaskStart(PERF_TASK_ID_TELEMETRY);
    // PHASE 6.1: Respect Load Manager suspension (Memory Stability Fix)
    if (!loadManagerIsSubsystemActive(LOAD_SUBSYS_TELEMETRY)) {
        watchdogFeed("Telemetry");
        perfMonitorTaskEnd(PERF_TASK_ID_TELEMETRY);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep longer when suspended to save CPU/Heap
        continue;
    }

    // --- PHASE 5.1 & 5.3: Heavy Telemetry Sampling (Throttled to 1Hz) ---
    static uint32_t last_heavy_telemetry = 0;
    bool runHeavy = (millis() - last_heavy_telemetry >= 1000);

    if (runHeavy) {
        // 1. Get snapshot (centralized sampling)
        system_telemetry_t snapshot = telemetryGetSnapshot();

        // 2. Update status and broadcast to WebUI
        webServer.setSpindleRPM(snapshot.spindle_rpm);
        webServer.setVFDCurrent(snapshot.spindle_current_amps);
        webServer.setVFDFrequency(snapshot.vfd_frequency_hz);
        webServer.setVFDThermalState(snapshot.vfd_thermal_state);
        webServer.setVFDFaultCode(snapshot.vfd_fault_code);
        webServer.setSpindleLoadPercent(snapshot.spindle_load_percent);
        webServer.setVFDConnected(snapshot.vfd_connected);
        webServer.setDROConnected(snapshot.dro_connected);
        webServer.setSpindleEfficiency(snapshot.spindle_efficiency);
        webServer.setSystemUptime(snapshot.uptime_seconds);

        for (int i = 0; i < 3; i++) {
            webServer.setAxisQualityScore(i, snapshot.axis_quality_score[i]);
            webServer.setAxisJitterAmplitude(i, snapshot.axis_jitter_mms[i]);
            webServer.setAxisStalled(i, snapshot.axis_stalled[i]);
            webServer.setAxisVFDError(i, snapshot.axis_vfd_error_percent[i]);
        }

        const char *status_str = "READY";
        if (snapshot.estop_active) status_str = "E-STOP";
        else if (snapshot.alarm_active) status_str = "ALARMED";
        else if (snapshot.motion_moving) status_str = "MOVING";
        webServer.setSystemStatus(status_str);

        webServer.broadcastState();
        last_heavy_telemetry = millis();
    }

    // PHASE 7.0: ESP-NOW Remote DRO Broadcast (10Hz)
    // Runs at 10Hz, task base is 50ms (20Hz) to ensure we always hit our 100ms window even under load
    static uint32_t last_esp_now_broadcast = 0;
    if (millis() - last_esp_now_broadcast >= 95) { 
        TelemetryPacket pkt;
        pkt.signature = 0x42495353; // "BISS"
        pkt.channel = (uint8_t)WiFi.channel();
        pkt.x = motionGetPositionMM(0);
        pkt.y = motionGetPositionMM(1);
        pkt.z = motionGetPositionMM(2);
        
        pkt.status = 0; // READY
        if (motionIsEmergencyStopped()) pkt.status = 3;
        else if (safetyIsAlarmed()) pkt.status = 2;
        else if (motionIsMoving()) pkt.status = 1;
        
        pkt.uptime = taskGetUptime();
        
        uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_send(broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
        last_esp_now_broadcast = millis();
    }

    watchdogFeed("Telemetry");
    perfMonitorTaskEnd(PERF_TASK_ID_TELEMETRY);
    
    // PHASE 6.1: Dynamic refresh rate based on system load/fragmentation
    uint32_t period = loadManagerGetAdjustedRefreshRate(TASK_PERIOD_TELEMETRY, LOAD_SUBSYS_TELEMETRY);
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(period));
  }
}

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
        // 1. Update System Telemetry (PHASE 5.1)
        telemetryUpdate();

        // 2. Update Phase 5.3 Modules
        encoderDiagnosticsUpdate();
        dashboardMetricsUpdate();
        
        // 2.5. Update Stone Cutting Analytics
        cuttingAnalyticsUpdate();

        // 3. Telemetry Processing (Registry Integrated)
        float current_amps = altivar31GetCurrentAmps();
        if (!isnan(current_amps) && current_amps > 0.0f && current_amps <= 100.0f) {
            vfdCalibrationSampleCurrent(current_amps);
        }

        float vfd_current = jxk10GetCurrentAmps();
        float vfd_frequency = altivar31GetFrequencyHz();
        int16_t vfd_thermal = altivar31GetThermalState();

        const jxk10_state_t* jxk_state = jxk10GetState();
        bool vfd_alive = jxk_state 
                         && jxk_state->enabled 
                         && (jxk_state->read_count > 5)
                         && (jxk_state->consecutive_errors < 5)
                         && (millis() - jxk_state->last_read_time_ms < 5000);

        int rated_rpm = configGetInt(KEY_SPINDLE_RATED_RPM, 1400);
        int blade_dia = configGetInt(KEY_BLADE_DIAMETER_MM, 350);
        float current_rpm = (vfd_alive && vfd_current > 1.0f) ? (float)rated_rpm : 0.0f;
        float current_speed = (current_rpm * 3.14159f * (float)blade_dia) / 60000.0f;
        
        webServer.setSpindleRPM(current_rpm);
        webServer.setSpindleSpeed(current_speed);
        webServer.setVFDCurrent(isnan(vfd_current) || vfd_current < 0.0f ? 0.0f : vfd_current);
        webServer.setVFDFrequency(isnan(vfd_frequency) || vfd_frequency < 0.0f ? 0.0f : vfd_frequency);
        webServer.setVFDThermalState((vfd_thermal < 0 || vfd_thermal > 200) ? 0 : vfd_thermal);
        webServer.setVFDFaultCode(altivar31GetFaultCode());
        webServer.setSpindleLoadPercent(spindleMonitorGetLoadPercent());
        webServer.setVFDCalibrationThreshold(vfdCalibrationGetThreshold());
        webServer.setVFDCalibrationValid(vfdCalibrationIsValid());
        webServer.setVFDConnected(vfd_alive);

        float actual_feedrate_mm_s = 0.0f;
        uint8_t active_axis = motionGetActiveAxis();
        if (active_axis < 3) actual_feedrate_mm_s = abs(motionGetVelocity(active_axis));
        
        float efficiency = (actual_feedrate_mm_s > 0.1f && vfd_current > 1.0f) ? vfd_current / actual_feedrate_mm_s : 0.0f;
        webServer.setSpindleEfficiency(efficiency);

        bool dro_alive = !wj66IsStale(0);
        webServer.setDROConnected(dro_alive);

        active_axis = motionGetActiveAxis();
        float x_vel = motionGetVelocity(0);
        float y_vel = motionGetVelocity(1);
        float z_vel = motionGetVelocity(2);
        float feedrate = motionGetFeedOverride();
        float vfd_freq_safe = (isnan(vfd_frequency) || vfd_frequency < 0.0f) ? 0.0f : vfd_frequency;

        axisSynchronizationUpdate(active_axis, x_vel, y_vel, z_vel, vfd_freq_safe, feedrate);

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

        webServer.setSystemUptime(taskGetUptime());

        const char *status_str = "READY";
        if (motionIsEmergencyStopped()) status_str = "E-STOP";
        else if (safetyIsAlarmed()) status_str = "ALARMED";
        else if (motionIsMoving()) status_str = "MOVING";

        webServer.setSystemStatus(status_str);

        // Broadcast to WebUI clients (1Hz is sufficient for browser)
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

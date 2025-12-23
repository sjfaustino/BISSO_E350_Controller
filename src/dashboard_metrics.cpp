/**
 * @file dashboard_metrics.cpp
 * @brief Dashboard Metrics Implementation (PHASE 5.3)
 */

#include "dashboard_metrics.h"
#include "system_telemetry.h"
#include "task_performance_monitor.h"
#include "encoder_diagnostics.h"
#include "load_manager.h"
#include "motion_state.h"
#include "spindle_current_monitor.h"
#include "safety.h"
#include "serial_logger.h"
#include <string.h>
#include <stdio.h>

static dashboard_metrics_t metrics_cache;
static uint32_t last_update_ms = 0;

void dashboardMetricsInit() {
    memset(&metrics_cache, 0, sizeof(metrics_cache));
    logInfo("[DASHBOARD] Metrics aggregator initialized");
}

void dashboardMetricsUpdate() {
    uint32_t now = millis();

    // Update at most every 200ms (5 Hz)
    if ((uint32_t)(now - last_update_ms) < 200) {
        return;
    }

    last_update_ms = now;

    // Collect system telemetry
    system_telemetry_t telemetry = telemetryGetSnapshot();
    metrics_cache.uptime_ms = now;
    metrics_cache.cpu_percent = telemetry.cpu_usage_percent;
    metrics_cache.free_heap_bytes = telemetry.free_heap_bytes;

    // Motion state
    metrics_cache.x_pos = telemetry.axis_x_mm;
    metrics_cache.y_pos = telemetry.axis_y_mm;
    metrics_cache.z_pos = telemetry.axis_z_mm;
    metrics_cache.a_pos = telemetry.axis_a_mm;
    metrics_cache.motion_moving = telemetry.motion_moving;
    metrics_cache.motion_enabled = telemetry.motion_enabled;

    // Safety
    metrics_cache.estop_active = telemetry.estop_active;
    metrics_cache.alarm_active = telemetry.alarm_active;
    metrics_cache.fault_count = telemetry.faults_logged;

    // Spindle
    metrics_cache.spindle_current_amps = telemetry.spindle_current_amps;
    metrics_cache.spindle_overcurrent = telemetry.spindle_overcurrent;

    // Network
    metrics_cache.wifi_connected = telemetry.wifi_connected;
    metrics_cache.wifi_signal = telemetry.wifi_signal_strength;

    // Task performance
    metrics_cache.slowest_task_id = telemetry.slowest_task_id;
    metrics_cache.slowest_task_us = telemetry.slowest_task_time_us;

    // Encoder health
    for (int i = 0; i < 4; i++) {
        const encoder_diagnostic_t* enc_diag = encoderDiagnosticsGetAxis(i);
        if (enc_diag) {
            metrics_cache.encoder_health[i] = enc_diag->health;
        }
    }

    // Load state
    load_status_t load_status = loadManagerGetStatus();
    metrics_cache.load_state = load_status.current_state;

    metrics_cache.timestamp_ms = now;
}

dashboard_metrics_t dashboardMetricsGetSnapshot() {
    dashboardMetricsUpdate();
    return metrics_cache;
}

size_t dashboardMetricsExportJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 512) return 0;

    // Compact JSON for frequent WebSocket updates
    // PHASE 5.10: Fixed duplicate "a" key (was A-axis and alarm, now "a" and "alm")
    size_t offset = snprintf(buffer, buffer_size,
        "{\"t\":%llu,\"cpu\":%u,\"heap\":%lu,"
        "\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"a\":%.2f,"
        "\"m\":%s,\"e\":%s,\"alm\":%s,\"s\":%.2f,"
        "\"w\":%s,\"ws\":%u,\"load\":%u}",
        (unsigned long long)metrics_cache.timestamp_ms,
        metrics_cache.cpu_percent,
        (unsigned long)metrics_cache.free_heap_bytes,
        metrics_cache.x_pos, metrics_cache.y_pos,
        metrics_cache.z_pos, metrics_cache.a_pos,
        metrics_cache.motion_moving ? "1" : "0",
        metrics_cache.motion_enabled ? "1" : "0",
        metrics_cache.alarm_active ? "1" : "0",
        metrics_cache.spindle_current_amps,
        metrics_cache.wifi_connected ? "1" : "0",
        metrics_cache.wifi_signal,
        metrics_cache.load_state);
    // PHASE 5.10: Check for buffer overflow
    if (offset >= buffer_size) return buffer_size - 1;

    return offset;
}

size_t dashboardMetricsExportExtendedJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 2048) return 0;

    size_t offset = 0;

    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\"system\":{"
        "\"uptime_ms\":%llu,\"cpu_percent\":%u,\"free_heap\":%lu},"
        "\"motion\":{"
        "\"position\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"a\":%.2f},"
        "\"moving\":%s,\"enabled\":%s},"
        "\"safety\":{"
        "\"estop\":%s,\"alarm\":%s,\"faults\":%lu},"
        "\"spindle\":{"
        "\"current_amps\":%.2f,\"overcurrent\":%s},"
        "\"network\":{"
        "\"wifi_connected\":%s,\"signal_percent\":%u},"
        "\"performance\":{"
        "\"slowest_task_id\":%u,\"slowest_task_us\":%lu},"
        "\"encoders\":[",
        (unsigned long long)metrics_cache.timestamp_ms,
        metrics_cache.cpu_percent,
        (unsigned long)metrics_cache.free_heap_bytes,
        metrics_cache.x_pos, metrics_cache.y_pos,
        metrics_cache.z_pos, metrics_cache.a_pos,
        metrics_cache.motion_moving ? "true" : "false",
        metrics_cache.motion_enabled ? "true" : "false",
        metrics_cache.estop_active ? "true" : "false",
        metrics_cache.alarm_active ? "true" : "false",
        (unsigned long)metrics_cache.fault_count,
        metrics_cache.spindle_current_amps,
        metrics_cache.spindle_overcurrent ? "true" : "false",
        metrics_cache.wifi_connected ? "true" : "false",
        metrics_cache.wifi_signal,
        metrics_cache.slowest_task_id,
        (unsigned long)metrics_cache.slowest_task_us);
    // PHASE 5.10: Check for buffer overflow after each snprintf
    if (offset >= buffer_size) return buffer_size - 1;

    // Add encoder status
    const char* health_strings[] = {"optimal", "normal", "degraded", "critical"};
    for (int i = 0; i < 4; i++) {
        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, ",");
            if (offset >= buffer_size) return buffer_size - 1;
        }
        offset += snprintf(buffer + offset, buffer_size - offset,
            "{\"axis\":%d,\"health\":\"%s\"}",
            i, health_strings[metrics_cache.encoder_health[i]]);
        if (offset >= buffer_size) return buffer_size - 1;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "],\"load_state\":%u}",
                      metrics_cache.load_state);
    if (offset >= buffer_size) return buffer_size - 1;

    return offset;
}

void dashboardMetricsPrint() {
    Serial.println("\n[DASHBOARD] === Real-time Metrics ===");
    Serial.printf("Uptime: %llu ms | CPU: %u%% | Heap: %lu bytes\n",
                 (unsigned long long)metrics_cache.timestamp_ms,
                 metrics_cache.cpu_percent,
                 (unsigned long)metrics_cache.free_heap_bytes);

    Serial.printf("Position: X=%.2f Y=%.2f Z=%.2f A=%.2f mm\n",
                 metrics_cache.x_pos, metrics_cache.y_pos,
                 metrics_cache.z_pos, metrics_cache.a_pos);

    Serial.printf("Motion: %s | Safety: %s | Alarm: %s\n",
                 metrics_cache.motion_moving ? "Moving" : "Stopped",
                 metrics_cache.estop_active ? "E-STOP" : "OK",
                 metrics_cache.alarm_active ? "ALARMED" : "OK");

    Serial.printf("Spindle: %.2f A | WiFi: %s (%u%%)\n",
                 metrics_cache.spindle_current_amps,
                 metrics_cache.wifi_connected ? "Connected" : "Disconnected",
                 metrics_cache.wifi_signal);

    Serial.printf("Encoders: [");
    for (int i = 0; i < 4; i++) {
        if (i > 0) Serial.print(" ");
        switch (metrics_cache.encoder_health[i]) {
            case 0: Serial.print("✓"); break;
            case 1: Serial.print("~"); break;
            case 2: Serial.print("⚠"); break;
            case 3: Serial.print("✗"); break;
        }
    }
    Serial.println("]");

    Serial.println();
}

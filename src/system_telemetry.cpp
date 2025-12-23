/**
 * @file system_telemetry.cpp
 * @brief System Telemetry Implementation (PHASE 5.1)
 */

#include "system_telemetry.h"
#include "serial_logger.h"
#include "motion.h"
#include "motion_state.h"
#include "safety.h"
#include "spindle_current_monitor.h"
#include "fault_logging.h"
#include "memory_monitor.h"
#include "task_manager.h"
#include "task_performance_monitor.h"
#include "config_unified.h"
#include "config_keys.h"
#include "system_constants.h"
#include <string.h>
#include <stdio.h>
#include <Arduino.h>
#include <WiFi.h>

// PHASE 5.10: Add spinlock to protect cache from concurrent read/write races
static portMUX_TYPE telemetrySpinlock = portMUX_INITIALIZER_UNLOCKED;

// Telemetry cache
static system_telemetry_t telemetry_cache;
static uint32_t last_update_ms = 0;
static uint32_t loop_cycle_counter = 0;

const char* telemetryGetHealthStatusString(system_health_t status) {
    switch (status) {
        case HEALTH_CRITICAL: return "CRITICAL";
        case HEALTH_WARNING: return "WARNING";
        case HEALTH_NORMAL: return "NORMAL";
        case HEALTH_OPTIMAL: return "OPTIMAL";
        default: return "UNKNOWN";
    }
}

static system_health_t calculateHealthStatus() {
    // Determine system health based on multiple factors
    uint8_t cpu = taskGetCpuUsage();
    uint32_t free_heap = memoryMonitorGetFreeHeap();

    if (safetyIsAlarmed() || emergencyStopIsActive()) {
        return HEALTH_CRITICAL;
    }

    if (cpu > 90 || free_heap < 10000) {
        return HEALTH_WARNING;
    }

    if (cpu > 70 || free_heap < 50000) {
        return HEALTH_NORMAL;
    }

    return HEALTH_OPTIMAL;
}

void telemetryInit() {
    memset(&telemetry_cache, 0, sizeof(telemetry_cache));
    logInfo("[TELEMETRY] Initialized");
}

void telemetryUpdate() {
    uint32_t now = millis();

    // Update cache only every 500ms to reduce overhead
    if ((uint32_t)(now - last_update_ms) < 500) {
        loop_cycle_counter++;
        return;
    }

    last_update_ms = now;

    // PHASE 5.10: Protect cache writes with spinlock to prevent torn reads
    portENTER_CRITICAL(&telemetrySpinlock);

    // System Status
    telemetry_cache.health_status = calculateHealthStatus();
    telemetry_cache.uptime_seconds = taskGetUptime();
    telemetry_cache.loop_cycle_count = loop_cycle_counter;

    // CPU & Memory
    telemetry_cache.cpu_usage_percent = taskGetCpuUsage();
    telemetry_cache.free_heap_bytes = memoryMonitorGetFreeHeap();
    telemetry_cache.stack_used_bytes = (TASK_STACK_BOOT * 4) - (uxTaskGetStackHighWaterMark(NULL) * 4);

    // Motion System
    telemetry_cache.motion_enabled = (motionGetState(0) != MOTION_ERROR);
    telemetry_cache.motion_moving = motionIsMoving();
    telemetry_cache.axis_x_mm = motionGetPositionMM(0);
    telemetry_cache.axis_y_mm = motionGetPositionMM(1);
    telemetry_cache.axis_z_mm = motionGetPositionMM(2);
    telemetry_cache.axis_a_mm = motionGetPositionMM(3);

    // Spindle
    const spindle_monitor_state_t* spindle_state = spindleMonitorGetState();
    if (spindle_state) {
        telemetry_cache.spindle_enabled = spindle_state->enabled;
        telemetry_cache.spindle_current_amps = spindle_state->current_amps;
        telemetry_cache.spindle_current_peak_amps = spindle_state->current_peak_amps;
        telemetry_cache.spindle_errors = spindle_state->error_count;
        telemetry_cache.spindle_overcurrent = spindleMonitorIsOvercurrent();
        telemetry_cache.spindle_fault = spindleMonitorIsFault();
    }

    // Safety System
    telemetry_cache.estop_active = emergencyStopIsActive();
    telemetry_cache.alarm_active = safetyIsAlarmed();
    telemetry_cache.faults_logged = faultGetRingBufferEntryCount();
    telemetry_cache.critical_faults = 0;  // TODO: Count critical faults in ring buffer

    // Task Metrics
    int task_count = 0;
    const task_performance_t* task_metrics = perfMonitorGetAllMetrics(&task_count);
    uint32_t max_task_time = 0;
    for (int i = 0; i < task_count; i++) {
        if (task_metrics[i].max_runtime_us > max_task_time) {
            max_task_time = task_metrics[i].max_runtime_us;
            telemetry_cache.slowest_task_id = i;
        }
    }
    telemetry_cache.slowest_task_time_us = max_task_time;

    // Network
    telemetry_cache.wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (telemetry_cache.wifi_connected) {
        int rssi = WiFi.RSSI();  // Returns negative dBm
        // Convert RSSI to 0-100 scale (typical range -100 to -30 dBm)
        // PHASE 5.10: Clamp to prevent underflow for RSSI < -100
        int signal_raw = (rssi + 100) * 100 / 70;
        if (signal_raw < 0) {
            telemetry_cache.wifi_signal_strength = 0;
        } else if (signal_raw > 100) {
            telemetry_cache.wifi_signal_strength = 100;
        } else {
            telemetry_cache.wifi_signal_strength = (uint8_t)signal_raw;
        }
    }

    // Configuration
    telemetry_cache.config_version = configGetInt("schema_version", 1);
    telemetry_cache.config_is_default = (configGetInt(KEY_WEB_PW_CHANGED, 0) == 0);

    portEXIT_CRITICAL(&telemetrySpinlock);
}

system_telemetry_t telemetryGetSnapshot() {
    telemetryUpdate();

    // PHASE 5.10: Atomic copy under spinlock protection
    portENTER_CRITICAL(&telemetrySpinlock);
    system_telemetry_t snapshot = telemetry_cache;
    portEXIT_CRITICAL(&telemetrySpinlock);

    return snapshot;
}

system_health_t telemetryGetHealthStatus() {
    return calculateHealthStatus();
}

size_t telemetryExportJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 512) return 0;

    system_telemetry_t t = telemetryGetSnapshot();
    size_t offset = 0;

    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\"system\":{\"health\":\"%s\",\"uptime_sec\":%lu,\"cpu_percent\":%u},"
        "\"memory\":{\"free_bytes\":%lu,\"stack_used\":%lu},"
        "\"motion\":{\"enabled\":%s,\"moving\":%s,\"x_mm\":%.2f,\"y_mm\":%.2f,\"z_mm\":%.2f,\"a_mm\":%.2f},"
        "\"spindle\":{\"enabled\":%s,\"current_amps\":%.2f,\"peak_amps\":%.2f,\"errors\":%lu,"
        "\"overcurrent\":%s,\"fault\":%s},"
        "\"safety\":{\"estop\":%s,\"alarm\":%s,\"faults\":%lu,\"critical\":%lu},"
        "\"tasks\":{\"slowest_id\":%u,\"slowest_us\":%lu},"
        "\"network\":{\"wifi_connected\":%s,\"signal_percent\":%u},"
        "\"config\":{\"version\":%lu,\"is_default\":%s}}",
        telemetryGetHealthStatusString(t.health_status),
        (unsigned long)t.uptime_seconds,
        t.cpu_usage_percent,
        (unsigned long)t.free_heap_bytes,
        (unsigned long)t.stack_used_bytes,
        t.motion_enabled ? "true" : "false",
        t.motion_moving ? "true" : "false",
        t.axis_x_mm, t.axis_y_mm, t.axis_z_mm, t.axis_a_mm,
        t.spindle_enabled ? "true" : "false",
        t.spindle_current_amps,
        t.spindle_current_peak_amps,
        (unsigned long)t.spindle_errors,
        t.spindle_overcurrent ? "true" : "false",
        t.spindle_fault ? "true" : "false",
        t.estop_active ? "true" : "false",
        t.alarm_active ? "true" : "false",
        (unsigned long)t.faults_logged,
        (unsigned long)t.critical_faults,
        t.slowest_task_id,
        (unsigned long)t.slowest_task_time_us,
        t.wifi_connected ? "true" : "false",
        t.wifi_signal_strength,
        (unsigned long)t.config_version,
        t.config_is_default ? "true" : "false");
    // PHASE 5.10: Check for buffer overflow
    if (offset >= buffer_size) return buffer_size - 1;

    return offset;
}

size_t telemetryExportCompactJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 256) return 0;

    system_telemetry_t t = telemetryGetSnapshot();
    size_t offset = 0;

    // Lightweight version - only most critical fields
    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\"health\":\"%s\",\"uptime\":%lu,\"cpu\":%u,\"heap\":%lu,"
        "\"motion_moving\":%s,\"estop\":%s,\"wifi\":%s}",
        telemetryGetHealthStatusString(t.health_status),
        (unsigned long)t.uptime_seconds,
        t.cpu_usage_percent,
        (unsigned long)t.free_heap_bytes,
        t.motion_moving ? "true" : "false",
        t.estop_active ? "true" : "false",
        t.wifi_connected ? "true" : "false");
    // PHASE 5.10: Check for buffer overflow
    if (offset >= buffer_size) return buffer_size - 1;

    return offset;
}

void telemetryPrintSummary() {
    system_telemetry_t t = telemetryGetSnapshot();

    Serial.println("\n[TELEMETRY] === System Health Summary ===");
    Serial.printf("Health: %s | Uptime: %lu sec | CPU: %u%% | Heap: %lu bytes\n",
        telemetryGetHealthStatusString(t.health_status),
        (unsigned long)t.uptime_seconds,
        t.cpu_usage_percent,
        (unsigned long)t.free_heap_bytes);

    Serial.printf("Motion: %s (Moving: %s)\n",
        t.motion_enabled ? "Enabled" : "Disabled",
        t.motion_moving ? "Yes" : "No");

    Serial.printf("Spindle: %s (Current: %.2f A, Peak: %.2f A)\n",
        t.spindle_enabled ? "Enabled" : "Disabled",
        t.spindle_current_amps,
        t.spindle_current_peak_amps);

    Serial.printf("Safety: E-STOP %s, Alarm %s, Faults: %lu\n",
        t.estop_active ? "ACTIVE" : "OK",
        t.alarm_active ? "ACTIVE" : "OK",
        (unsigned long)t.faults_logged);

    Serial.printf("Network: WiFi %s (Signal: %u%%)\n",
        t.wifi_connected ? "Connected" : "Disconnected",
        t.wifi_signal_strength);

    Serial.println();
}

void telemetryPrintDetailed() {
    system_telemetry_t t = telemetryGetSnapshot();

    Serial.println("\n[TELEMETRY] === Detailed System Telemetry ===");

    Serial.println("=== SYSTEM ===");
    Serial.printf("Health Status: %s\n", telemetryGetHealthStatusString(t.health_status));
    Serial.printf("Uptime: %lu seconds\n", (unsigned long)t.uptime_seconds);
    Serial.printf("Loop Cycles: %lu\n", (unsigned long)t.loop_cycle_count);

    Serial.println("\n=== CPU & MEMORY ===");
    Serial.printf("CPU Usage: %u%%\n", t.cpu_usage_percent);
    Serial.printf("Free Heap: %lu bytes\n", (unsigned long)t.free_heap_bytes);
    Serial.printf("Stack Used: %lu bytes\n", (unsigned long)t.stack_used_bytes);

    Serial.println("\n=== MOTION ===");
    Serial.printf("Enabled: %s | Moving: %s\n",
        t.motion_enabled ? "Yes" : "No",
        t.motion_moving ? "Yes" : "No");
    Serial.printf("Position: X=%.2f Y=%.2f Z=%.2f A=%.2f mm\n",
        t.axis_x_mm, t.axis_y_mm, t.axis_z_mm, t.axis_a_mm);
    Serial.printf("Steps Executed: %lu\n", (unsigned long)t.steps_executed);
    Serial.printf("Motion Errors: %lu\n", (unsigned long)t.motion_errors);

    Serial.println("\n=== SPINDLE ===");
    Serial.printf("Enabled: %s\n", t.spindle_enabled ? "Yes" : "No");
    Serial.printf("Current: %.2f A (Peak: %.2f A)\n",
        t.spindle_current_amps, t.spindle_current_peak_amps);
    Serial.printf("Errors: %lu | Overcurrent: %s | Fault: %s\n",
        (unsigned long)t.spindle_errors,
        t.spindle_overcurrent ? "Yes" : "No",
        t.spindle_fault ? "Yes" : "No");

    Serial.println("\n=== SAFETY ===");
    Serial.printf("E-STOP: %s\n", t.estop_active ? "ACTIVE" : "OK");
    Serial.printf("Alarm: %s\n", t.alarm_active ? "ACTIVE" : "OK");
    Serial.printf("Faults Logged: %lu (Critical: %lu)\n",
        (unsigned long)t.faults_logged,
        (unsigned long)t.critical_faults);
    Serial.printf("Safety Events: %lu\n", (unsigned long)t.safety_events);

    Serial.println("\n=== TASKS ===");
    Serial.printf("Slowest Task: ID %u (%lu µs)\n",
        t.slowest_task_id,
        (unsigned long)t.slowest_task_time_us);
    Serial.printf("Task Underruns: %lu\n", (unsigned long)t.total_task_underruns);

    Serial.println("\n=== NETWORK ===");
    Serial.printf("WiFi Connected: %s\n", t.wifi_connected ? "Yes" : "No");
    Serial.printf("Signal Strength: %u%%\n", t.wifi_signal_strength);
    Serial.printf("HTTP Requests: %lu | Errors: %lu\n",
        (unsigned long)t.http_requests_served,
        (unsigned long)t.http_errors);

    Serial.println("\n=== CONFIGURATION ===");
    Serial.printf("Config Version: %lu\n", (unsigned long)t.config_version);
    Serial.printf("Using Defaults: %s\n", t.config_is_default ? "Yes" : "No");
    Serial.printf("Config Changes: %lu\n", (unsigned long)t.config_changes_count);
    Serial.printf("Watchdog Resets: %lu\n", (unsigned long)t.watchdog_resets);

    Serial.println();
}

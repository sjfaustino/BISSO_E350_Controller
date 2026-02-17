/**
 * @file system_telemetry.cpp
 * @brief System Telemetry Implementation (PHASE 5.1)
 */

#include "system_telemetry.h"
#include "lcd_interface.h" // Added for lcdInterfaceGetContent
#include "telemetry_history.h" // PSRAM history
#include "firmware_version.h"
#include "serial_logger.h"
#include "mcu_info.h"
#include "altivar31_modbus.h"  // VFD run status
#include "axis_synchronization.h"
#include "dashboard_metrics.h"
#include "vfd_current_calibration.h"
#include "encoder_wj66.h"
#include "jxk10_modbus.h"
#include "motion.h"
#include "motion_state.h"
#include "safety.h"
#include "motion_buffer.h"
#include "spindle_current_monitor.h"
#include "yhtc05_modbus.h"  // YH-TC05 RPM sensor
#include "fault_logging.h"
#include "plc_iface.h"  // For plcIsHardwarePresent()
#include "memory_monitor.h"
#include "task_manager.h"
#include "task_performance_monitor.h"
#include "config_unified.h"
#include "config_keys.h"
#include "config_schema_versioning.h"
#include "gcode_parser.h"
#include "system_constants.h"
#include "rtc_manager.h"
#include "sd_card_manager.h"
#include "system_events.h"
#include <string.h>
#include <stdio.h>
#include "string_safety.h"
#include <cmath>
#include <Arduino.h>
#include <WiFi.h>

// PHASE 5.10: Add spinlock to protect cache from concurrent read/write races
static portMUX_TYPE telemetrySpinlock = portMUX_INITIALIZER_UNLOCKED;

// Telemetry cache
static system_telemetry_t telemetry_cache;
static uint32_t last_update_ms = 0;
static uint32_t loop_cycle_counter = 0;

// PHASE 5.10: Track WiFi connection state changes for event signaling
static bool wifi_was_connected = false;

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
    telemetryHistoryInit(); // Initialize PSRAM buffer
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

    // =========================================================================
    // PHASE 1: Collect all data OUTSIDE critical section
    // WiFi calls, malloc, logging, etc. MUST NOT be inside spinlock
    // =========================================================================

    // System Status
    system_health_t health = calculateHealthStatus();
    uint32_t uptime = taskGetUptime();
    uint32_t cycles = loop_cycle_counter;

    // CPU & Memory
    uint8_t cpu = taskGetCpuUsage();
    float soc_temp = temperatureRead();
    uint32_t free_heap = memoryMonitorGetFreeHeap();
    uint32_t stack_used = (TASK_STACK_BOOT * 4) - (uxTaskGetStackHighWaterMark(NULL) * 4);

    // Motion System
    bool motion_en = (motionGetState(0) != MOTION_ERROR);
    bool motion_mov = motionIsMoving();
    float ax_x = motionGetPositionMM(0);
    float ax_y = motionGetPositionMM(1);
    float ax_z = motionGetPositionMM(2);
    float ax_a = motionGetPositionMM(3);
    
    float wco[4] = {0, 0, 0, 0};
    gcodeParser.getWCO(wco);

    // Spindle
    bool sp_en = false;
    bool sp_running = false;    // Actual motor running state from VFD
    float sp_current = 0.0f;
    float sp_peak = 0.0f;
    uint32_t sp_errors = 0;
    bool sp_overcurrent = false;
    bool sp_fault = false;
    const spindle_monitor_state_t* spindle_state = spindleMonitorGetState();
    if (spindle_state) {
        sp_en = spindle_state->enabled;
        sp_current = spindle_state->current_amps;
        sp_peak = spindle_state->current_peak_amps;
        sp_errors = spindle_state->error_count;
        sp_overcurrent = spindleMonitorIsOvercurrent();
        sp_fault = spindleMonitorIsFault();
    }
    // Get actual run status from VFD Modbus status word
    sp_running = altivar31IsRunning();

    // RPM Sensor (YH-TC05)
    bool rpm_en = false;
    uint16_t rpm_val = 0;
    bool rpm_stall = false;
    const yhtc05_state_t* rpm_state = yhtc05GetState();
    if (rpm_state) {
        rpm_en = rpm_state->enabled;
        rpm_val = rpm_state->rpm;
        rpm_stall = rpm_state->is_stalled;
    }

    // Safety System
    bool estop = emergencyStopIsActive();
    bool alarm = safetyIsAlarmed();
    uint32_t faults = faultGetRingBufferEntryCount();
    
    // Count critical faults
    uint32_t critical_count = 0;
    uint8_t entry_count = faultGetRingBufferEntryCount();
    for (uint8_t i = 0; i < entry_count; i++) {
        const fault_entry_t* entry = faultGetRingBufferEntry(i);
        if (entry && entry->severity == FAULT_CRITICAL) {
            critical_count++;
        }
    }

    // Task Metrics
    taskUpdateStackUsage();
    uint8_t slowest_id = 0;
    uint32_t slowest_us = 0;
    int task_count = 0;
    const task_performance_t* task_metrics = perfMonitorGetAllMetrics(&task_count);
    for (int i = 0; i < task_count; i++) {
        if (task_metrics[i].max_runtime_us > slowest_us) {
            slowest_us = task_metrics[i].max_runtime_us;
            slowest_id = i;
        }
    }

    // Network - MUST be outside critical section (WiFi uses interrupts)
    bool wifi_conn = (WiFi.status() == WL_CONNECTED);
    uint8_t wifi_signal = 0;
    if (wifi_conn) {
        int rssi = WiFi.RSSI();
        int signal_raw = (rssi + 100) * 100 / 70;
        if (signal_raw < 0) wifi_signal = 0;
        else if (signal_raw > 100) wifi_signal = 100;
        else wifi_signal = (uint8_t)signal_raw;
    }

    // RTC Battery Status
    bool rtc_low = false;
    #if BOARD_HAS_RTC_DS3231
        rtc_low = rtcHasBatteryWarning();
    #endif

    // Configuration
    // uint32_t cfg_ver = configGetInt("schema_version", 1); // Unused
    bool cfg_default = (configGetInt(KEY_WEB_PW_CHANGED, 0) == 0);

    // VFD & Spindle Load (DRY Phase 8)
    const jxk10_state_t* jxk_state = jxk10GetState();
    bool vfd_alive = jxk_state 
                     && jxk_state->enabled 
                     && (jxk_state->read_count > 5)
                     && (jxk_state->consecutive_errors < 5)
                     && (now - jxk_state->last_read_time_ms < 5000);

    float vfd_freq = altivar31GetFrequencyHz();
    float vfd_current = jxk10GetCurrentAmps();
    int16_t vfd_thermal = altivar31GetThermalState();
    uint32_t vfd_fault = altivar31GetFaultCode();
    float spindle_load = spindleMonitorGetLoadPercent();
    
    // MCU Identification
    char mcu_rev[16];
    char mcu_ser[32];
    mcuGetRevisionString(mcu_rev, sizeof(mcu_rev));
    uint64_t mac = ESP.getEfuseMac();
    snprintf(mcu_ser, sizeof(mcu_ser), "BS-E350-%02X%02X", (uint8_t)(mac >> 8), (uint8_t)mac);

    float actual_feedrate_mm_s = 0.0f;
    uint8_t active_axis = motionGetActiveAxis();
    if (active_axis < 3) actual_feedrate_mm_s = abs(motionGetVelocity(active_axis));
    float efficiency = (actual_feedrate_mm_s > 0.1f && vfd_current > 1.0f) ? vfd_current / actual_feedrate_mm_s : 0.0f;

    // Axis Quality (DRY Phase 8)
    float q_scores[3] = {0};
    float j_amps[3] = {0};
    bool a_stalled[3] = {false};
    float v_errs[3] = {0};
    bool m_warns[3] = {false};
    
    axisSynchronizationLock();
    for (int i = 0; i < 3; i++) {
        const axis_metrics_t *metrics = axisSynchronizationGetAxisMetrics(i);
        if (metrics) {
            q_scores[i] = metrics->quality_score;
            j_amps[i] = metrics->velocity_jitter_mms;
            a_stalled[i] = metrics->stalled;
            v_errs[i] = metrics->vfd_encoder_error_percent;
            m_warns[i] = metrics->jitter_elevated;
        }
    }
    axisSynchronizationUnlock();

    bool dro_alive = !wj66IsStale(0);

    // SD Card Status (PHASE 6.6)
    bool sd_mount = sdCardIsMounted();
    uint8_t sd_health = (uint8_t)sdCardGetLastHealth();
    static uint64_t last_sd_total = 0;
    static uint64_t last_sd_used = 0;
    static uint32_t last_sd_space_update = 0;

    if (sd_mount && (now - last_sd_space_update >= 10000 || last_sd_space_update == 0)) {
        last_sd_space_update = now;
        SDCardInfo info;
        if (sdCardGetInfo(&info)) {
            last_sd_total = info.totalBytes;
            last_sd_used = info.usedBytes;
        }
    }

    // =========================================================================
    // PHASE 2: Write to cache under spinlock (fast, no function calls)
    // =========================================================================
    portENTER_CRITICAL(&telemetrySpinlock);

    telemetry_cache.health_status = health;
    telemetry_cache.uptime_seconds = uptime;
    telemetry_cache.loop_cycle_count = cycles;
    telemetry_cache.cpu_usage_percent = cpu;
    telemetry_cache.temperature = soc_temp;
    telemetry_cache.free_heap_bytes = free_heap;
    telemetry_cache.stack_used_bytes = stack_used;
    telemetry_cache.motion_enabled = motion_en;
    telemetry_cache.motion_moving = motion_mov;
    telemetry_cache.axis_x_mm = ax_x;
    telemetry_cache.axis_y_mm = ax_y;
    telemetry_cache.axis_z_mm = ax_z;
    telemetry_cache.axis_a_mm = ax_a;
    memcpy(telemetry_cache.wcs_offset_mm, wco, sizeof(wco));
    telemetry_cache.active_wcs = (uint8_t)gcodeParser.getCurrentWCOSystem();
    telemetry_cache.spindle_enabled = sp_en;
    telemetry_cache.spindle_running = sp_running;
    telemetry_cache.spindle_current_amps = sp_current;
    telemetry_cache.spindle_current_peak_amps = sp_peak;
    telemetry_cache.spindle_errors = sp_errors;
    telemetry_cache.spindle_overcurrent = sp_overcurrent;
    telemetry_cache.spindle_fault = sp_fault;
    telemetry_cache.rpm_sensor_enabled = rpm_en;
    telemetry_cache.spindle_rpm = rpm_val;
    telemetry_cache.rpm_stall_detected = rpm_stall;
    telemetry_cache.estop_active = estop;
    telemetry_cache.alarm_active = alarm;
    telemetry_cache.faults_logged = faults;
    telemetry_cache.critical_faults = critical_count;
    telemetry_cache.slowest_task_id = slowest_id;
    telemetry_cache.slowest_task_time_us = slowest_us;
    telemetry_cache.wifi_connected = wifi_conn;
    telemetry_cache.wifi_signal_strength = wifi_signal;
    telemetry_cache.config_is_default = cfg_default;
    telemetry_cache.plc_hardware_present = plcIsHardwarePresent();
    telemetry_cache.rtc_battery_low = rtc_low;
    
    // SD Card (PHASE 6.6)
    telemetry_cache.sd_mounted = sd_mount;
    telemetry_cache.sd_health = sd_health;
    telemetry_cache.sd_total_bytes = last_sd_total;
    telemetry_cache.sd_used_bytes = last_sd_used;
    
    telemetry_cache.motion_errors = faultGetStats().motion_faults;
    telemetry_cache.motion_buffer_count = motionBuffer.available();
    telemetry_cache.motion_buffer_capacity = motionBuffer.getCapacity();
    
    // DRY Phase 8 Fields
    telemetry_cache.vfd_connected = vfd_alive;
    telemetry_cache.vfd_frequency_hz = (isnan(vfd_freq) || vfd_freq < 0.0f) ? 0.0f : vfd_freq;
    telemetry_cache.vfd_thermal_state = (vfd_thermal < 0 || vfd_thermal > 200) ? 0 : vfd_thermal;
    telemetry_cache.vfd_fault_code = vfd_fault;
    telemetry_cache.vfd_threshold_amps = vfdCalibrationGetThreshold();
    telemetry_cache.vfd_calibration_valid = vfdCalibrationIsValid();
    telemetry_cache.spindle_load_percent = spindle_load;
    telemetry_cache.spindle_efficiency = efficiency;
    telemetry_cache.dro_connected = dro_alive;
    
    memcpy(telemetry_cache.axis_quality_score, q_scores, sizeof(q_scores));
    memcpy(telemetry_cache.axis_jitter_mms, j_amps, sizeof(j_amps));
    memcpy(telemetry_cache.axis_stalled, a_stalled, sizeof(a_stalled));
    memcpy(telemetry_cache.axis_vfd_error_percent, v_errs, sizeof(v_errs));
    memcpy(telemetry_cache.axis_maintenance_warning, m_warns, sizeof(m_warns));

    // Parser State
    telemetry_cache.parser_absolute_mode = (gcodeParser.getDistanceMode() == G_MODE_ABSOLUTE);
    telemetry_cache.parser_req_feedrate = gcodeParser.getCurrentFeedRate();
    telemetry_cache.parser_actual_feedrate = actual_feedrate_mm_s;

    // Identification
    SAFE_STRCPY(telemetry_cache.mcu_revision, mcu_rev, sizeof(telemetry_cache.mcu_revision));
    SAFE_STRCPY(telemetry_cache.mcu_serial, mcu_ser, sizeof(telemetry_cache.mcu_serial));
    SAFE_STRCPY(telemetry_cache.mcu_model, mcuGetModelName(), sizeof(telemetry_cache.mcu_model));
    SAFE_STRCPY(telemetry_cache.system_status_string, "NORMAL", sizeof(telemetry_cache.system_status_string)); // Default status string

    // LCD Mirror
    lcdInterfaceGetContent(telemetry_cache.lcd_lines);

    portEXIT_CRITICAL(&telemetrySpinlock);

    // PHASE 6: Add to PSRAM History (1Hz)
    static uint32_t last_history_ms = 0;
    if (now - last_history_ms >= 1000) {
        last_history_ms = now;
        telemetry_packet_t packet;
        telemetryExportBinary(&packet);
        telemetryHistoryAdd(&packet);
    }

    // =========================================================================
    // PHASE 3: Signal events OUTSIDE critical section (may log/allocate)
    // =========================================================================
    if (wifi_conn && !wifi_was_connected) {
        systemEventsSystemSet(EVENT_SYSTEM_NETWORK_CONNECTED);
        logInfo("[TELEMETRY] WiFi connected");
    } else if (!wifi_conn && wifi_was_connected) {
        systemEventsSystemSet(EVENT_SYSTEM_NETWORK_LOST);
        logWarning("[TELEMETRY] WiFi connection lost");
    }
    wifi_was_connected = wifi_conn;
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

size_t telemetryExportJSON(char* buffer, size_t buffer_size, bool full) {
    if (!buffer || buffer_size < 1024) return 0;

    system_telemetry_t t = telemetryGetSnapshot();
    
    // Identification Mode
    char ver_str[32];
    snprintf(ver_str, sizeof(ver_str), "v%d.%d.%d", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);

    int n = snprintf(buffer, buffer_size,
        "{\"system\":{\"status\":\"%s\",\"health\":\"%s\",\"uptime_sec\":%lu,\"cpu_percent\":%u,\"free_heap_bytes\":%lu,\"temperature\":%.1f,"
        "\"firmware_version\":\"%s\",\"build_date\":\"%s\",\"rtc_battery_low\":%s",
        t.system_status_string,
        telemetryGetHealthStatusString(t.health_status),
        (unsigned long)t.uptime_seconds,
        t.cpu_usage_percent,
        (unsigned long)t.free_heap_bytes,
        t.temperature,
        ver_str,
        __DATE__,
        t.rtc_battery_low ? "true" : "false"
    );

    if (n < 0 || (size_t)n >= buffer_size) return 0;
    size_t offset = (size_t)n;

    if (full) {
        n = snprintf(buffer + offset, buffer_size - offset,
            ",\"plc_hardware_present\":%s,\"hw_model\":\"%s\",\"hw_mcu\":\"%s\",\"hw_revision\":\"%s\",\"hw_serial\":\"%s\"",
            t.plc_hardware_present ? "true" : "false",
            t.mcu_model, mcuGetModelName(), t.mcu_revision, t.mcu_serial);
        if (n > 0 && (offset + n) < buffer_size) offset += n;
    }

    n = snprintf(buffer + offset, buffer_size - offset,
        "},\"x_mm\":%.3f,\"y_mm\":%.3f,\"z_mm\":%.3f,\"a_mm\":%.3f,\"motion_active\":%s,"
        "\"motion\":{\"moving\":%s,\"buffer_count\":%d,\"buffer_capacity\":%d,\"dro_connected\":%s},",
        t.axis_x_mm, t.axis_y_mm, t.axis_z_mm, t.axis_a_mm,
        t.motion_moving ? "true" : "false",
        t.motion_moving ? "true" : "false",
        t.motion_buffer_count,
        t.motion_buffer_capacity,
        t.dro_connected ? "true" : "false");
    if (n > 0 && (offset + n) < buffer_size) offset += n;

    n = snprintf(buffer + offset, buffer_size - offset,
        "\"vfd\":{\"current_amps\":%.2f,\"frequency_hz\":%.2f,\"thermal_percent\":%d,\"fault_code\":%u,"
        "\"stall_threshold\":%.2f,\"calibration_valid\":%s,\"connected\":%s,\"rpm\":%.1f,\"speed_m_s\":%.2f,\"efficiency\":%.2f,\"load_pct\":%.1f},",
        t.spindle_current_amps, t.vfd_frequency_hz, t.vfd_thermal_state, t.vfd_fault_code,
        t.vfd_threshold_amps, t.vfd_calibration_valid ? "true" : "false", t.vfd_connected ? "true" : "false",
        (float)t.spindle_rpm, 0.0f, t.spindle_efficiency, t.spindle_load_percent);
    if (n > 0 && (offset + n) < buffer_size) offset += n;

    n = snprintf(buffer + offset, buffer_size - offset,
        "\"axis\":{\"x\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s},"
        "\"y\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s},"
        "\"z\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s}},"
        "\"network\":{\"wifi_connected\":%s,\"signal_percent\":%u},",
        (uint32_t)t.axis_quality_score[0], t.axis_jitter_mms[0], t.axis_vfd_error_percent[0], 
        t.axis_stalled[0] ? "true" : "false", t.axis_maintenance_warning[0] ? "true" : "false",
        (uint32_t)t.axis_quality_score[1], t.axis_jitter_mms[1], t.axis_vfd_error_percent[1], 
        t.axis_stalled[1] ? "true" : "false", t.axis_maintenance_warning[1] ? "true" : "false",
        (uint32_t)t.axis_quality_score[2], t.axis_jitter_mms[2], t.axis_vfd_error_percent[2], 
        t.axis_stalled[2] ? "true" : "false", t.axis_maintenance_warning[2] ? "true" : "false",
        t.wifi_connected ? "true" : "false", t.wifi_signal_strength);
    if (n > 0 && (offset + n) < buffer_size) offset += n;

    n = snprintf(buffer + offset, buffer_size - offset,
        "\"sd\":{\"mounted\":%s,\"health\":%d,\"total_bytes\":%llu,\"used_bytes\":%llu},"
        "\"parser\":{\"absolute_mode\":%s,\"feedrate\":%.1f,\"actual_feedrate\":%.1f},"
        "\"lcd\":{\"lines\":[\"%s\",\"%s\",\"%s\",\"%s\"]}",
        t.sd_mounted ? "true" : "false", (int)t.sd_health, t.sd_total_bytes, t.sd_used_bytes,
        t.parser_absolute_mode ? "true" : "false", t.parser_req_feedrate, t.parser_actual_feedrate,
        t.lcd_lines[0], t.lcd_lines[1], t.lcd_lines[2], t.lcd_lines[3]);
    if (n > 0 && (offset + n) < buffer_size) offset += n;

    if (t.motion_moving) {
        n = snprintf(buffer + offset, buffer_size - offset,
            ",\"exec\":{\"cmd\":\"%s\",\"progress\":%.1f,\"eta\":%lu}",
            motionGetCurrentCommand(), motionGetExecutionProgress(), (unsigned long)motionGetEstimatedTimeRemaining());
        if (n > 0 && (offset + n) < buffer_size) offset += n;
    }

    n = snprintf(buffer + offset, buffer_size - offset, ",\"stack\":{");
    if (n > 0 && (offset + n) < buffer_size) offset += n;
    
    task_stats_t* stats = taskGetStatsArray();
    int count = taskGetStatsCount();
    bool first_stack = true;
    for (int i = 0; i < count; i++) {
        if (stats[i].handle) {
            n = snprintf(buffer + offset, buffer_size - offset, "%s\"%s\":%u", first_stack ? "" : ",", stats[i].name, stats[i].stack_high_water);
            if (n > 0 && (offset + n) < buffer_size) {
                offset += n;
                first_stack = false;
            }
            if (offset >= buffer_size - 16) break;
        }
    }
    
    if (offset < buffer_size - 2) {
        buffer[offset++] = '}'; // Close stack
        buffer[offset++] = '}'; // Close root
        buffer[offset] = '\0';
    }

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

size_t telemetryExportBinary(telemetry_packet_t* packet) {
    if (!packet) return 0;

    system_telemetry_t t = telemetryGetSnapshot();

    // PHASE 5.10: Atomic copy into packed structure
    packet->magic = 0xB155; // BISSO
    packet->version = 1;
    packet->health = (uint8_t)t.health_status;
    packet->uptime = t.uptime_seconds;
    packet->cpu_usage = t.cpu_usage_percent;
    packet->free_heap = t.free_heap_bytes;
    
    // Pack flags
    packet->flags = 0;
    if (t.motion_enabled)   packet->flags |= (1 << 0);
    if (t.motion_moving)    packet->flags |= (1 << 1);
    if (t.spindle_enabled)  packet->flags |= (1 << 2);
    if (t.spindle_overcurrent) packet->flags |= (1 << 3);
    if (t.spindle_fault)    packet->flags |= (1 << 4);
    if (t.wifi_connected)   packet->flags |= (1 << 5);
    if (t.estop_active)     packet->flags |= (1 << 6);
    if (t.alarm_active)     packet->flags |= (1 << 7);

    packet->flags2 = 0;
    if (t.plc_hardware_present) packet->flags2 |= (1 << 0);

    packet->axis_x = t.axis_x_mm;
    packet->axis_y = t.axis_y_mm;
    packet->axis_z = t.axis_z_mm;
    packet->axis_a = t.axis_a_mm;

    packet->spindle_amps = t.spindle_current_amps;
    packet->spindle_peak = t.spindle_current_peak_amps;

    packet->faults_logged = t.faults_logged;
    packet->critical_faults = t.critical_faults;
    
    packet->slowest_id = t.slowest_task_id;
    packet->slowest_us = t.slowest_task_time_us;
    
    packet->wifi_signal = t.wifi_signal_strength;

    return sizeof(telemetry_packet_t);
}

// Delta telemetry support - tracks previous values
static system_telemetry_t delta_baseline = {};  // C++ value-initialization
static bool delta_initialized = false;

size_t telemetryExportDeltaJSON(char* buffer, size_t buffer_size, bool full_update) {
    if (!buffer || buffer_size < 128) return 0;

    system_telemetry_t current = telemetryGetSnapshot();
    size_t offset = 0;
    bool first_field = true;

    // Start JSON object
    offset += snprintf(buffer + offset, buffer_size - offset, "{");

    // Reset baseline if requested or first call
    if (full_update || !delta_initialized) {
        delta_baseline = current;
        delta_initialized = true;
        // Return compact version for full update
        offset += snprintf(buffer + offset, buffer_size - offset,
            "\"full\":true,\"health\":\"%s\",\"uptime\":%lu,\"cpu\":%u,\"heap\":%lu,"
            "\"x_mm\":%.2f,\"y_mm\":%.2f,\"z_mm\":%.2f,\"a_mm\":%.2f,"
            "\"estop\":%s,\"moving\":%s,\"wifi\":%s}",
            telemetryGetHealthStatusString(current.health_status),
            (unsigned long)current.uptime_seconds,
            current.cpu_usage_percent,
            (unsigned long)current.free_heap_bytes,
            current.axis_x_mm, current.axis_y_mm, current.axis_z_mm, current.axis_a_mm,
            current.estop_active ? "true" : "false",
            current.motion_moving ? "true" : "false",
            current.wifi_connected ? "true" : "false");
        return offset;
    }

    // Helper macro to add changed field
    #define ADD_DELTA_INT(name, field) \
        if (current.field != delta_baseline.field) { \
            offset += snprintf(buffer + offset, buffer_size - offset, \
                "%s\"%s\":%ld", first_field ? "" : ",", name, (long)current.field); \
            first_field = false; \
        }

    #define ADD_DELTA_UINT(name, field) \
        if (current.field != delta_baseline.field) { \
            offset += snprintf(buffer + offset, buffer_size - offset, \
                "%s\"%s\":%lu", first_field ? "" : ",", name, (unsigned long)current.field); \
            first_field = false; \
        }

    #define ADD_DELTA_BOOL(name, field) \
        if (current.field != delta_baseline.field) { \
            offset += snprintf(buffer + offset, buffer_size - offset, \
                "%s\"%s\":%s", first_field ? "" : ",", name, current.field ? "true" : "false"); \
            first_field = false; \
        }

    #define ADD_DELTA_FLOAT(name, field, precision) \
        if (fabs(current.field - delta_baseline.field) > 0.01f) { \
            offset += snprintf(buffer + offset, buffer_size - offset, \
                "%s\"%s\":%." #precision "f", first_field ? "" : ",", name, current.field); \
            first_field = false; \
        }

    // Compare and add only changed fields
    if (current.health_status != delta_baseline.health_status) {
        offset += snprintf(buffer + offset, buffer_size - offset,
            "%s\"health\":\"%s\"", first_field ? "" : ",",
            telemetryGetHealthStatusString(current.health_status));
        first_field = false;
    }

    ADD_DELTA_UINT("uptime", uptime_seconds);
    ADD_DELTA_UINT("cpu", cpu_usage_percent);
    ADD_DELTA_UINT("heap", free_heap_bytes);
    ADD_DELTA_BOOL("moving", motion_moving);
    ADD_DELTA_BOOL("estop", estop_active);
    ADD_DELTA_BOOL("alarm", alarm_active);
    ADD_DELTA_BOOL("wifi", wifi_connected);
    ADD_DELTA_UINT("signal", wifi_signal_strength);
    ADD_DELTA_FLOAT("x_mm", axis_x_mm, 2);
    ADD_DELTA_FLOAT("y_mm", axis_y_mm, 2);
    ADD_DELTA_FLOAT("z_mm", axis_z_mm, 2);
    ADD_DELTA_FLOAT("a_mm", axis_a_mm, 2);
    ADD_DELTA_FLOAT("spindle_a", spindle_current_amps, 2);
    ADD_DELTA_UINT("faults", faults_logged);

    #undef ADD_DELTA_INT
    #undef ADD_DELTA_UINT
    #undef ADD_DELTA_BOOL
    #undef ADD_DELTA_FLOAT

    // Close JSON
    offset += snprintf(buffer + offset, buffer_size - offset, "}");

    // Update baseline for next call
    delta_baseline = current;

    return offset;
}


void telemetryPrintSummary() {
    system_telemetry_t t = telemetryGetSnapshot();

    serialLoggerLock();
    logPrintln("\r\n[TELEMETRY] === System Health Summary ===");
    logPrintf("Health: %s | Uptime: %lu sec | CPU: %u%% | Heap: %lu bytes\r\n",
        telemetryGetHealthStatusString(t.health_status),
        (unsigned long)t.uptime_seconds,
        t.cpu_usage_percent,
        (unsigned long)t.free_heap_bytes);

    logPrintf("Motion: %s (Moving: %s)\r\n",
        t.motion_enabled ? "Enabled" : "Disabled",
        t.motion_moving ? "Yes" : "No");

    logPrintf("Spindle: %s (Current: %.2f A, Peak: %.2f A)\r\n",
        t.spindle_enabled ? "Enabled" : "Disabled",
        t.spindle_current_amps,
        t.spindle_current_peak_amps);

    logPrintf("Safety: E-STOP %s, Alarm %s, Faults: %lu\r\n",
        t.estop_active ? "ACTIVE" : "OK",
        t.alarm_active ? "ACTIVE" : "OK",
        (unsigned long)t.faults_logged);

    logPrintf("Network: WiFi %s (Signal: %u%%)\r\n",
        t.wifi_connected ? "Connected" : "Disconnected",
        t.wifi_signal_strength);

    logPrintln("");
    serialLoggerUnlock();
}

void telemetryPrintDetailed() {
    system_telemetry_t t = telemetryGetSnapshot();

    serialLoggerLock();
    logPrintln("\r\n[TELEMETRY] === Detailed System Telemetry ===");

    logPrintln("=== SYSTEM ===");
    logPrintf("Health Status: %s\r\n", telemetryGetHealthStatusString(t.health_status));
    logPrintf("Uptime: %lu seconds\r\n", (unsigned long)t.uptime_seconds);
    logPrintf("Loop Cycles: %lu\r\n", (unsigned long)t.loop_cycle_count);

    logPrintln("\n=== CPU & MEMORY ===");
    logPrintf("CPU Usage: %u%%\r\n", t.cpu_usage_percent);
    logPrintf("Free Heap: %lu bytes\r\n", (unsigned long)t.free_heap_bytes);
    logPrintf("Stack Used: %lu bytes\r\n", (unsigned long)t.stack_used_bytes);

    logPrintln("\n=== MOTION ===");
    logPrintf("Enabled: %s | Moving: %s\r\n",
        t.motion_enabled ? "Yes" : "No",
        t.motion_moving ? "Yes" : "No");
    logPrintf("Position: X=%.2f Y=%.2f Z=%.2f A=%.2f mm\r\n",
        t.axis_x_mm, t.axis_y_mm, t.axis_z_mm, t.axis_a_mm);
    logPrintf("Steps Executed: %lu\r\n", (unsigned long)t.steps_executed);
    logPrintf("Motion Errors: %lu\r\n", (unsigned long)t.motion_errors);

    logPrintln("\n=== SPINDLE ===");
    logPrintf("Enabled: %s\r\n", t.spindle_enabled ? "Yes" : "No");
    logPrintf("Current: %.2f A (Peak: %.2f A)\r\n",
        t.spindle_current_amps, t.spindle_current_peak_amps);
    logPrintf("Errors: %lu | Overcurrent: %s | Fault: %s\r\n",
        (unsigned long)t.spindle_errors,
        t.spindle_overcurrent ? "Yes" : "No",
        t.spindle_fault ? "Yes" : "No");

    logPrintln("\n=== SAFETY ===");
    logPrintf("E-STOP: %s\r\n", t.estop_active ? "ACTIVE" : "OK");
    logPrintf("Alarm: %s\r\n", t.alarm_active ? "ACTIVE" : "OK");
    logPrintf("Faults Logged: %lu (Critical: %lu)\r\n",
        (unsigned long)t.faults_logged,
        (unsigned long)t.critical_faults);
    logPrintf("Safety Events: %lu\n", (unsigned long)t.safety_events);

    logPrintln("\n=== TASKS ===");
    logPrintf("Slowest Task: ID %u (%lu us)\n",
        t.slowest_task_id,
        (unsigned long)t.slowest_task_time_us);
    logPrintf("Task Underruns: %lu\n", (unsigned long)t.total_task_underruns);

    logPrintln("\n=== NETWORK ===");
    logPrintf("WiFi Connected: %s\n", t.wifi_connected ? "Yes" : "No");
    logPrintf("Signal Strength: %u%%\n", t.wifi_signal_strength);
    logPrintf("HTTP Requests: %lu | Errors: %lu\n",
        (unsigned long)t.http_requests_served,
        (unsigned long)t.http_errors);

    logPrintln("\n=== CONFIGURATION ===");
    logPrintf("Config Version: %lu\n", (unsigned long)t.config_version);
    logPrintf("Using Defaults: %s\n", t.config_is_default ? "Yes" : "No");
    logPrintf("Config Changes: %lu\n", (unsigned long)t.config_changes_count);
    logPrintf("Watchdog Resets: %lu\n", (unsigned long)t.watchdog_resets);

    logPrintln("");
    serialLoggerUnlock();
}

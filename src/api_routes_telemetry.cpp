/**
 * @file api_routes_telemetry.cpp
 * @brief Telemetry and Status API Routes
 * @details Handles /api/status, /api/spindle, /api/history/...
 */

#include "api_routes.h"
#include "system_telemetry.h"
#include "spindle_current_monitor.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "mcu_info.h"
#include "firmware_version.h"
#include <ArduinoJson.h>

// External from web_server.cpp
extern void updateHistory(uint8_t cpu, uint32_t heap, float spindle);

// Telemetry history buffer (shared with web_server.cpp)
#define HISTORY_BUFFER_SIZE 60

struct history_sample_t {
    uint8_t cpu;
    uint32_t heap;
    float spindle;
};

extern history_sample_t telemetry_history[HISTORY_BUFFER_SIZE];
extern int history_head;
extern int history_count;

void registerTelemetryRoutes(PsychicHttpServer& server) {
    
    // Handler for status/telemetry endpoints (OPTIMIZED: snprintf, no heap)
    auto statusHandler = [](PsychicRequest *request, PsychicResponse *response) {
        system_telemetry_t telemetry = telemetryGetSnapshot();
        
        char rev_str[16];
        mcuGetRevisionString(rev_str, sizeof(rev_str));

        char serial_str[32];
        uint64_t mac = ESP.getEfuseMac();
        snprintf(serial_str, sizeof(serial_str), "BS-E350-%02X%02X", (uint8_t)(mac >> 8), (uint8_t)mac);

        char buffer[2048];
        snprintf(buffer, sizeof(buffer),
            "{\"system\":{"
            "\"status\":\"READY\",\"health\":\"%s\",\"uptime_sec\":%lu,"
            "\"cpu_percent\":%d,\"free_heap_bytes\":%lu,\"plc_hardware_present\":%s,"
            "\"firmware_version\":\"v%d.%d.%d\",\"build_date\":\"%s\","
            "\"hw_model\":\"BISSO E350\",\"hw_mcu\":\"%s\",\"hw_revision\":\"%s\","
            "\"hw_serial\":\"%s\",\"hw_psram_size\":%zu,\"hw_flash_size\":%zu,"
            "\"hw_has_psram\":%s"
            "},"
            "\"x_mm\":%.3f,\"y_mm\":%.3f,\"z_mm\":%.3f,\"a_mm\":%.3f,"
            "\"motion_enabled\":%s,\"motion_moving\":%s,\"estop\":%s,\"alarm\":%s}",
            telemetryGetHealthStatusString(telemetry.health_status),
            (unsigned long)telemetry.uptime_seconds,
            telemetry.cpu_usage_percent,
            (unsigned long)telemetry.free_heap_bytes,
            telemetry.plc_hardware_present ? "true" : "false",
            FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH,
            __DATE__,
            mcuGetModelName(),
            rev_str,
            serial_str,
            mcuGetPsramSize(),
            mcuGetFlashSize(),
            mcuHasPsram() ? "true" : "false",
            telemetry.axis_x_mm,
            telemetry.axis_y_mm,
            telemetry.axis_z_mm,
            telemetry.axis_a_mm,
            telemetry.motion_enabled ? "true" : "false",
            telemetry.motion_moving ? "true" : "false",
            telemetry.estop_active ? "true" : "false",
            telemetry.alarm_active ? "true" : "false"
        );
        
        return response->send(200, "application/json", buffer);
    };

    // GET /api/status - System status and positions
    server.on("/api/status", HTTP_GET, statusHandler);
    
    // GET /api/telemetry - Alias for /api/status (backwards compatibility)
    server.on("/api/telemetry", HTTP_GET, statusHandler);
    
    // GET /api/spindle - Spindle monitor state (OPTIMIZED: snprintf, no heap)
    server.on("/api/spindle", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
            "{\"current_amps\":%.2f,\"peak_amps\":%.2f,\"threshold_amps\":%.2f,"
            "\"auto_pause_threshold\":%.2f,\"auto_pause_count\":%u,\"overcurrent\":%s}",
            state->current_amps,
            state->current_peak_amps,
            state->overcurrent_threshold_amps,
            state->auto_pause_threshold_amps,
            state->auto_pause_count,
            state->alarm_overload ? "true" : "false"
        );
        
        return response->send(200, "application/json", buffer);
    });
    
    // GET /api/spindle/alarm - Spindle alarm thresholds (OPTIMIZED: snprintf, no heap)
    server.on("/api/spindle/alarm", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        float toolbreak = configGetFloat(KEY_SPINDL_TOOLBREAK_THR, 5.0f);
        int stall_thr = configGetInt(KEY_SPINDL_PAUSE_THR, 25);
        int stall_timeout = configGetInt(KEY_STALL_TIMEOUT, 2000);
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
            "{\"success\":true,\"toolbreak_threshold\":%.2f,\"stall_threshold\":%d,"
            "\"stall_timeout_ms\":%d,\"alarm_tool_breakage\":%s,\"alarm_stall\":%s}",
            toolbreak, stall_thr, stall_timeout,
            state->alarm_overload ? "true" : "false",
            state->alarm_overload ? "true" : "false"
        );
        
        return response->send(200, "application/json", buffer);
    });
    
    // POST /api/spindle/alarm - Set spindle alarm thresholds
    server.on("/api/spindle/alarm", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        if (!doc["toolbreak_threshold"].isNull()) {
            configSetFloat(KEY_SPINDL_TOOLBREAK_THR, doc["toolbreak_threshold"]);
        }
        if (!doc["stall_threshold"].isNull()) {
            configSetInt(KEY_SPINDL_PAUSE_THR, doc["stall_threshold"]);
        }
        if (!doc["stall_timeout_ms"].isNull()) {
            configSetInt(KEY_STALL_TIMEOUT, doc["stall_timeout_ms"]);
        }
        
        configUnifiedSave();
        return response->send(200, "application/json", "{\"success\":true}");
    });
    
    // POST /api/spindle/alarm/clear - Clear spindle alarms
    server.on("/api/spindle/alarm/clear", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        spindleMonitorClearAlarms();
        return response->send(200, "application/json", "{\"success\":true}");
    });
    
    // GET /api/history/telemetry (OPTIMIZED: snprintf, no heap)
    server.on("/api/history/telemetry", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        char* buffer = (char*)malloc(4096);
        if (!buffer) return response->send(500, "application/json", "{\"error\":\"Out of memory\"}");
        
        char* p = buffer;
        size_t remain = 4096;
        int n;

        n = snprintf(p, remain, "{\"success\":true,\"cpu\":[");
        if (n > 0) { p += n; remain -= n; }

        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            n = snprintf(p, remain, "%d%s", telemetry_history[idx].cpu, (i < history_count - 1) ? "," : "");
            if (n > 0) { p += n; remain -= n; }
        }

        n = snprintf(p, remain, "],\"heap\":[");
        if (n > 0) { p += n; remain -= n; }

        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            n = snprintf(p, remain, "%lu%s", (unsigned long)telemetry_history[idx].heap, (i < history_count - 1) ? "," : "");
            if (n > 0) { p += n; remain -= n; }
        }

        n = snprintf(p, remain, "],\"spindle_amps\":[");
        if (n > 0) { p += n; remain -= n; }

        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            n = snprintf(p, remain, "%.2f%s", telemetry_history[idx].spindle, (i < history_count - 1) ? "," : "");
            if (n > 0) { p += n; remain -= n; }
        }

        snprintf(p, remain, "]}");
        
        esp_err_t res = response->send(200, "application/json", buffer);
        free(buffer);
        return res;
    });
    
    logDebug("[WEB] Telemetry routes registered");
}

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
    
    // Handler for status/telemetry endpoints (shared logic)
    auto statusHandler = [](PsychicRequest *request, PsychicResponse *response) {
        system_telemetry_t telemetry = telemetryGetSnapshot();
        JsonDocument doc;
        
        // Nested system object to match WebSocket structure (PHASE 6.5)
        JsonObject sys = doc["system"].to<JsonObject>();
        sys["status"] = "READY"; // Fallback, could be richer
        sys["health"] = telemetryGetHealthStatusString(telemetry.health_status);
        sys["uptime_sec"] = telemetry.uptime_seconds;
        sys["cpu_percent"] = telemetry.cpu_usage_percent;
        sys["free_heap_bytes"] = telemetry.free_heap_bytes;
        sys["plc_hardware_present"] = telemetry.plc_hardware_present;
        sys["firmware_version"] = "v" + String(FIRMWARE_VERSION_MAJOR) + "." + String(FIRMWARE_VERSION_MINOR) + "." + String(FIRMWARE_VERSION_PATCH);
        sys["build_date"] = __DATE__;

        // Hardware details
        sys["hw_model"] = "BISSO E350";
        sys["hw_mcu"] = mcuGetModelName();
        
        char rev_str[16];
        mcuGetRevisionString(rev_str, sizeof(rev_str));
        sys["hw_revision"] = rev_str;

        char serial_str[32];
        uint64_t mac = ESP.getEfuseMac();
        snprintf(serial_str, sizeof(serial_str), "BS-E350-%02X%02X", (uint8_t)(mac >> 8), (uint8_t)mac);
        sys["hw_serial"] = serial_str;
        
        // Memory info
        sys["hw_psram_size"] = mcuGetPsramSize();
        sys["hw_flash_size"] = mcuGetFlashSize();
        sys["hw_has_psram"] = mcuHasPsram();

        // Flattened fields for dashboard compatibility
        doc["x_mm"] = telemetry.axis_x_mm;
        doc["y_mm"] = telemetry.axis_y_mm;
        doc["z_mm"] = telemetry.axis_z_mm;
        doc["a_mm"] = telemetry.axis_a_mm;
        
        doc["motion_enabled"] = telemetry.motion_enabled;
        doc["motion_moving"] = telemetry.motion_moving;
        doc["estop"] = telemetry.estop_active;
        doc["alarm"] = telemetry.alarm_active;
        
        return sendJsonResponse(response, doc);
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
    
    // GET /api/history/telemetry - Historical telemetry data
    server.on("/api/history/telemetry", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        JsonDocument doc;
        doc["success"] = true;
        JsonArray cpu_arr = doc["cpu"].to<JsonArray>();
        JsonArray heap_arr = doc["heap"].to<JsonArray>();
        JsonArray spindle_arr = doc["spindle_amps"].to<JsonArray>();

        // Reconstruct chronological order from circular buffer
        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            cpu_arr.add(telemetry_history[idx].cpu);
            heap_arr.add(telemetry_history[idx].heap);
            spindle_arr.add(telemetry_history[idx].spindle);
        }

        return sendJsonResponse(response, doc);
    });
    
    logDebug("[WEB] Telemetry routes registered");
}

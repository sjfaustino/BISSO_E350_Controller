/**
 * @file api_routes_telemetry.cpp
 * @brief Telemetry and Status API Routes
 * @details Handles /api/status, /api/spindle, /api/history/...
 */

#include "api_routes.h"
#include "system_telemetry.h"
#include "telemetry_history.h"
#include "spindle_current_monitor.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "mcu_info.h"
#include "firmware_version.h"
#include "hardware_config.h"
#include "psram_alloc.h"
#include "memory_prealloc.h"
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

        if (!memoryLockStatusBuffer(100)) {
            return response->send(503, "application/json", "{\"error\":\"Resource busy\"}");
        }

        char* buffer = memoryGetStatusBuffer();
        if (buffer == nullptr) {
            memoryUnlockStatusBuffer();
            return response->send(500, "application/json", "{\"error\":\"Buffer not allocated\"}");
        }

        snprintf(buffer, API_STATUS_BUFFER_SIZE,
            "{\"system\":{"
            "\"status\":\"READY\",\"health\":\"%s\",\"uptime_sec\":%lu,"
            "\"cpu_percent\":%d,\"free_heap_bytes\":%lu,\"plc_hardware_present\":%s,"
            "\"firmware_version\":\"v%d.%d.%d\",\"build_date\":\"%s\","
            "\"hw_model\":\"%s\",\"hw_mcu\":\"%s\",\"hw_revision\":\"%s\","
            "\"hw_serial\":\"%s\",\"hw_psram_size\":%zu,\"hw_flash_size\":%zu,"
            "\"hw_has_psram\":%s,\"hw_has_rtc\":%s,\"hw_has_oled\":%s,\"hw_has_sd\":%s,"
            "\"hw_eth_chip\":\"%s\""
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
            getBoardName(),
            mcuGetModelName(),
            rev_str,
            serial_str,
            mcuGetPsramSize(),
            mcuGetFlashSize(),
            mcuHasPsram() ? "true" : "false",
            BOARD_HAS_RTC_DS3231 ? "true" : "false",
            BOARD_HAS_OLED_SSD1306 ? "true" : "false",
            BOARD_HAS_SDCARD ? "true" : "false",
            BOARD_HAS_W5500 ? "W5500 (SPI)" : "LAN8720A (RMII)",
            telemetry.axis_x_mm,
            telemetry.axis_y_mm,
            telemetry.axis_z_mm,
            telemetry.axis_a_mm,
            telemetry.motion_enabled ? "true" : "false",
            telemetry.motion_moving ? "true" : "false",
            telemetry.estop_active ? "true" : "false",
            telemetry.alarm_active ? "true" : "false"
        );
        
        response->setCode(200);
        response->setContentType("application/json");
        response->sendHeaders();
        response->sendChunk((uint8_t*)buffer, strlen(buffer));
        memoryUnlockStatusBuffer();
        return response->finishChunking();
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
    
    // GET /api/history/telemetry (legacy 5-min history)
    server.on("/api/history/telemetry", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        // ... (existing logic remains same for backwards compatibility)
        response->setContentType("application/json");
        response->sendHeaders(); 
        
        char chunk[512]; 
        char* p = chunk;
        size_t rem = sizeof(chunk);

        auto flushIfFull = [&](size_t needed) {
            if (needed >= rem) {
                response->sendChunk((uint8_t*)chunk, p - chunk);
                p = chunk;
                rem = sizeof(chunk);
            }
        };

        // 1. CPU Array
        response->sendChunk((uint8_t*)"{\"success\":true,\"cpu\":[", 23);
        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            char val[16];
            int n = snprintf(val, sizeof(val), "%d%s", telemetry_history[idx].cpu, (i < history_count - 1) ? "," : "");
            flushIfFull(n);
            memcpy(p, val, n);
            p += n; rem -= n;
        }
        response->sendChunk((uint8_t*)chunk, p - chunk);
        p = chunk; rem = sizeof(chunk);

        // 2. Heap Array
        response->sendChunk((uint8_t*)"],\"heap\":[", 10);
        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            char val[16];
            int n = snprintf(val, sizeof(val), "%lu%s", (unsigned long)telemetry_history[idx].heap, (i < history_count - 1) ? "," : "");
            flushIfFull(n);
            memcpy(p, val, n);
            p += n; rem -= n;
        }
        response->sendChunk((uint8_t*)chunk, p - chunk);
        p = chunk; rem = sizeof(chunk);

        // 3. Spindle Array + Footer
        response->sendChunk((uint8_t*)"],\"spindle_amps\":[", 18);
        for (int i = 0; i < history_count; i++) {
            int idx = (history_head - history_count + i + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
            char val[16];
            int n = snprintf(val, sizeof(val), "%.2f%s", telemetry_history[idx].spindle, (i < history_count - 1) ? "," : "");
            flushIfFull(n);
            memcpy(p, val, n);
            p += n; rem -= n;
        }
        response->sendChunk((uint8_t*)chunk, p - chunk);
        response->sendChunk((uint8_t*)"]}", 2);
        
        return response->finishChunking();
    });

    // NEW: GET /api/telemetry/history - 1-Hour High-Res History (JSON)
    server.on("/api/telemetry/history", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        uint16_t count = telemetryHistoryGetCount();
        if (count == 0) {
            return response->send(200, "application/json", "{\"success\":true,\"samples\":[]}");
        }

        // Use pre-allocated buffer for history (fragmentation fix)
        if (!memoryLockHistoryBuffer(500)) {
            return response->send(503, "application/json", "{\"error\":\"Resource busy\"}");
        }

        telemetry_packet_t* samples = (telemetry_packet_t*)memoryGetHistoryExportBuffer();
        if (samples == NULL) {
            memoryUnlockHistoryBuffer();
            return response->send(500, "application/json", "{\"error\":\"Buffer not allocated\"}");
        }

        telemetryHistoryGet(samples, &count);

        response->setContentType("application/json");
        response->sendHeaders();
        response->sendChunk((uint8_t*)"{\"success\":true,\"samples\":[", 27);

        // OPTIMIZATION: Use status buffer as a 2KB staging area to batch multiple samples
        // before calling sendChunk(). This reduces overhead significantly.
        char* staging = memoryGetStatusBuffer(); 
        bool staging_locked = memoryLockStatusBuffer(100);
        
        size_t offset = 0;
        for (uint16_t i = 0; i < count; i++) {
            char sample_buf[128];
            int n = snprintf(sample_buf, sizeof(sample_buf), 
                "{\"t\":%lu,\"cpu\":%u,\"heap\":%lu,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"spindle\":%.2f}%s",
                (unsigned long)samples[i].uptime, 
                samples[i].cpu_usage, 
                (unsigned long)samples[i].free_heap,
                samples[i].axis_x, samples[i].axis_y, samples[i].axis_z,
                samples[i].spindle_amps,
                (i < count - 1) ? "," : "");

            if (staging_locked && staging) {
                if (offset + n >= API_STATUS_BUFFER_SIZE - 1) {
                    response->sendChunk((uint8_t*)staging, offset);
                    offset = 0;
                }
                memcpy(staging + offset, sample_buf, n);
                offset += n;
            } else {
                // Fallback to single chunk if staging buffer busy
                response->sendChunk((uint8_t*)sample_buf, n);
            }
        }

        if (staging_locked && staging && offset > 0) {
            response->sendChunk((uint8_t*)staging, offset);
        }
        if (staging_locked) memoryUnlockStatusBuffer();

        response->sendChunk((uint8_t*)"]}", 2);
        memoryUnlockHistoryBuffer();
        return response->finishChunking();
    });

    // NEW: GET /api/telemetry/history/raw - Binary export for offline analysis
    server.on("/api/telemetry/history/raw", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        uint16_t count = telemetryHistoryGetCount();
        size_t total_size = count * sizeof(telemetry_packet_t);
        
        if (count == 0 || total_size == 0) {
            return response->send(404, "text/plain", "No history available");
        }

        if (!memoryLockHistoryBuffer(500)) {
            return response->send(503, "text/plain", "Resource busy");
        }

        telemetry_packet_t* samples = (telemetry_packet_t*)memoryGetHistoryExportBuffer();
        if (samples == NULL) {
             memoryUnlockHistoryBuffer();
             return response->send(500, "text/plain", "Buffer not allocated");
        }

        telemetryHistoryGet(samples, &count);
        
        response->setContentType("application/octet-stream");
        response->addHeader("Content-Disposition", "attachment; filename=\"telemetry.bin\"");
        response->setContent((const uint8_t*)samples, count * sizeof(telemetry_packet_t));
        response->send();
        
        memoryUnlockHistoryBuffer();
        return ESP_OK;
    });

    
    logDebug("[WEB] Telemetry routes registered");
}

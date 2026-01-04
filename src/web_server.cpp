/**
 * @file web_server.cpp
 * @brief Web Server Implementation with REST API
 * @details Implements static file serving and REST API for configuration.
 */

#include "web_server.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "api_config.h"
#include "serial_logger.h"
#include "hardware_config.h"
#include "plc_iface.h"
#include "board_inputs.h"
#include "gcode_parser.h"
#include "system_telemetry.h"
#include "fault_logging.h"
#include "spindle_current_monitor.h"
#include "encoder_wj66.h"

// Telemetry History Buffer (last 60 samples, sampled every 5s = 5mins)
#define HISTORY_BUFFER_SIZE 60
struct history_sample_t {
    uint8_t cpu;
    uint32_t heap;
    float spindle;
};
static history_sample_t telemetry_history[HISTORY_BUFFER_SIZE];
static int history_head = 0;
static int history_count = 0;
static uint32_t last_history_sample_ms = 0;

static void updateHistory(uint8_t cpu, uint32_t heap, float spindle) {
    uint32_t now = millis();
    if (now - last_history_sample_ms < 5000) return; // Sample every 5s
    last_history_sample_ms = now;

    telemetry_history[history_head] = {cpu, heap, spindle};
    history_head = (history_head + 1) % HISTORY_BUFFER_SIZE;
    if (history_count < HISTORY_BUFFER_SIZE) history_count++;
}

// Instantiate the global webServer object declared extern in web_server.h
WebServerManager webServer;

// Constructor
WebServerManager::WebServerManager(uint16_t port) : server(port), port(port) {
    // server(port) initializes the PsychicHttpServer with the port
}

// Destructor
WebServerManager::~WebServerManager() {
}

// Initialization
void WebServerManager::init() {
    logPrintln("[WEB] Init with REST API");
    
    // TUNE: Increase stack size and enable LRU purge for concurrency
    server.config.stack_size = 8192;
    server.config.max_open_sockets = 10; // Increase from default (4) to handle parallel browser requests
    server.config.lru_purge_enable = true;
    server.config.send_wait_timeout = 2; // Increase send timeout (default is often small)
    server.config.recv_wait_timeout = 2;

    // Mount Filesystem (format on first failure for new/corrupted flash)
    if (!LittleFS.begin(false)) {
        logPrintln("[WEB] LittleFS mount failed, formatting...");
        if (!LittleFS.format()) {
            logPrintln("[WEB] LittleFS format failed!");
            return;
        }
        if (!LittleFS.begin(false)) {
            logPrintln("[WEB] Failed to mount LittleFS after format");
            return;
        }
        logPrintln("[WEB] LittleFS formatted and mounted");
    }
    
    // --- API Routes (must be registered before static file serving) ---
    
    // GET /api/config/get?category=N
    server.on("/api/config/get", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        int category = 0;
        if (request->hasParam("category")) {
            category = request->getParam("category")->value().toInt();
        }
        
        JsonDocument doc;
        if (apiConfigGet((config_category_t)category, doc)) {
            String json;
            serializeJson(doc, json);
            return response->send(200, "application/json", json.c_str());
        }
        return response->send(400, "application/json", "{\"error\":\"Invalid category\"}");
    });
    
    // POST /api/config/set
    server.on("/api/config/set", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        int category = doc["category"] | 0;
        const char* key = doc["key"] | "";
        JsonVariant value = doc["value"];
        
        char error_msg[128] = {0};
        if (!apiConfigValidate((config_category_t)category, key, value, error_msg, sizeof(error_msg))) {
            JsonDocument resp;
            resp["error"] = error_msg;
            String json;
            serializeJson(resp, json);
            return response->send(400, "application/json", json.c_str());
        }
        
        if (apiConfigSet((config_category_t)category, key, value)) {
            apiConfigSave();
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(500, "application/json", "{\"error\":\"Failed to set config\"}");
    });
    
    // GET /api/network/status
    server.on("/api/network/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        JsonDocument doc;
        bool connected = WiFi.isConnected();
        doc["wifi_connected"] = connected;
        doc["wifi_ssid"] = connected ? WiFi.SSID() : "--";
        doc["wifi_ip"] = connected ? WiFi.localIP().toString() : "0.0.0.0";
        doc["wifi_rssi"] = connected ? WiFi.RSSI() : -100;
        doc["wifi_mac"] = WiFi.macAddress();
        doc["wifi_gateway"] = connected ? WiFi.gatewayIP().toString() : "0.0.0.0";
        doc["wifi_dns"] = connected ? WiFi.dnsIP().toString() : "0.0.0.0";
        
        // Calculate signal quality percentage (maps -100..-50 to 0..100)
        int rssi = WiFi.RSSI();
        int quality = 0;
        if (rssi <= -100) quality = 0;
        else if (rssi >= -50) quality = 100;
        else quality = 2 * (rssi + 100);
        doc["signal_quality"] = quality;
        
        // System uptime in ms
        doc["uptime_ms"] = millis();
        
        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // POST /api/network/reconnect
    server.on("/api/network/reconnect", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        WiFi.disconnect();
        WiFi.begin(); // Re-trigger connection using current credentials
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Reconnection triggered";
        
        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });
    
    // GET /api/spindle/alarm - stub for now
    server.on("/api/spindle/alarm", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        JsonDocument doc;
        doc["enabled"] = false;
        doc["threshold_amps"] = 0.0f;
        doc["alarm_active"] = false;
        
        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // GET /api/gcode/state
    server.on("/api/gcode/state", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        JsonDocument doc;
        doc["success"] = true;
        doc["absolute_mode"] = (gcodeParser.getDistanceMode() == G_MODE_ABSOLUTE);
        // doc["wcs"] = (54 + gcodeParser.currentWCS); // currentWCS is private, but getParserState uses it
        doc["feedrate"] = gcodeParser.getCurrentFeedRate();
        
        // Use getParserState to get the full grbl-style state string if needed
        char buffer[64];
        gcodeParser.getParserState(buffer, sizeof(buffer));
        doc["state_str"] = buffer;

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });
    
    // POST /api/encoder/calibrate
    server.on("/api/encoder/calibrate", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        uint8_t axis = doc["axis"] | 0;
        uint16_t ppm = doc["ppm"] | 0;
        
        if (apiConfigCalibrateEncoder(axis, ppm)) {
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"Calibration failed\"}");
    });

    // POST /api/hardware/wj66/baud
    server.on("/api/hardware/wj66/baud", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        
        uint32_t baud = doc["baud"] | 0;
        if (wj66SetBaud(baud)) {
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"Invalid baud rate\"}");
    });

    // POST /api/hardware/wj66/detect
    server.on("/api/hardware/wj66/detect", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        // Don't block the web server - spawn a background task
        xTaskCreate([](void* param) {
            logInfo("[WJ66] Autodetect task starting...");
            wj66Autodetect();
            logInfo("[WJ66] Autodetect task complete");
            vTaskDelete(NULL);
        }, "wj66_detect", 4096, NULL, 1, NULL);
        
        // Return immediately
        return response->send(200, "application/json", "{\"success\":true,\"message\":\"Detection started\"}");
    });

    // --- Time API ---
    server.on("/api/time", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

        JsonDocument doc;
        doc["timestamp"] = (uint32_t)now;
        doc["formatted"] = buf;
        doc["synced"] = (timeinfo.tm_year > (2020 - 1900));

        String output;
        serializeJson(doc, output);
        return response->send(200, "application/json", output.c_str());
    });

    server.on("/api/time/sync", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, request->body());
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }

        if (!doc["timestamp"].is<JsonVariant>()) {
            return response->send(400, "application/json", "{\"error\":\"Missing timestamp\"}");
        }

        uint32_t timestamp = doc["timestamp"];
        struct timeval tv;
        tv.tv_sec = timestamp;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        time_t now = timestamp;
        struct tm* timeinfo = localtime(&now);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);

        String out = String("{\"status\":\"success\",\"time\":\"") + buf + "\"}";
        return response->send(200, "application/json", out.c_str());
    });

    // --- Diagnostics & Monitoring API ---

    // GET /api/io/status
    server.on("/api/io/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        doc["success"] = true;
        
        uint8_t in_bits = elboI73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        
        // Map fields expected by diagnostics.js
        doc["estop"] = (board_in & 0x08) != 0; 
        doc["door"] = (board_in & 0x10) != 0;
        doc["probe"] = (board_in & 0x20) != 0;
        doc["limit_x"] = (board_in & 0x01) != 0;
        doc["limit_y"] = (board_in & 0x02) != 0;
        doc["limit_z"] = (board_in & 0x04) != 0;
        
        doc["spindle_on"] = (out_bits & 0x01) == 0; // Active-low
        doc["coolant_on"] = (out_bits & 0x02) == 0;
        doc["vacuum_on"] = (out_bits & 0x04) == 0;
        doc["alarm_on"] = (out_bits & 0x80) == 0;

        // Raw values for power users
        doc["raw_in"] = in_bits;
        doc["raw_out"] = out_bits;
        doc["raw_board"] = board_in;

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // GET /api/faults
    server.on("/api/faults", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        uint8_t count = faultGetRingBufferEntryCount();
        JsonArray faults = doc["faults"].to<JsonArray>();

        for (uint8_t i = 0; i < count; i++) {
            const fault_entry_t* entry = faultGetRingBufferEntry(i);
            if (entry) {
                JsonObject f = faults.add<JsonObject>();
                f["code"] = entry->code;
                f["description"] = faultCodeToString(entry->code);
                f["severity"] = faultSeverityToString(entry->severity);
                f["timestamp"] = entry->timestamp;
            }
        }

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // POST /api/faults/clear
    server.on("/api/faults/clear", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        faultClearHistory();
        return response->send(200, "application/json", "{\"success\":true}");
    });

    // GET /api/spindle
    server.on("/api/spindle", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        JsonDocument doc;
        doc["current_amps"] = state->current_amps;
        doc["peak_amps"] = state->current_peak_amps;
        doc["threshold_amps"] = state->overcurrent_threshold_amps;
        doc["auto_pause_threshold"] = state->auto_pause_threshold_amps;
        doc["auto_pause_count"] = state->auto_pause_count;
        doc["overcurrent"] = state->alarm_overload;

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // GET /api/history/telemetry
    server.on("/api/history/telemetry", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
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

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // --- Hardware API Routes ---

    // GET /api/hardware/pins
    server.on("/api/hardware/pins", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        
        // Populate pins array (GPIOs)
        JsonArray pinsArray = doc["pins"].to<JsonArray>();
        for (size_t i = 0; i < PIN_COUNT; i++) {
            JsonObject p = pinsArray.add<JsonObject>();
            p["gpio"] = pinDatabase[i].gpio;
            p["silk"] = pinDatabase[i].silk;
            p["type"] = pinDatabase[i].type;
            p["note"] = pinDatabase[i].note;
        }

        // Populate signals array (Mappings)
        JsonArray sigsArray = doc["signals"].to<JsonArray>();
        for (size_t i = 0; i < SIGNAL_COUNT; i++) {
            JsonObject s = sigsArray.add<JsonObject>();
            s["key"] = signalDefinitions[i].key;
            s["name"] = signalDefinitions[i].name;
            s["type"] = signalDefinitions[i].type;
            s["current_pin"] = getPin(signalDefinitions[i].key);
            s["default_pin"] = signalDefinitions[i].default_gpio;
        }

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // POST /api/hardware/pins
    server.on("/api/hardware/pins", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, request->body());
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }

        JsonObject assignments = doc.as<JsonObject>();
        bool all_ok = true;
        int count = 0;
        for (JsonPair kv : assignments) {
            // Pass skip_save=true to defer NVS flush
            if (!setPin(kv.key().c_str(), kv.value().as<int8_t>(), true)) {
                all_ok = false;
            }
            count++;
        }

        // Single flush for all pin changes
        configUnifiedSave();
        logInfo("[WEB] Batch pin save: %d pins", count);

        if (all_ok) {
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"One or more assignments failed\"}");
    });

    // GET /api/hardware/io
    server.on("/api/hardware/io", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        doc["success"] = true;
        
        uint8_t in_bits = elboI73GetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        
        // I73/Board Inputs (16 total)
        JsonArray inputs = doc["inputs"].to<JsonArray>();
        for (int i = 0; i < 8; i++) {
            JsonObject in = inputs.add<JsonObject>();
            in["state"] = (in_bits & (1 << i)) != 0;
            in["name"] = String("I73-") + i;
        }
        for (int i = 0; i < 8; i++) {
            JsonObject in = inputs.add<JsonObject>();
            in["state"] = (board_in & (1 << i)) != 0;
            in["name"] = String("B-X") + (i+1);
        }

        // Q73 Outputs (8 total)
        JsonArray outputs = doc["outputs"].to<JsonArray>();
        for (int i = 0; i < 8; i++) {
            JsonObject out = outputs.add<JsonObject>();
            out["state"] = (out_bits & (1 << i)) == 0; // Active-low
            out["name"] = String("Y") + (i+1);
        }

        doc["estop"] = (board_in & 0x08) != 0; // Typical E-stop mask

        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    // POST /api/hardware/pins/reset
    server.on("/api/hardware/pins/reset", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        // Reset pin mappings to defaults by clearing NVS keys
        for (size_t i = 0; i < SIGNAL_COUNT; i++) {
            char nvs_key[40];
            snprintf(nvs_key, sizeof(nvs_key), "pin_%s", signalDefinitions[i].key);
            // We don't have a direct configDelete, so we set to -1
            configSetInt(nvs_key, -1);
        }
        configUnifiedSave();
        return response->send(200, "application/json", "{\"success\":true}");
    });

    // --- Config API Aliases (for hardware.js compatibility) ---
    // These maps /api/config (GET/POST) to the existing category-based logic
    
    server.on("/api/config", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        // Merge common categories for the hardware page
        apiConfigGet(CONFIG_CATEGORY_MOTION, doc);
        apiConfigGet(CONFIG_CATEGORY_VFD, doc);
        apiConfigGet(CONFIG_CATEGORY_ENCODER, doc);
        
        String json;
        serializeJson(doc, json);
        return response->send(200, "application/json", json.c_str());
    });

    server.on("/api/config", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        deserializeJson(doc, request->body());
        
        if (doc["key"].is<const char*>() && doc["value"].is<const char*>()) {
            const char* key = doc["key"];
            const char* value = doc["value"];
            
            // Simple string-to-typed set
            if (strchr(value, '.')) configSetFloat(key, atof(value));
            else configSetInt(key, atoi(value));
            
            configUnifiedSave();
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"Missing key/value\"}");
    });

    // Batch config save - reduces NVS wear by setting all values then flushing once
    server.on("/api/config/batch", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, request->body());
        if (err) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        int count = 0;
        JsonObject obj = doc.as<JsonObject>();
        for (JsonPair kv : obj) {
            const char* key = kv.key().c_str();
            JsonVariant val = kv.value();
            
            if (val.is<int>() || val.is<long>()) {
                configSetInt(key, val.as<int32_t>());
            } else if (val.is<float>() || val.is<double>()) {
                configSetFloat(key, val.as<float>());
            } else if (val.is<const char*>()) {
                const char* strVal = val.as<const char*>();
                if (strchr(strVal, '.')) configSetFloat(key, atof(strVal));
                else configSetInt(key, atoi(strVal));
            }
            count++;
        }
        
        // Single flush for all changes
        configUnifiedSave();
        logInfo("[WEB] Batch config saved %d keys", count);
        
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"count\":%d}", count);
        return response->send(200, "application/json", resp);
    });

    logPrintln("[WEB] Hardware API routes registered");
    
    // --- WebSocket Handler for real-time telemetry ---
    wsHandler.onOpen([this](PsychicWebSocketClient *client) {
        logPrintf("[WS] Client connected: %s\n", client->remoteIP().toString().c_str());
    });
    
    wsHandler.onClose([this](PsychicWebSocketClient *client) {
        logPrintf("[WS] Client disconnected: %s\n", client->remoteIP().toString().c_str());
    });
    
    wsHandler.onFrame([this](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        // Handle incoming messages (commands from web UI)
        if (frame->type == HTTPD_WS_TYPE_TEXT) {
            String msg = String((char*)frame->payload, frame->len);
            // Filter out heartbeat pings to keep logs clean
            if (msg.indexOf("ping") == -1) {
                logPrintf("[WS] Received: %s\n", msg.c_str());
            }
            // Could parse commands here if needed
        }
        return ESP_OK;
    });
    
    server.on("/ws", &wsHandler);
    logPrintln("[WEB] WebSocket handler registered at /ws");
    
    // Serve static files from root (MUST be after API routes)
    // PHASE 6: Enable browser caching to reduce load on LittleFS
    server.serveStatic("/", LittleFS, "/", "public, max-age=60");
    
    // Initialize status cache
    memset(&current_status, 0, sizeof(current_status));
    strncpy(current_status.status, "IDLE", sizeof(current_status.status));
}

void WebServerManager::begin() {
    logPrintln("[WEB] Starting Server");
    
    // PHASE 6: Performance tuning for concurrent browser requests
    // Reduced to 2 for heap stability (preventing OOM when serving large files)
    // Browser will queue additional requests; prevents ESP32 crash
    server.config.max_open_sockets = 2;
    server.config.max_uri_handlers = 40;
    
    server.start(); 
}

// --- Telemetry Setters - Update internal state ---

void WebServerManager::setSystemStatus(const char* status) {
    portENTER_CRITICAL(&statusSpinlock);
    strncpy(current_status.status, status, sizeof(current_status.status) - 1);
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setAxisPosition(char axis, float position) {
    portENTER_CRITICAL(&statusSpinlock);
    switch (axis) {
        case 'X': case 'x': current_status.x_pos = position; break;
        case 'Y': case 'y': current_status.y_pos = position; break;
        case 'Z': case 'z': current_status.z_pos = position; break;
        case 'A': case 'a': current_status.a_pos = position; break;
    }
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setSystemUptime(uint32_t seconds) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.uptime_sec = seconds;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDCurrent(float current_amps) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_current_amps = current_amps;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDFrequency(float frequency_hz) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_frequency_hz = frequency_hz;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDThermalState(int16_t thermal_percent) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_thermal_percent = thermal_percent;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDFaultCode(uint16_t fault_code) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_fault_code = fault_code;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDCalibrationThreshold(float threshold_amps) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_threshold_amps = threshold_amps;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDCalibrationValid(bool is_valid) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_calibration_valid = is_valid;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setVFDConnected(bool is_connected) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.vfd_connected = is_connected;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setDROConnected(bool is_connected) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.dro_connected = is_connected;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setSpindleRPM(float rpm) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.spindle_rpm = rpm;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setSpindleSpeed(float speed_m_s) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.spindle_speed_m_s = speed_m_s;
    portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setAxisQualityScore(uint8_t axis, uint32_t quality_score) {
    if (axis < 3) {
        portENTER_CRITICAL(&statusSpinlock);
        current_status.axis_metrics[axis].quality_score = quality_score;
        portEXIT_CRITICAL(&statusSpinlock);
    }
}

void WebServerManager::setAxisJitterAmplitude(uint8_t axis, float jitter_mms) {
    if (axis < 3) {
        portENTER_CRITICAL(&statusSpinlock);
        current_status.axis_metrics[axis].jitter_mms = jitter_mms;
        portEXIT_CRITICAL(&statusSpinlock);
    }
}

void WebServerManager::setAxisStalled(uint8_t axis, bool is_stalled) {
    if (axis < 3) {
        portENTER_CRITICAL(&statusSpinlock);
        current_status.axis_metrics[axis].stalled = is_stalled;
        portEXIT_CRITICAL(&statusSpinlock);
    }
}

void WebServerManager::setAxisVFDError(uint8_t axis, float error_percent) {
    if (axis < 3) {
        portENTER_CRITICAL(&statusSpinlock);
        current_status.axis_metrics[axis].vfd_error_percent = error_percent;
        portEXIT_CRITICAL(&statusSpinlock);
    }
}

// --- Broadcast state to all connected WebSocket clients ---
void WebServerManager::broadcastState() {
    // Build JSON telemetry payload
    JsonDocument doc;
    
    system_telemetry_t telemetry = telemetryGetSnapshot();
    
    portENTER_CRITICAL(&statusSpinlock);
    doc["system"]["status"] = current_status.status;
    doc["system"]["health"] = telemetryGetHealthStatusString(telemetry.health_status);
    doc["system"]["uptime_seconds"] = current_status.uptime_sec;
    doc["system"]["cpu_percent"] = telemetry.cpu_usage_percent;
    doc["system"]["free_heap_bytes"] = telemetry.free_heap_bytes;
    doc["system"]["temperature"] = telemetry.temperature;
    doc["system"]["plc_hardware_present"] = telemetry.plc_hardware_present;
    
    doc["motion"]["position"]["x"] = current_status.x_pos;
    doc["motion"]["position"]["y"] = current_status.y_pos;
    doc["motion"]["position"]["z"] = current_status.z_pos;
    doc["motion"]["position"]["a"] = current_status.a_pos;
    doc["motion"]["dro_connected"] = current_status.dro_connected;
    
    doc["vfd"]["current_amps"] = current_status.vfd_current_amps;
    doc["vfd"]["frequency_hz"] = current_status.vfd_frequency_hz;
    doc["vfd"]["thermal_percent"] = current_status.vfd_thermal_percent;
    doc["vfd"]["fault_code"] = current_status.vfd_fault_code;
    doc["vfd"]["stall_threshold"] = current_status.vfd_threshold_amps;
    doc["vfd"]["calibration_valid"] = current_status.vfd_calibration_valid;
    doc["vfd"]["connected"] = current_status.vfd_connected;
    doc["vfd"]["rpm"] = current_status.spindle_rpm;
    doc["vfd"]["speed_m_s"] = current_status.spindle_speed_m_s;
    
    // Axis Metrics
    for (int i = 0; i < 3; i++) {
        const char* axis_names[] = {"x", "y", "z"};
        doc["axis"][axis_names[i]]["quality"] = current_status.axis_metrics[i].quality_score;
        doc["axis"][axis_names[i]]["jitter_mms"] = current_status.axis_metrics[i].jitter_mms;
        doc["axis"][axis_names[i]]["vfd_error_percent"] = current_status.axis_metrics[i].vfd_error_percent;
        doc["axis"][axis_names[i]]["stalled"] = (current_status.axis_metrics[i].quality_score < 10); // Simple stall heuristic for UI
    }
    
    // Network Status
    doc["network"]["wifi_connected"] = telemetry.wifi_connected;
    doc["network"]["signal_percent"] = telemetry.wifi_signal_strength;
    doc["network"]["latency"] = 0; // Placeholder for future ping/roundtrip tracking
    
    portEXIT_CRITICAL(&statusSpinlock);
    
    String json;
    serializeJson(doc, json);
    
    // Send to all connected WebSocket clients
    wsHandler.sendAll(json.c_str());

    // Update history tracking
    updateHistory(telemetry.cpu_usage_percent, telemetry.free_heap_bytes, current_status.vfd_current_amps);
}

// --- Credentials Stubs ---
void WebServerManager::loadCredentials() {}
void WebServerManager::setPassword(const char* new_password) {}
bool WebServerManager::isPasswordChangeRequired() { return false; }

// --- Legacy Support ---
void WebServerManager::handleClient() {}

// --- WebSocket Handler Accessor ---
// getWebSocketHandler is defined inline in web_server.h
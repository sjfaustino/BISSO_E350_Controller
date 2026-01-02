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
    Serial.println("[WEB] Init with REST API");
    
    // TUNE: Increase stack size and enable LRU purge for concurrency
    server.config.stack_size = 8192;
    server.config.lru_purge_enable = true;

    // Mount Filesystem (format on first failure for new/corrupted flash)
    if (!LittleFS.begin(false)) {
        Serial.println("[WEB] LittleFS mount failed, formatting...");
        if (!LittleFS.format()) {
            Serial.println("[WEB] LittleFS format failed!");
            return;
        }
        if (!LittleFS.begin(false)) {
            Serial.println("[WEB] Failed to mount LittleFS after format");
            return;
        }
        Serial.println("[WEB] LittleFS formatted and mounted");
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
        doc["wifi_connected"] = WiFi.isConnected();
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["mac"] = WiFi.macAddress();
        
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
        for (JsonPair kv : assignments) {
            if (!setPin(kv.key().c_str(), kv.value().as<int8_t>())) {
                all_ok = false;
            }
        }

        if (all_ok) {
            configUnifiedSave();
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

    Serial.println("[WEB] Hardware API routes registered");
    
    // --- WebSocket Handler for real-time telemetry ---
    wsHandler.onOpen([this](PsychicWebSocketClient *client) {
        Serial.printf("[WS] Client connected: %s\n", client->remoteIP().toString().c_str());
    });
    
    wsHandler.onClose([this](PsychicWebSocketClient *client) {
        Serial.printf("[WS] Client disconnected: %s\n", client->remoteIP().toString().c_str());
    });
    
    wsHandler.onFrame([this](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        // Handle incoming messages (commands from web UI)
        if (frame->type == HTTPD_WS_TYPE_TEXT) {
            String msg = String((char*)frame->payload, frame->len);
            Serial.printf("[WS] Received: %s\n", msg.c_str());
            // Could parse commands here if needed
        }
        return ESP_OK;
    });
    
    server.on("/ws", &wsHandler);
    Serial.println("[WEB] WebSocket handler registered at /ws");
    
    // Serve static files from root (MUST be after API routes)
    server.serveStatic("/", LittleFS, "/", "no-store, max-age=0");
    
    // Initialize status cache
    memset(&current_status, 0, sizeof(current_status));
    strncpy(current_status.status, "IDLE", sizeof(current_status.status));
}

void WebServerManager::begin() {
    Serial.println("[WEB] Starting Server");
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
    
    portENTER_CRITICAL(&statusSpinlock);
    doc["system"]["status"] = current_status.status;
    doc["system"]["uptime_seconds"] = current_status.uptime_sec;
    doc["system"]["cpu_percent"] = 0;  // TODO: Implement CPU usage tracking
    doc["system"]["free_heap_bytes"] = ESP.getFreeHeap();
    
    doc["motion"]["position"]["x"] = current_status.x_pos;
    doc["motion"]["position"]["y"] = current_status.y_pos;
    doc["motion"]["position"]["z"] = current_status.z_pos;
    doc["motion"]["position"]["a"] = current_status.a_pos;
    
    doc["vfd"]["current_amps"] = current_status.vfd_current_amps;
    doc["vfd"]["frequency_hz"] = current_status.vfd_frequency_hz;
    doc["vfd"]["thermal_percent"] = current_status.vfd_thermal_percent;
    doc["vfd"]["fault_code"] = current_status.vfd_fault_code;
    doc["vfd"]["stall_threshold"] = current_status.vfd_threshold_amps;
    doc["vfd"]["calibration_valid"] = current_status.vfd_calibration_valid;
    portEXIT_CRITICAL(&statusSpinlock);
    
    String json;
    serializeJson(doc, json);
    
    // Send to all connected WebSocket clients
    wsHandler.sendAll(json.c_str());
}

// --- Credentials Stubs ---
void WebServerManager::loadCredentials() {}
void WebServerManager::setPassword(const char* new_password) {}
bool WebServerManager::isPasswordChangeRequired() { return false; }

// --- Legacy Support ---
void WebServerManager::handleClient() {}

// --- WebSocket Handler Accessor ---
// getWebSocketHandler is defined inline in web_server.h
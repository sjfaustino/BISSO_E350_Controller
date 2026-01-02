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
    
    Serial.println("[WEB] API routes registered");
    
    // Serve static files from root (MUST be after API routes)
    server.serveStatic("/", LittleFS, "/", "no-store, max-age=0");
}

void WebServerManager::begin() {
    Serial.println("[WEB] Starting Server");
    server.start(); 
}

// --- Stubs for Telemetry (No-op in minimal mode) ---

void WebServerManager::setSystemStatus(const char* status) {}
void WebServerManager::setAxisPosition(char axis, float position) {}
void WebServerManager::setSystemUptime(uint32_t seconds) {}
void WebServerManager::setVFDCurrent(float current_amps) {}
void WebServerManager::setVFDFrequency(float frequency_hz) {}
void WebServerManager::setVFDThermalState(int16_t thermal_percent) {}
void WebServerManager::setVFDFaultCode(uint16_t fault_code) {}
void WebServerManager::setVFDCalibrationThreshold(float threshold_amps) {}
void WebServerManager::setVFDCalibrationValid(bool is_valid) {}
void WebServerManager::setAxisQualityScore(uint8_t axis, uint32_t quality_score) {}
void WebServerManager::setAxisJitterAmplitude(uint8_t axis, float jitter_mms) {}
void WebServerManager::setAxisStalled(uint8_t axis, bool is_stalled) {}
void WebServerManager::setAxisVFDError(uint8_t axis, float error_percent) {}
void WebServerManager::broadcastState() {}

// --- Credentials Stubs ---
void WebServerManager::loadCredentials() {}
void WebServerManager::setPassword(const char* new_password) {}
bool WebServerManager::isPasswordChangeRequired() { return false; }

// --- Legacy Support ---
void WebServerManager::handleClient() {}

// --- WebSocket Stubs ---
// getWebSocketHandler is defined inline in web_server.h
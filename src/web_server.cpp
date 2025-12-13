/**
 * @file web_server.cpp
 * @brief High-Level Web Router and Telemetry Service (Gemini v3.1.0)
 * @details Handles HTTP routing, WebSockets, and delegates File I/O to api_file_manager.
 * @project Gemini v3.1.0
 * @author Sergio Faustino
 */

#include "web_server.h"
#include "motion.h"
#include "motion_state.h"     // Read-only state access
#include "api_file_manager.h" // Delegated file handling
#include "spindle_current_monitor.h"  // PHASE 5.1: Spindle telemetry
#include "config_unified.h"   // NVS configuration
#include "config_keys.h"      // Configuration keys
#include "string_safety.h"    // Safe string operations
#include "api_rate_limiter.h"  // PHASE 5.1: Rate limiting
#include "task_performance_monitor.h"  // PHASE 5.1: Task performance metrics
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Global instance
WebServerManager webServer(80);

// Credentials loaded from NVS (PHASE 5.1: Security hardening)
static char http_username[CONFIG_VALUE_LEN] = "admin";
static char http_password[CONFIG_VALUE_LEN] = "password";
static bool password_change_enforced = false;

// Telemetry Buffer
static char json_response_buffer[WEB_BUFFER_SIZE];

WebServerManager::WebServerManager(uint16_t port) : server(nullptr), ws(nullptr), port(port) {
    memset(&current_status, 0, sizeof(current_status));
    safe_strcpy(current_status.status, sizeof(current_status.status), "INITIALIZING");
    // Initialize default safe positions if needed
    current_status.z_pos = 0.0f;
}

WebServerManager::~WebServerManager() {
    if (server) delete server;
    if (ws) delete ws;
}

// Load credentials from NVS (PHASE 5.1: Security hardening)
void WebServerManager::loadCredentials() {
    const char* user = configGetString(KEY_WEB_USERNAME, "admin");
    const char* pass = configGetString(KEY_WEB_PASSWORD, "password");
    int pw_changed = configGetInt(KEY_WEB_PW_CHANGED, 0);

    strncpy(http_username, user, CONFIG_VALUE_LEN - 1);
    http_username[CONFIG_VALUE_LEN - 1] = '\0';
    strncpy(http_password, pass, CONFIG_VALUE_LEN - 1);
    http_password[CONFIG_VALUE_LEN - 1] = '\0';

    if (pw_changed == 0) {
        Serial.println("[WEB] [WARN] Default credentials detected - password change required!");
        password_change_enforced = true;
    } else {
        Serial.println("[WEB] [OK] Credentials loaded from NVS");
        password_change_enforced = false;
    }
}

// Check if password has been changed from default
bool WebServerManager::isPasswordChangeRequired() {
    return password_change_enforced;
}

// Set new password (for CLI command)
void WebServerManager::setPassword(const char* new_password) {
    if (!new_password || strlen(new_password) < 4) {
        Serial.println("[WEB] [ERR] Password must be at least 4 characters");
        return;
    }

    configSetString(KEY_WEB_PASSWORD, new_password);
    configSetInt(KEY_WEB_PW_CHANGED, 1);
    configUnifiedSave();

    strncpy(http_password, new_password, CONFIG_VALUE_LEN - 1);
    http_password[CONFIG_VALUE_LEN - 1] = '\0';
    password_change_enforced = false;

    Serial.println("[WEB] [OK] Password changed successfully");
}

void WebServerManager::init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[WEB] [FAIL] SPIFFS Mount Failed");
        return;
    }
    Serial.println("[WEB] [OK] SPIFFS mounted");

    // Load credentials from NVS (PHASE 5.1)
    loadCredentials();

    // PHASE 5.1: Initialize API rate limiting
    apiRateLimiterInit();

    server = new AsyncWebServer(port);
    ws = new AsyncWebSocket("/ws");

    // WebSocket Event Handler
    ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });
    server->addHandler(ws);

    setupRoutes();

    Serial.println("[WEB] [OK] Async Server initialized");
}

void WebServerManager::begin() {
    if (server) {
        server->begin();
        Serial.println("[WEB] [OK] Started");
    }
}

void WebServerManager::handleClient() { 
    // No-op for AsyncWebServer, kept for compatibility
}

void WebServerManager::setupRoutes() {
    // 1. Static Files (Protected)
    server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setAuthentication(http_username, http_password);

    // 2. API Status (Protected, Rate Limited)
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.1: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        JsonDocument doc;
        // Use the new read-only accessors for thread safety
        doc["status"] = motionStateToString(motionGetState(0));
        doc["x_pos"] = motionGetPositionMM(0);
        doc["y_pos"] = motionGetPositionMM(1);
        doc["z_pos"] = motionGetPositionMM(2);
        doc["a_pos"] = motionGetPositionMM(3);

        // Use cached uptime/status from the Monitor Task updates
        doc["uptime"] = current_status.uptime_sec;
        // Or if real-time needed: doc["uptime"] = taskGetUptime();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 3. API Jog (Protected, Rate Limited)
    server->on("/api/jog", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            this->handleJogBody(request, data, len, index, total);
        }
    ).setAuthentication(http_username, http_password);

    // 4. API Spindle Telemetry (Protected, Rate Limited) - PHASE 5.1
    server->on("/api/spindle", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.1: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_SPINDLE, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        JsonDocument doc;

        // Get spindle monitor state
        const spindle_monitor_state_t* spindle_state = spindleMonitorGetState();

        if (spindle_state) {
            doc["enabled"] = spindle_state->enabled;
            doc["current_amps"] = spindle_state->current_amps;
            doc["peak_amps"] = spindle_state->current_peak_amps;
            doc["average_amps"] = spindle_state->current_average_amps;
            doc["threshold_amps"] = spindle_state->overcurrent_threshold_amps;
            doc["poll_interval_ms"] = spindle_state->poll_interval_ms;
            doc["read_count"] = spindle_state->read_count;
            doc["error_count"] = spindle_state->error_count;
            doc["overload_count"] = spindle_state->overload_count;
            doc["shutdown_count"] = spindle_state->shutdown_count;
            doc["jxk10_address"] = spindle_state->jxk10_slave_address;
            doc["jxk10_baud"] = spindle_state->jxk10_baud_rate;

            // JXK-10 device status
            if (spindleMonitorIsOverload()) {
                doc["jxk10_status"] = "OVERLOAD";
            } else if (spindleMonitorIsFault()) {
                doc["jxk10_status"] = "FAULT";
            } else {
                doc["jxk10_status"] = "OK";
            }

            doc["overcurrent"] = spindleMonitorIsOvercurrent();
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 5. API Task Performance Metrics (Protected, Rate Limited) - PHASE 5.1
    server->on("/api/metrics", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.1: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        // Export performance metrics as JSON
        char* metrics_buffer = (char*)malloc(2048);
        if (!metrics_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t metrics_size = perfMonitorExportJSON(metrics_buffer, 2048);
        if (metrics_size == 0) {
            free(metrics_buffer);
            request->send(500, "application/json", "{\"error\":\"Failed to export metrics\"}");
            return;
        }

        request->send(200, "application/json", metrics_buffer);
        free(metrics_buffer);
    });

    // 6. DELEGATE FILE MANAGEMENT
    // Registers /api/files (GET, DELETE) and /api/upload (POST)
    apiRegisterFileRoutes(server, http_username, http_password);

    server->onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not Found");
    });
}

// --- Handlers ---

void WebServerManager::handleJogBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_JOG, 0)) {
        request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        Serial.printf("[WEB] [ERR] JSON parse failed: %s\n", error.c_str());
        return;
    }

    const char* direction = doc["direction"];
    float distance = doc["distance"] | 10.0f;
    float speed = doc["speed"] | 50.0f;

    if (!direction) return;

    Serial.printf("[WEB] Jog: %s, %.1f mm, %.1f mm/s\n", direction, distance, speed);
    
    // Map direction strings to Motion API calls
    if (strcmp(direction, "X+") == 0) motionMoveRelative(distance, 0, 0, 0, speed);
    else if (strcmp(direction, "X-") == 0) motionMoveRelative(-distance, 0, 0, 0, speed);
    else if (strcmp(direction, "Y+") == 0) motionMoveRelative(0, distance, 0, 0, speed);
    else if (strcmp(direction, "Y-") == 0) motionMoveRelative(0, -distance, 0, 0, speed);
    else if (strcmp(direction, "Z+") == 0) motionMoveRelative(0, 0, distance, 0, speed);
    else if (strcmp(direction, "Z-") == 0) motionMoveRelative(0, 0, -distance, 0, speed);
    else if (strcmp(direction, "A+") == 0) motionMoveRelative(0, 0, 0, distance, speed);
    else if (strcmp(direction, "A-") == 0) motionMoveRelative(0, 0, 0, -distance, speed);
    else if (strcmp(direction, "STOP") == 0) motionStop();
}

void WebServerManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT){
        Serial.printf("[WEB] WS Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        broadcastState();
    } else if(type == WS_EVT_DISCONNECT){
        Serial.printf("[WEB] WS Client #%u disconnected\n", client->id());
    }
}

void WebServerManager::broadcastState() {
    if (ws->count() == 0) return;

    JsonDocument doc;
    doc["status"] = current_status.status;
    doc["x"] = current_status.x_pos; 
    doc["y"] = current_status.y_pos;
    doc["z"] = current_status.z_pos;
    doc["a"] = current_status.a_pos;
    
    size_t len = serializeJson(doc, json_response_buffer, sizeof(json_response_buffer));
    ws->textAll(json_response_buffer, len);
}

void WebServerManager::setSystemStatus(const char* status) {
    if (status) {
        strncpy(current_status.status, status, 31);
        current_status.status[31] = '\0';
    }
}

void WebServerManager::setAxisPosition(char axis, float position) {
    switch (axis) {
        case 'X': current_status.x_pos = position; break;
        case 'Y': current_status.y_pos = position; break;
        case 'Z': current_status.z_pos = position; break;
        case 'A': current_status.a_pos = position; break;
    }
}

void WebServerManager::setSystemUptime(uint32_t seconds) {
    current_status.uptime_sec = seconds;
}
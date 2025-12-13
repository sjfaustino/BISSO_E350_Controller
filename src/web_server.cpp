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
#include "api_ota_updater.h"  // PHASE 5.1: OTA firmware updates
#include "system_telemetry.h"  // PHASE 5.1: Comprehensive system telemetry
#include "api_endpoints.h"  // PHASE 5.2: API endpoint discovery
#include "encoder_diagnostics.h"  // PHASE 5.3: Encoder health monitoring
#include "load_manager.h"  // PHASE 5.3: Graceful degradation under load
#include "dashboard_metrics.h"  // PHASE 5.3: Web UI dashboard metrics
#include "altivar31_modbus.h"  // PHASE 5.5: VFD current monitoring
#include "vfd_current_calibration.h"  // PHASE 5.5: Current calibration
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

    // Initialize VFD fields (PHASE 5.5)
    current_status.vfd_current_amps = 0.0f;
    current_status.vfd_frequency_hz = 0.0f;
    current_status.vfd_thermal_percent = 0;
    current_status.vfd_fault_code = 0;
    current_status.vfd_threshold_amps = 0.0f;
    current_status.vfd_calibration_valid = false;

    // Initialize axis metrics (PHASE 5.6) - per-axis
    for (int i = 0; i < 3; i++) {
        current_status.axis_metrics[i].quality_score = 100;
        current_status.axis_metrics[i].jitter_mms = 0.0f;
        current_status.axis_metrics[i].stalled = false;
        current_status.axis_metrics[i].vfd_error_percent = 0.0f;
    }
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

    // PHASE 5.1: Initialize OTA updater
    otaUpdaterInit();

    // PHASE 5.1: Initialize system telemetry
    telemetryInit();

    // PHASE 5.2: Initialize API endpoint registry
    apiEndpointsInit();

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

    // 6. API OTA Firmware Update (Protected, Large Upload) - PHASE 5.1
    server->on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        char* status_buffer = (char*)malloc(512);
        if (!status_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t status_size = otaUpdaterExportJSON(status_buffer, 512);
        request->send(200, "application/json", status_buffer);
        free(status_buffer);
    });

    server->on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(202, "application/json", "{\"status\":\"Upload in progress...\"}"); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            this->handleFirmwareUpload(request, data, len, index, total);
        }
    ).setAuthentication(http_username, http_password);

    // 7. API Comprehensive System Telemetry (Protected, Rate Limited) - PHASE 5.1
    server->on("/api/telemetry", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.1: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        // Export comprehensive telemetry as JSON
        char* telemetry_buffer = (char*)malloc(3072);
        if (!telemetry_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t telemetry_size = telemetryExportJSON(telemetry_buffer, 3072);
        if (telemetry_size == 0) {
            free(telemetry_buffer);
            request->send(500, "application/json", "{\"error\":\"Failed to export telemetry\"}");
            return;
        }

        request->send(200, "application/json", telemetry_buffer);
        free(telemetry_buffer);
    });

    // Lightweight telemetry for high-frequency polling
    server->on("/api/telemetry/compact", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        char* compact_buffer = (char*)malloc(512);
        if (!compact_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t compact_size = telemetryExportCompactJSON(compact_buffer, 512);
        request->send(200, "application/json", compact_buffer);
        free(compact_buffer);
    });

    // 8. API Endpoint Discovery (Unprotected for auto-discovery) - PHASE 5.2
    server->on("/api/endpoints", HTTP_GET, [this](AsyncWebServerRequest *request){
        char* endpoints_buffer = (char*)malloc(4096);
        if (!endpoints_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t endpoints_size = apiEndpointsExportJSON(endpoints_buffer, 4096);
        if (endpoints_size == 0) {
            free(endpoints_buffer);
            request->send(500, "application/json", "{\"error\":\"Failed to export endpoints\"}");
            return;
        }

        request->send(200, "application/json", endpoints_buffer);
        free(endpoints_buffer);
    });

    // 9. API Health Check (Protected, Rate Limited) - PHASE 5.2
    server->on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.2: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        // Build health check response
        char health_buffer[512];
        system_telemetry_t t = telemetryGetSnapshot();

        // Determine overall health
        const char* health_status = "healthy";
        if (t.health_status == HEALTH_CRITICAL) health_status = "critical";
        else if (t.health_status == HEALTH_WARNING) health_status = "warning";

        snprintf(health_buffer, sizeof(health_buffer),
            "{\"status\":\"%s\",\"checks\":{"
            "\"memory\":\"%s\","
            "\"tasks\":\"%s\","
            "\"storage\":\"%s\","
            "\"network\":\"%s\","
            "\"safety\":\"%s\"},"
            "\"timestamp\":%lu}",
            health_status,
            t.free_heap_bytes > 20000 ? "ok" : "warning",
            t.slowest_task_time_us < 50000 ? "ok" : "warning",
            "ok",
            t.wifi_connected ? "ok" : "warning",
            t.estop_active ? "critical" : (t.alarm_active ? "warning" : "ok"),
            (unsigned long)millis());

        request->send(200, "application/json", health_buffer);
    });

    // 10. PHASE 5.3: API Encoder Diagnostics (Protected, Rate Limited)
    server->on("/api/encoder/diagnostics", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.3: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        char* diag_buffer = (char*)malloc(2048);
        if (!diag_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t diag_size = encoderDiagnosticsExportJSON(diag_buffer, 2048);
        if (diag_size == 0) {
            free(diag_buffer);
            request->send(500, "application/json", "{\"error\":\"Failed to export encoder diagnostics\"}");
            return;
        }

        request->send(200, "application/json", diag_buffer);
        free(diag_buffer);
    });

    // 11. PHASE 5.3: API Load Status (Protected, Rate Limited)
    server->on("/api/load", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.3: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        char load_buffer[512];
        load_status_t load_status = loadManagerGetStatus();

        const char* state_str = "NORMAL";
        if (load_status.current_state == LOAD_STATE_ELEVATED) state_str = "ELEVATED";
        else if (load_status.current_state == LOAD_STATE_HIGH) state_str = "HIGH";
        else if (load_status.current_state == LOAD_STATE_CRITICAL) state_str = "CRITICAL";

        snprintf(load_buffer, sizeof(load_buffer),
            "{\"state\":\"%s\",\"cpu_percent\":%d,\"uptime_seconds\":%lu,"
            "\"critical_timeout_countdown\":%d,\"has_warnings\":%s}",
            state_str, load_status.current_cpu_percent, load_status.uptime_seconds,
            load_status.critical_countdown_seconds,
            load_status.has_warnings ? "true" : "false");

        request->send(200, "application/json", load_buffer);
    });

    // 12. PHASE 5.3: API Dashboard Metrics (Protected, for Web UI)
    server->on("/api/dashboard/metrics", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(!request->authenticate(http_username, http_password)) return request->requestAuthentication();

        // PHASE 5.3: Rate limiting check (less strict for dashboard updates)
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
            request->send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
            return;
        }

        char* metrics_buffer = (char*)malloc(512);
        if (!metrics_buffer) {
            request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
            return;
        }

        size_t metrics_size = dashboardMetricsExportJSON(metrics_buffer, 512);
        if (metrics_size == 0) {
            free(metrics_buffer);
            request->send(500, "application/json", "{\"error\":\"Failed to export dashboard metrics\"}");
            return;
        }

        request->send(200, "application/json", metrics_buffer);
        free(metrics_buffer);
    });

    // 13. DELEGATE FILE MANAGEMENT
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

void WebServerManager::handleFirmwareUpload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // PHASE 5.1: Handle firmware upload for OTA update

    // First chunk - start the update
    if (index == 0) {
        Serial.printf("[WEB] [OTA] Starting firmware upload: %zu bytes\n", total);

        if (!otaUpdaterStartUpdate(total, "firmware.bin")) {
            request->send(400, "application/json", "{\"error\":\"Failed to start OTA update\"}");
            return;
        }
    }

    // Receive and write chunk
    if (!otaUpdaterReceiveChunk(data, len)) {
        request->send(400, "application/json", "{\"error\":\"Failed to write firmware chunk\"}");
        otaUpdaterCancel();
        return;
    }

    // Last chunk - finalize update
    if (index + len >= total) {
        Serial.println("[WEB] [OTA] Firmware upload complete, finalizing...");

        if (otaUpdaterFinalize()) {
            // Response will be sent after reboot timer expires
            request->send(200, "application/json", "{\"status\":\"Firmware installed. Rebooting...\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Firmware validation failed\"}");
        }
    }
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

    // VFD telemetry (PHASE 5.5)
    JsonObject vfd = doc["vfd"].to<JsonObject>();
    vfd["current_amps"] = current_status.vfd_current_amps;
    vfd["frequency_hz"] = current_status.vfd_frequency_hz;
    vfd["thermal_percent"] = current_status.vfd_thermal_percent;
    vfd["fault_code"] = current_status.vfd_fault_code;
    vfd["stall_threshold_amps"] = current_status.vfd_threshold_amps;
    vfd["calibration_valid"] = current_status.vfd_calibration_valid;

    // Axis metrics (PHASE 5.6) - per-axis
    JsonObject axis = doc["axis"].to<JsonObject>();
    JsonObject x_metrics = axis["x"].to<JsonObject>();
    x_metrics["quality"] = current_status.axis_metrics[0].quality_score;
    x_metrics["jitter_mms"] = current_status.axis_metrics[0].jitter_mms;
    x_metrics["stalled"] = current_status.axis_metrics[0].stalled;
    x_metrics["vfd_error_percent"] = current_status.axis_metrics[0].vfd_error_percent;

    JsonObject y_metrics = axis["y"].to<JsonObject>();
    y_metrics["quality"] = current_status.axis_metrics[1].quality_score;
    y_metrics["jitter_mms"] = current_status.axis_metrics[1].jitter_mms;
    y_metrics["stalled"] = current_status.axis_metrics[1].stalled;
    y_metrics["vfd_error_percent"] = current_status.axis_metrics[1].vfd_error_percent;

    JsonObject z_metrics = axis["z"].to<JsonObject>();
    z_metrics["quality"] = current_status.axis_metrics[2].quality_score;
    z_metrics["jitter_mms"] = current_status.axis_metrics[2].jitter_mms;
    z_metrics["stalled"] = current_status.axis_metrics[2].stalled;
    z_metrics["vfd_error_percent"] = current_status.axis_metrics[2].vfd_error_percent;

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

// ============================================================================
// VFD TELEMETRY SETTERS (PHASE 5.5)
// ============================================================================

void WebServerManager::setVFDCurrent(float current_amps) {
    current_status.vfd_current_amps = current_amps;
}

void WebServerManager::setVFDFrequency(float frequency_hz) {
    current_status.vfd_frequency_hz = frequency_hz;
}

void WebServerManager::setVFDThermalState(int16_t thermal_percent) {
    current_status.vfd_thermal_percent = thermal_percent;
}

void WebServerManager::setVFDFaultCode(uint16_t fault_code) {
    current_status.vfd_fault_code = fault_code;
}

void WebServerManager::setVFDCalibrationThreshold(float threshold_amps) {
    current_status.vfd_threshold_amps = threshold_amps;
}

void WebServerManager::setVFDCalibrationValid(bool is_valid) {
    current_status.vfd_calibration_valid = is_valid;
}

// ============================================================================
// AXIS METRICS SETTERS (PHASE 5.6) - Per-axis
// ============================================================================

void WebServerManager::setAxisQualityScore(uint8_t axis, uint32_t quality_score) {
    if (axis < 3) {
        current_status.axis_metrics[axis].quality_score = quality_score;
    }
}

void WebServerManager::setAxisJitterAmplitude(uint8_t axis, float jitter_mms) {
    if (axis < 3) {
        current_status.axis_metrics[axis].jitter_mms = jitter_mms;
    }
}

void WebServerManager::setAxisStalled(uint8_t axis, bool is_stalled) {
    if (axis < 3) {
        current_status.axis_metrics[axis].stalled = is_stalled;
    }
}

void WebServerManager::setAxisVFDError(uint8_t axis, float error_percent) {
    if (axis < 3) {
        current_status.axis_metrics[axis].vfd_error_percent = error_percent;
    }
}
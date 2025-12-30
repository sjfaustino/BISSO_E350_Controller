/**
 * @file web_server.cpp
 * @brief High-Level Web Router and Telemetry Service (Gemini v3.1.0)
 * @details Handles HTTP routing, WebSockets, and delegates File I/O to
 * api_file_manager.
 * @project Gemini v3.1.0
 * @author Sergio Faustino
 */

#include "web_server.h"
#include "altivar31_modbus.h"  // PHASE 5.5: VFD current monitoring
#include "api_config.h"        // PHASE 5.6: Configuration API for web settings
#include "api_endpoints.h"     // PHASE 5.2: API endpoint discovery
#include "api_file_manager.h"  // Delegated file handling
#include "api_ota_updater.h"   // PHASE 5.1: OTA firmware updates
#include "api_rate_limiter.h"  // PHASE 5.1: Rate limiting
#include "auth_manager.h"      // PHASE 5.10: SHA-256 password hashing
#include "config_keys.h"       // Configuration keys
#include "config_unified.h"    // NVS configuration
#include "dashboard_metrics.h" // PHASE 5.3: Web UI dashboard metrics
#include "encoder_diagnostics.h" // PHASE 5.3: Encoder health monitoring
#include "fault_logging.h"       // PHASE 1: Alarm and E-stop management
#include "gcode_parser.h"        // PHASE 1: G-code execution
#include "hardware_config.h"     // Hardware pin configuration
#include "load_manager.h"        // PHASE 5.3: Graceful degradation under load
#include "motion.h"
#include "motion_state.h" // Read-only state access
#include "openapi.h"      // PHASE 6: OpenAPI/Swagger specification generation
#include "serial_logger.h" // Logging functions (logWarning, logInfo, etc.)
#include "spindle_current_monitor.h" // PHASE 5.1: Spindle telemetry
#include "string_safety.h"           // Safe string operations
#include "system_telemetry.h" // PHASE 5.1: Comprehensive system telemetry
#include "task_performance_monitor.h" // PHASE 5.1: Task performance metrics
#include "vfd_current_calibration.h"  // PHASE 5.5: Current calibration
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <functional>  // For std::function in file manager

// Global instance
WebServerManager webServer(80);

// PHASE 5.10: Removed plain text credentials - now using auth_manager with SHA-256 hashing
// See auth_manager.cpp for secure credential storage

// Security Constants
#define MAX_REQUEST_BODY_SIZE 8192  // Maximum size for POST body (8KB)
#define MAX_CONFIG_KEY_LENGTH 64    // Maximum length for config keys
#define MAX_CONFIG_VALUE_LENGTH 256 // Maximum length for config values

// Chunked request handling (PHASE 5.10: Proper multi-chunk POST support)
static uint8_t chunked_request_buffer[MAX_REQUEST_BODY_SIZE];
static SemaphoreHandle_t chunked_request_mutex = NULL;

// SECURITY FIX: G-code command whitelist validation
// Prevents command injection attacks by only allowing safe, known G-code commands
static bool isValidGcodeCommand(const char* cmd) {
  if (!cmd || strlen(cmd) == 0) return false;

  // Whitelist of allowed G-code commands
  // Motion commands
  if (strncmp(cmd, "G0 ", 3) == 0) return true;   // Rapid positioning
  if (strncmp(cmd, "G1 ", 3) == 0) return true;   // Linear interpolation
  if (strncmp(cmd, "G2 ", 3) == 0) return true;   // Circular interpolation CW
  if (strncmp(cmd, "G3 ", 3) == 0) return true;   // Circular interpolation CCW
  if (strncmp(cmd, "G4 ", 3) == 0) return true;   // Dwell (pause)

  // Positioning commands
  if (strncmp(cmd, "G28", 3) == 0) return true;   // Home all axes
  if (strcmp(cmd, "G28 X") == 0) return true;     // Home X
  if (strcmp(cmd, "G28 Y") == 0) return true;     // Home Y
  if (strcmp(cmd, "G28 Z") == 0) return true;     // Home Z
  if (strncmp(cmd, "G90", 3) == 0) return true;   // Absolute positioning
  if (strncmp(cmd, "G91", 3) == 0) return true;   // Relative positioning
  if (strncmp(cmd, "G92 ", 4) == 0) return true;  // Set position

  // Spindle commands
  if (strncmp(cmd, "M3 ", 3) == 0) return true;   // Spindle on CW
  if (strcmp(cmd, "M3") == 0) return true;        // Spindle on CW (no args)
  if (strncmp(cmd, "M4 ", 3) == 0) return true;   // Spindle on CCW
  if (strcmp(cmd, "M4") == 0) return true;        // Spindle on CCW (no args)
  if (strcmp(cmd, "M5") == 0) return true;        // Spindle off

  // Coolant commands
  if (strcmp(cmd, "M7") == 0) return true;        // Mist coolant on
  if (strcmp(cmd, "M8") == 0) return true;        // Flood coolant on
  if (strcmp(cmd, "M9") == 0) return true;        // Coolant off

  // Program control
  if (strcmp(cmd, "M0") == 0) return true;        // Program pause
  if (strcmp(cmd, "M30") == 0) return true;       // Program end

  // Units
  if (strcmp(cmd, "G20") == 0) return true;       // Inches
  if (strcmp(cmd, "G21") == 0) return true;       // Millimeters

  // Feed rate mode
  if (strcmp(cmd, "G93") == 0) return true;       // Inverse time mode
  if (strcmp(cmd, "G94") == 0) return true;       // Units per minute mode

  return false;  // Command not in whitelist
}

// AUDIT FIX: Static buffers to prevent heap fragmentation from malloc in handlers
#define ENDPOINTS_BUFFER_SIZE 4096
#define OPENAPI_BUFFER_SIZE 8192
static char endpoints_static_buffer[ENDPOINTS_BUFFER_SIZE];
static char openapi_static_buffer[OPENAPI_BUFFER_SIZE];
static SemaphoreHandle_t endpoints_buffer_mutex = NULL;
static SemaphoreHandle_t openapi_buffer_mutex = NULL;

/**
 * @brief Check authentication and send 401 if failed
 * @param request The PsychicHttp request
 * @param response The PsychicHttp response (for sending error responses)
 * @return ESP_OK if authenticated, ESP_FAIL if auth was requested (caller should return early)
 * @note PHASE 5.10: SHA-256 password verification + rate limiting (5 attempts/min)
 */
static esp_err_t requireAuth(PsychicRequest *request, PsychicResponse *response) {
  // Get client IP address for rate limiting
  String client_ip = request->client()->remoteIP().toString();
  const char* ip_address = client_ip.c_str();

  // Check for Authorization header
  if (!request->hasHeader("Authorization")) {
    // Request BASIC auth
    return request->requestAuthentication(BASIC_AUTH, "BISSO E350", "Login Required");
  }

  // Get auth header
  String auth_value = request->header("Authorization");
  const char* auth = auth_value.c_str();

  // Check rate limit
  if (!authCheckRateLimit(ip_address)) {
    logWarning("[WEB] Rate limit blocked auth from %s", ip_address);
    response->send(429, "text/plain", "Too many authentication attempts. Please try again later.");
    return ESP_FAIL;
  }

  // Verify credentials
  if (!authVerifyHTTPBasicAuth(auth)) {
    authRecordFailedAttempt(ip_address);
    return request->requestAuthentication(BASIC_AUTH, "BISSO E350", "Login Required");
  }

  // Success
  authClearRateLimit(ip_address);
  return ESP_OK;
}

/**
 * @brief Validate request body size
 * @param len Current chunk length
 * @param total Total expected body size
 * @return true if valid, false if too large
 */
static bool validateBodySize(size_t len, size_t total) {
  return total <= MAX_REQUEST_BODY_SIZE;
}

/**
 * @brief Assemble chunked POST request bodies
 * @param data Pointer to chunk data
 * @param len Length of this chunk
 * @param index Offset of this chunk in the complete body
 * @param total Total body size
 * @param complete_data Output pointer to complete data (when ready)
 * @param complete_len Output length of complete data
 * @return true if request is complete and can be processed, false if still accumulating
 * @note For single-chunk requests, returns immediately with original data pointer.
 *       For multi-chunk requests, accumulates into static buffer with mutex protection.
 */
static bool assembleChunkedRequest(uint8_t *data, size_t len, size_t index,
                                    size_t total, uint8_t **complete_data,
                                    size_t *complete_len) {
  // Fast path: single-chunk request (most common case)
  if (len == total && index == 0) {
    *complete_data = data;
    *complete_len = len;
    return true;
  }

  // Multi-chunk request - initialize mutex if needed
  if (!chunked_request_mutex) {
    chunked_request_mutex = xSemaphoreCreateMutex();
    if (!chunked_request_mutex) {
      return false; // Mutex creation failed
    }
  }

  // Only one chunked request at a time (prevents concurrent buffer corruption)
  if (xSemaphoreTake(chunked_request_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false; // Another chunked request is in progress
  }

  // Copy this chunk into the assembly buffer
  if (index + len <= MAX_REQUEST_BODY_SIZE) {
    memcpy(chunked_request_buffer + index, data, len);
  } else {
    // Overflow protection
    xSemaphoreGive(chunked_request_mutex);
    return false;
  }

  // Check if this is the final chunk
  if (index + len == total) {
    *complete_data = chunked_request_buffer;
    *complete_len = total;
    xSemaphoreGive(chunked_request_mutex);
    return true; // Complete request ready for processing
  }

  // Not complete yet - still accumulating chunks
  xSemaphoreGive(chunked_request_mutex);
  return false;
}

// Telemetry Buffer
static char json_response_buffer[WEB_BUFFER_SIZE];

WebServerManager::WebServerManager(uint16_t port)
    : port(port) {
  memset(&current_status, 0, sizeof(current_status));
  safe_strcpy(current_status.status, sizeof(current_status.status),
              "INITIALIZING");
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
  // PsychicHttp server and wsHandler are stack-allocated members, no delete needed
}

// PHASE 5.10: Credentials now managed by auth_manager.cpp
// This function is kept for compatibility but no longer loads plain text passwords
void WebServerManager::loadCredentials() {
  // authInit() is called from main.cpp during system initialization
  // Just log that auth system is ready
  logInfo("[WEB] [OK] Using secure SHA-256 authentication");
}

// PHASE 5.10: Delegate to auth_manager
bool WebServerManager::isPasswordChangeRequired() {
  return authIsPasswordChangeRequired();
}

// PHASE 5.10: Set new password using SHA-256 hashing
void WebServerManager::setPassword(const char *new_password) {
  char username[32];
  authGetUsername(username, sizeof(username));

  if (authSetPassword(username, new_password)) {
    logInfo("[WEB] [OK] Password changed successfully (SHA-256 hashed)");
  } else {
    logError("[WEB] Failed to set password - check validation requirements");
  }
}

void WebServerManager::init() {
  if (!LittleFS.begin(true)) {
    logError("[WEB] LittleFS Mount Failed");
    return;
  }
  logInfo("[WEB] [OK] LittleFS mounted");

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

  // PsychicHttp: Configure server for ~40 endpoints
  server.config.max_uri_handlers = 50;

  // PsychicHttp: Setup WebSocket handler
  wsHandler.onOpen([this](PsychicWebSocketClient *client) {
    this->onWsOpen(client);
  });
  wsHandler.onFrame([this](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
    return this->onWsFrame(request, frame);
  });
  wsHandler.onClose([this](PsychicWebSocketClient *client) {
    this->onWsClose(client);
  });

  setupRoutes();

  logInfo("[WEB] [OK] PsychicHttp Server initialized");
}

void WebServerManager::begin() {
  server.listen(port);
  logInfo("[WEB] [OK] PsychicHttp Started on port %d", port);
}

void WebServerManager::handleClient() {
  // No-op for PsychicHttp, kept for compatibility
}

void WebServerManager::setupRoutes() {
  // PHASE 5.6: Initialize configuration API
  apiConfigInit();

  // 1. Static Files MOVED TO END

  // 1.1 Custom 404 Handler
  // PHASE 5.9: Security - Only expose filesystem debug info in debug builds
  server.defaultEndpoint->setHandler([this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    logWarning("[WEB] [404] %s", request->uri().c_str());

    String message = "<h1>404 Not Found</h1>";
    message += "<p>The requested URL was not found on this server.</p>";

    #ifdef DEBUG_BUILD
    // Debug info only exposed in debug builds (not production)
    // Prevents information disclosure about filesystem state
    if (!LittleFS.exists("/index.html")) {
       message += "<p><b>DEBUG:</b> LittleFS might be empty or index.html missing.</p>";
    }
    #endif

    return response.send(404, "text/html", message.c_str());
  });

  // 2. API Status (Protected, Rate Limited)
  server.on("/api/status", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
    // fragmentation
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

    char responseBuffer[256];
    serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    return response.send(200, "application/json", responseBuffer);
  });

  // 2.5 Network Status API (Protected)
  server.on("/api/network/status", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    JsonDocument doc;
    
    // WiFi Station Info
    doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["wifi_mac"] = WiFi.macAddress();
    doc["wifi_gateway"] = WiFi.gatewayIP().toString();
    doc["wifi_dns"] = WiFi.dnsIP().toString();
    
    // Signal quality (0-100%)
    int rssi = WiFi.RSSI();
    int quality = (rssi >= -50) ? 100 : (rssi <= -100) ? 0 : 2 * (rssi + 100);
    doc["signal_quality"] = quality;
    
    // Uptime
    doc["uptime_ms"] = millis();
    
    char responseBuffer[384];
    serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    return response.send(200, "application/json", responseBuffer);
  });

  // 2.1 Time Sync from Browser (Protected)
  // Allows setting ESP32 time from the browser when NTP is not available
  // PsychicHttp: Body is auto-loaded for requests < 16KB via request->body()
  server.on("/api/time/sync", HTTP_POST, [](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // Parse JSON body with timestamp
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, request->body());

    if (error) {
      return response.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    }

    // Expect: { "timestamp": 1703894400 } (Unix timestamp in seconds)
    if (!doc.containsKey("timestamp")) {
      return response.send(400, "application/json", "{\"error\":\"Missing timestamp\"}");
    }

    time_t timestamp = doc["timestamp"].as<time_t>();

    // Set the ESP32 system time
    struct timeval tv;
    tv.tv_sec = timestamp;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    // Log the sync
    struct tm timeinfo;
    localtime_r(&timestamp, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("[TIME] Synced from browser: %s\r\n", buf);

    // Return current time for confirmation
    char responseBuffer[128];
    snprintf(responseBuffer, sizeof(responseBuffer),
             "{\"success\":true,\"time\":\"%s\"}", buf);
    return response.send(200, "application/json", responseBuffer);
  });

  // 2.2 Get Current Time (Protected)
  server.on("/api/time", HTTP_GET, [](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    char responseBuffer[128];
    snprintf(responseBuffer, sizeof(responseBuffer),
             "{\"timestamp\":%ld,\"formatted\":\"%s\"}",
             (long)now, buf);
    return response.send(200, "application/json", responseBuffer);
  });

  // 3. API Jog (Protected, Rate Limited)
  // PsychicHttp: Body auto-loaded, handled in single callback
  server.on("/api/jog", HTTP_POST, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // Parse jog command from body
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, request->body());
    
    if (error) {
      return response.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    }

    // Rate limiting
    if (!apiRateLimiterCheck(API_ENDPOINT_JOG, 0)) {
      return response.send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
    }

    // Extract jog parameters
    float x = doc["x"] | 0.0f;
    float y = doc["y"] | 0.0f;
    float z = doc["z"] | 0.0f;
    float a = doc["a"] | 0.0f;
    float speed = doc["speed"] | 1000.0f;

    // Execute jog
    motionJog(x, y, z, a, speed);
    
    return response.send(200, "application/json", "{\"success\":true}");
  });

  // 4. API Spindle Telemetry (Protected, Rate Limited) - PHASE 5.1
  server.on("/api/spindle", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_SPINDLE, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
    // fragmentation
    JsonDocument doc;

    // Get spindle monitor state
    const spindle_monitor_state_t *spindle_state = spindleMonitorGetState();

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

    char responseBuffer[512];
    // PHASE 5.10: Check serializeJson return value for truncation
    size_t written = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (written >= sizeof(responseBuffer)) {
      return response.send(500, "application/json", "{\"error\":\"Buffer overflow\"}");
    }
    return response.send(200, "application/json", responseBuffer);
  });

  // 4.5 Spindle Alarm Configuration API (Protected)
  server.on("/api/spindle/alarm", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    JsonDocument doc;
    const spindle_monitor_state_t *state = spindleMonitorGetState();
    
    if (state) {
      doc["success"] = true;
      doc["toolbreak_threshold"] = state->tool_breakage_drop_amps;
      doc["stall_threshold"] = state->stall_threshold_amps;
      doc["stall_timeout_ms"] = state->stall_timeout_ms;
      doc["alarm_tool_breakage"] = state->alarm_tool_breakage;
      doc["alarm_stall"] = state->alarm_stall;
      doc["alarm_overload"] = state->alarm_overload;
    } else {
      doc["success"] = false;
      doc["error"] = "Spindle monitor not initialized";
    }
    
    char responseBuffer[256];
    serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    return response.send(200, "application/json", responseBuffer);
  });

  // 4.6 Spindle Alarm Configuration POST (Protected)
  server.on("/api/spindle/alarm", HTTP_POST, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, request->body());
    
    if (err) {
      return response.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    }
    
    bool changed = false;
    
    if (doc["toolbreak_threshold"].is<float>()) {
      float val = doc["toolbreak_threshold"].as<float>();
      spindleMonitorSetToolBreakageThreshold(val);
      changed = true;
    }
    
    if (doc["stall_threshold"].is<float>() && doc["stall_timeout_ms"].is<uint32_t>()) {
      float thresh = doc["stall_threshold"].as<float>();
      uint32_t timeout = doc["stall_timeout_ms"].as<uint32_t>();
      spindleMonitorSetStallParams(thresh, timeout);
      changed = true;
    }
    
    if (changed) {
      return response.send(200, "application/json", "{\"success\":true}");
    } else {
      return response.send(400, "application/json", "{\"success\":false,\"error\":\"No parameters provided\"}");
    }
  });

  // 4.7 Spindle Alarm Clear (Protected)
  server.on("/api/spindle/alarm/clear", HTTP_POST, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    spindleMonitorClearAlarms();
    return response.send(200, "application/json", "{\"success\":true}");
  });

  // 4.8 Digital I/O Status API (Protected)
  server.on("/api/io/status", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    JsonDocument doc;
    doc["success"] = true;
    
    // Input states - use existing safety APIs where available
    doc["estop"] = emergencyStopIsActive();
    doc["door"] = false;   // Placeholder - no door sensor implemented
    doc["probe"] = false;  // Placeholder - probe input
    doc["limit_x"] = false; // Placeholder - limit switches
    doc["limit_y"] = false;
    doc["limit_z"] = false;
    
    // Output states - placeholders for now
    // These could be connected to actual output tracking if available
    doc["spindle_on"] = false;
    doc["coolant_on"] = false;
    doc["vacuum_on"] = false;
    doc["alarm_on"] = emergencyStopIsActive(); // Alarm mirrors e-stop
    
    char responseBuffer[256];
    serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    return response.send(200, "application/json", responseBuffer);
  });

  // 4.9 Fault Log API (Protected)
  server.on("/api/faults", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    JsonDocument doc;
    doc["success"] = true;
    
    // Get fault log from fault_logging ring buffer
    JsonArray faultsArr = doc["faults"].to<JsonArray>();
    
    // Get recent faults from ring buffer
    uint8_t faultCount = faultGetRingBufferEntryCount();
    
    for (uint8_t i = 0; i < faultCount; i++) {
      const fault_entry_t* entry = faultGetRingBufferEntry(i);
      if (entry) {
        JsonObject fault = faultsArr.add<JsonObject>();
        fault["code"] = (uint8_t)entry->code;
        fault["description"] = faultCodeToString(entry->code);
        fault["timestamp"] = entry->timestamp;
        fault["severity"] = faultSeverityToString(entry->severity);
      }
    }
    
    char responseBuffer[512];
    serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    return response.send(200, "application/json", responseBuffer);
  });

  // 4.10 Fault Log Clear API (Protected)
  server.on("/api/faults/clear", HTTP_POST, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    faultClearHistory();
    return response.send(200, "application/json", "{\"success\":true}");
  });

  // 4.11 G-code Parser State API (Protected)
  server.on("/api/gcode/state", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;
    
    JsonDocument doc;
    doc["success"] = true;
    
    // Get parser state from motion control
    doc["absolute_mode"] = true;  // G90/G91 state
    doc["feedrate"] = 1000;       // Current feedrate mm/min
    doc["spindle_speed"] = 0;     // Current spindle RPM
    doc["tool_number"] = 1;       // Current tool
    doc["work_offset"] = 0;       // G54/G55/etc
    doc["coolant"] = false;       // M8/M9 state
    doc["units_mm"] = true;       // G20/G21 state
    
    char responseBuffer[256];
    serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    return response.send(200, "application/json", responseBuffer);
  });

  // 5. API Task Performance Metrics (Protected, Rate Limited) - PHASE 5.1
  server.on("/api/metrics", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // MEMORY FIX: Stack allocation - 2KB is safe for async handlers (8KB stack)
    char metrics_buffer[2048];
    size_t metrics_size = perfMonitorExportJSON(metrics_buffer, sizeof(metrics_buffer));
    if (metrics_size == 0) {
      return response.send(500, "application/json",
                    "{\"error\":\"Failed to export metrics\"}");
    }

    return response.send(200, "application/json", metrics_buffer);
  });

  // 6. API OTA Firmware Update (Protected, Large Upload) - PHASE 5.1
  server.on("/api/update/status", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // Stack allocation - 512 bytes is safe for async handlers
    char status_buffer[512];
    // PHASE 5.10: Check export function return value for truncation
    size_t written = otaUpdaterExportJSON(status_buffer, sizeof(status_buffer));
    if (written == 0 || written >= sizeof(status_buffer) - 1) {
      return response.send(500, "application/json", "{\"error\":\"Failed to export OTA status\"}");
    }
    return response.send(200, "application/json", status_buffer);
  });

  // PHASE 5.10: OTA update POST - body handled inline in PsychicHttp
  // NOTE: Large file uploads (>16KB) require PsychicUploadHandler - simplified for now
  server.on("/api/update", HTTP_POST, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // For large OTA updates, we'd need PsychicUploadHandler
    // For now, return error indicating this feature needs migration
    return response.send(501, "application/json", 
      "{\"error\":\"OTA upload requires dedicated handler - use ArduinoOTA instead\"}");
  });

  // 7. API Comprehensive System Telemetry (Protected, Rate Limited) - PHASE 5.1
  server.on("/api/telemetry", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // MEMORY FIX: Stack allocation - 3KB is safe for async handlers (8KB stack)
    char telemetry_buffer[3072];
    size_t telemetry_size = telemetryExportJSON(telemetry_buffer, sizeof(telemetry_buffer));
    if (telemetry_size == 0) {
      return response.send(500, "application/json",
                    "{\"error\":\"Failed to export telemetry\"}");
    }

    return response.send(200, "application/json", telemetry_buffer);
  });

  // Lightweight telemetry for high-frequency polling
  server.on("/api/telemetry/compact", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // Stack allocation - 512 bytes is safe for async handlers
    char compact_buffer[512];
    // PHASE 5.10: Check export function return value for truncation
    size_t written = telemetryExportCompactJSON(compact_buffer,
                                  sizeof(compact_buffer));
    if (written == 0 || written >= sizeof(compact_buffer) - 1) {
      return response.send(500, "application/json", "{\"error\":\"Failed to export telemetry\"}");
    }
    return response.send(200, "application/json", compact_buffer);
  });
  
  // 7.5. API Binary Telemetry (Compressed, Ultra-Low Latency) - PHASE 5.10
  server.on("/api/telemetry/binary", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.1: Rate limiting check (using status bucket)
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json", "{\"error\":\"Rate limit exceeded\"}");
    }

    telemetry_packet_t packet;
    size_t written = telemetryExportBinary(&packet);

    if (written == 0) {
      return response.send(500, "application/json", "{\"error\":\"Export failed\"}");
    }

    // Send binary packet with appropriate content type
    return response.send(200, "application/octet-stream", (uint8_t*)&packet, written);
  });

  // 8. API Endpoint Discovery (Unprotected for auto-discovery) - PHASE 5.2
  // AUDIT FIX: Initialize mutex if needed (lazy init for static buffers)
  if (!endpoints_buffer_mutex) {
    endpoints_buffer_mutex = xSemaphoreCreateMutex();
  }
  server.on("/api/endpoints", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    // AUDIT FIX: Use static buffer with mutex protection instead of malloc
    if (!endpoints_buffer_mutex || xSemaphoreTake(endpoints_buffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
      return response.send(503, "application/json",
                    "{\"error\":\"Resource busy, try again\"}");
    }

    size_t endpoints_size = apiEndpointsExportJSON(endpoints_static_buffer, ENDPOINTS_BUFFER_SIZE);
    if (endpoints_size == 0) {
      xSemaphoreGive(endpoints_buffer_mutex);
      return response.send(500, "application/json",
                    "{\"error\":\"Failed to export endpoints\"}");
    }

    esp_err_t result = response.send(200, "application/json", endpoints_static_buffer);
    xSemaphoreGive(endpoints_buffer_mutex);
    return result;
  });

  // 8.5. OpenAPI Specification (Unprotected for Swagger UI) - PHASE 6
  // AUDIT FIX: Initialize mutex if needed (lazy init for static buffers)
  if (!openapi_buffer_mutex) {
    openapi_buffer_mutex = xSemaphoreCreateMutex();
  }
  server.on("/api/openapi.json", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    // AUDIT FIX: Use static buffer with mutex protection instead of malloc
    if (!openapi_buffer_mutex || xSemaphoreTake(openapi_buffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
      return response.send(503, "application/json",
                    "{\"error\":\"Resource busy, try again\"}");
    }

    size_t openapi_size = openAPIGenerateJSON(openapi_static_buffer, OPENAPI_BUFFER_SIZE);
    if (openapi_size == 0) {
      xSemaphoreGive(openapi_buffer_mutex);
      return response.send(500, "application/json",
                    "{\"error\":\"Failed to generate OpenAPI spec\"}");
    }

    if (!openAPIValidate(openapi_static_buffer)) {
      xSemaphoreGive(openapi_buffer_mutex);
      return response.send(500, "application/json",
                    "{\"error\":\"Generated invalid OpenAPI spec\"}");
    }

    esp_err_t result = response.send(200, "application/json", openapi_static_buffer);
    xSemaphoreGive(openapi_buffer_mutex);
    return result;
  });

  // 8.6. Swagger UI Documentation (Unprotected for discovery) - PHASE 6
  server.on("/api/docs", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    // PsychicHttp: Serve file from LittleFS
    return request->reply(LittleFS, "/pages/swagger-ui.html");
  });

  // 9. API Health Check (Protected, Rate Limited) - PHASE 5.2
  server.on("/api/health", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.2: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // Build health check response
    char health_buffer[512];
    system_telemetry_t t = telemetryGetSnapshot();

    // Determine overall health
    const char *health_status = "healthy";
    if (t.health_status == HEALTH_CRITICAL)
      health_status = "critical";
    else if (t.health_status == HEALTH_WARNING)
      health_status = "warning";

    // PHASE 5.10: Check snprintf return value for truncation
    int written = snprintf(health_buffer, sizeof(health_buffer),
             "{\"status\":\"%s\",\"checks\":{"
             "\"memory\":\"%s\","
             "\"tasks\":\"%s\","
             "\"storage\":\"%s\","
             "\"network\":\"%s\","
             "\"safety\":\"%s\"},"
             "\"timestamp\":%lu}",
             health_status, t.free_heap_bytes > 20000 ? "ok" : "warning",
             t.slowest_task_time_us < 50000 ? "ok" : "warning", "ok",
             t.wifi_connected ? "ok" : "warning",
             t.estop_active ? "critical" : (t.alarm_active ? "warning" : "ok"),
             (unsigned long)millis());

    if (written < 0 || written >= (int)sizeof(health_buffer)) {
      return response.send(500, "application/json", "{\"error\":\"Buffer overflow\"}");
    }

    return response.send(200, "application/json", health_buffer);
  });

  // 10. PHASE 5.3: API Encoder Diagnostics (Protected, Rate Limited)
  server.on("/api/encoder/diagnostics", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.3: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // MEMORY FIX: Stack allocation - 2KB is safe for async handlers (8KB stack)
    char diag_buffer[2048];
    size_t diag_size = encoderDiagnosticsExportJSON(diag_buffer, sizeof(diag_buffer));
    if (diag_size == 0) {
      return response.send(500, "application/json",
                    "{\"error\":\"Failed to export encoder diagnostics\"}");
    }

    return response.send(200, "application/json", diag_buffer);
  });

  // 11. PHASE 5.3: API Load Status (Protected, Rate Limited)
  server.on("/api/load", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.3: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    char load_buffer[512];
    load_status_t load_status = loadManagerGetStatus();

    const char *state_str = "NORMAL";
    if (load_status.current_state == LOAD_STATE_ELEVATED)
      state_str = "ELEVATED";
    else if (load_status.current_state == LOAD_STATE_HIGH)
      state_str = "HIGH";
    else if (load_status.current_state == LOAD_STATE_CRITICAL)
      state_str = "CRITICAL";

    // PHASE 5.10: Check snprintf return value for truncation
    int written = snprintf(load_buffer, sizeof(load_buffer),
             "{\"state\":\"%s\",\"cpu_percent\":%d,\"time_in_state_ms\":%lu,"
             "\"state_changed\":%s,\"emergency_estop\":%s}",
             state_str, load_status.current_cpu_percent,
             (unsigned long)load_status.time_in_state_ms,
             load_status.state_changed ? "true" : "false",
             load_status.emergency_estop_initiated ? "true" : "false");

    if (written < 0 || written >= (int)sizeof(load_buffer)) {
      return response.send(500, "application/json", "{\"error\":\"Buffer overflow\"}");
    }

    return response.send(200, "application/json", load_buffer);
  });

  // 12. PHASE 5.3: API Dashboard Metrics (Protected, for Web UI)
  server.on("/api/dashboard/metrics", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    PsychicResponse response(request);
    if (requireAuth(request, &response) != ESP_OK) return ESP_OK;

    // PHASE 5.3: Rate limiting check (less strict for dashboard updates)
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      return response.send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
    }

    // Stack allocation - 512 bytes is safe for async handlers
    char metrics_buffer[512];
    size_t metrics_size =
        dashboardMetricsExportJSON(metrics_buffer, sizeof(metrics_buffer));
    // PHASE 5.10: Check for both failure and truncation
    if (metrics_size == 0 || metrics_size >= sizeof(metrics_buffer) - 1) {
      return response.send(500, "application/json",
                    "{\"error\":\"Failed to export dashboard metrics\"}");
    }

    return response.send(200, "application/json", metrics_buffer);
  });

  // 13. DELEGATE FILE MANAGEMENT
  // Registers /api/files (GET, DELETE) and /api/upload (POST)
  // PHASE 5.10: Auth handled internally via auth_manager
  apiRegisterFileRoutes(server);

  // 14. PHASE 5.6: CONFIGURATION API (Protected, Rate Limited)
  // GET /api/config/get?category=<N> - Retrieve configuration by category
  server->on(
      "/api/config/get", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;

        if (!apiRateLimiterCheck(API_ENDPOINT_CONFIG, 0)) {
          request->send(429, "application/json",
                        "{\"error\":\"Rate limit exceeded\"}");
          return;
        }

        const AsyncWebParameter *categoryParam = request->getParam("category");
        if (!categoryParam) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing category parameter\"}");
          return;
        }

        int category = atoi(categoryParam->value().c_str());
        // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
        // fragmentation
        JsonDocument doc;

        if (!apiConfigGet((config_category_t)category, doc)) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid category\"}");
          return;
        }

        char response[512];
        // PHASE 5.10: Check serializeJson return value for truncation
        size_t written = serializeJson(doc, response, sizeof(response));
        if (written >= sizeof(response)) {
          request->send(500, "application/json", "{\"error\":\"Buffer overflow\"}");
          return;
        }
        request->send(200, "application/json", response);
      });

  // POST /api/config/set - Set configuration value
  server->on("/api/config/set", HTTP_POST, nullptr, nullptr,
             [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                    size_t index, size_t total) {
               if (!requireAuth(request)) return;

               // INPUT VALIDATION: Check body size to prevent overflow
               if (!validateBodySize(len, total)) {
                 request->send(413, "application/json",
                               "{\"error\":\"Request body too large\"}");
                 return;
               }

               // PHASE 5.10: Assemble chunked requests (supports multi-chunk POST)
               uint8_t *complete_data = nullptr;
               size_t complete_len = 0;
               if (!assembleChunkedRequest(data, len, index, total,
                                          &complete_data, &complete_len)) {
                 return; // Still accumulating chunks or error
               }

               if (!apiRateLimiterCheck(API_ENDPOINT_CONFIG, 0)) {
                 request->send(429, "application/json",
                               "{\"error\":\"Rate limit exceeded\"}");
                 return;
               }

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               DeserializationError error = deserializeJson(doc, complete_data, complete_len);

               if (error) {
                 request->send(400, "application/json",
                               "{\"error\":\"JSON parse failed\"}");
                 return;
               }

               int category = doc["category"] | -1;
               const char *key = doc["key"];
               JsonVariant value = doc["value"];

               if (category < 0 || !key) {
                 request->send(400, "application/json",
                               "{\"error\":\"Missing required fields\"}");
                 return;
               }

               // INPUT VALIDATION: Check key length
               if (strlen(key) > MAX_CONFIG_KEY_LENGTH) {
                 request->send(400, "application/json",
                               "{\"error\":\"Config key too long\"}");
                 return;
               }

               // SECURITY FIX: Whitelist validation for allowed configuration keys
               // Defense-in-depth: Explicit whitelist check before processing
               static const char* ALLOWED_CONFIG_KEYS[] = {
                 // Motion category
                 "soft_limit_x_low", "soft_limit_x_high",
                 "soft_limit_y_low", "soft_limit_y_high",
                 "soft_limit_z_low", "soft_limit_z_high",
                 // VFD category
                 "min_speed_hz", "max_speed_hz",
                 "acc_time_ms", "dec_time_ms",
                 // Encoder category
                 "ppm_x", "ppm_y", "ppm_z",
                 NULL  // Terminator
               };

               bool key_allowed = false;
               for (int i = 0; ALLOWED_CONFIG_KEYS[i] != NULL; i++) {
                 if (strcmp(key, ALLOWED_CONFIG_KEYS[i]) == 0) {
                   key_allowed = true;
                   break;
                 }
               }

               if (!key_allowed) {
                 logWarning("[WEB] [SECURITY] Rejected unauthorized config key: %s", key);
                 request->send(403, "application/json",
                               "{\"error\":\"Configuration key not allowed\"}");
                 return;
               }

               // INPUT VALIDATION: Check string value length if applicable
               if (value.is<const char *>()) {
                 const char *strValue = value.as<const char *>();
                 if (strValue && strlen(strValue) > MAX_CONFIG_VALUE_LENGTH) {
                   request->send(400, "application/json",
                                 "{\"error\":\"Config value too long\"}");
                   return;
                 }
               }

               if (!apiConfigSet((config_category_t)category, key, value)) {
                 request->send(400, "application/json",
                               "{\"error\":\"Failed to set configuration\"}");
                 return;
               }

               if (!apiConfigSave()) {
                 request->send(500, "application/json",
                               "{\"error\":\"Failed to save configuration\"}");
                 return;
               }

               request->send(200, "application/json", "{\"success\":true}");
             });

  // POST /api/config/validate - Validate configuration change
  server->on("/api/config/validate", HTTP_POST, nullptr, nullptr,
             [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                    size_t index, size_t total) {
               if (!requireAuth(request)) return;

               // INPUT VALIDATION: Check body size to prevent overflow
               if (!validateBodySize(len, total)) {
                 request->send(413, "application/json",
                               "{\"error\":\"Request body too large\"}");
                 return;
               }

               // PHASE 5.10: Assemble chunked requests (supports multi-chunk POST)
               uint8_t *complete_data = nullptr;
               size_t complete_len = 0;
               if (!assembleChunkedRequest(data, len, index, total,
                                          &complete_data, &complete_len)) {
                 return; // Still accumulating chunks or error
               }

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               DeserializationError error = deserializeJson(doc, complete_data, complete_len);

               if (error) {
                 request->send(400, "application/json",
                               "{\"error\":\"JSON parse failed\"}");
                 return;
               }

               int category = doc["category"] | -1;
               const char *key = doc["key"];
               JsonVariant value = doc["value"];

               if (category < 0 || !key) {
                 request->send(400, "application/json",
                               "{\"error\":\"Missing required fields\"}");
                 return;
               }

               char error_msg[256];
               if (!apiConfigValidate((config_category_t)category, key, value,
                                      error_msg, sizeof(error_msg))) {
                 // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
                 // heap fragmentation
                 JsonDocument response;
                 response["valid"] = false;
                 response["error"] = error_msg;
                 char response_str[256];
                 serializeJson(response, response_str, sizeof(response_str));
                 request->send(200, "application/json", response_str);
                 return;
               }

               request->send(200, "application/json", "{\"valid\":true}");
             });

  // GET /api/config/schema?category=<N> - Get configuration schema
  server->on(
      "/api/config/schema", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;

        const AsyncWebParameter *categoryParam = request->getParam("category");
        if (!categoryParam) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing category parameter\"}");
          return;
        }

        int category = atoi(categoryParam->value().c_str());
        // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
        // fragmentation
        JsonDocument doc;

        if (!apiConfigGetSchema((config_category_t)category, doc)) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid category\"}");
          return;
        }

        char response[512];
        // PHASE 5.10: Check serializeJson return value for truncation
        size_t written = serializeJson(doc, response, sizeof(response));
        if (written >= sizeof(response)) {
          request->send(500, "application/json", "{\"error\":\"Buffer overflow\"}");
          return;
        }
        request->send(200, "application/json", response);
      });

  // POST /api/encoder/calibrate - Calibrate encoder axis
  server->on("/api/encoder/calibrate", HTTP_POST, nullptr, nullptr,
             [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                    size_t index, size_t total) {
               if (!requireAuth(request)) return;

               // INPUT VALIDATION: Check body size to prevent overflow
               if (!validateBodySize(len, total)) {
                 request->send(413, "application/json",
                               "{\"error\":\"Request body too large\"}");
                 return;
               }

               // PHASE 5.10: Assemble chunked requests (supports multi-chunk POST)
               uint8_t *complete_data = nullptr;
               size_t complete_len = 0;
               if (!assembleChunkedRequest(data, len, index, total,
                                          &complete_data, &complete_len)) {
                 return; // Still accumulating chunks or error
               }

               if (!apiRateLimiterCheck(API_ENDPOINT_CONFIG, 0)) {
                 request->send(429, "application/json",
                               "{\"error\":\"Rate limit exceeded\"}");
                 return;
               }

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               DeserializationError error = deserializeJson(doc, complete_data, complete_len);

               if (error) {
                 request->send(400, "application/json",
                               "{\"error\":\"JSON parse failed\"}");
                 return;
               }

               int axis = doc["axis"] | -1;
               int ppm = doc["ppm"] | 0;

               if (axis < 0 || axis > 2 || ppm < 50 || ppm > 200) {
                 request->send(400, "application/json",
                               "{\"error\":\"Invalid axis or PPM value\"}");
                 return;
               }

               if (!apiConfigCalibrateEncoder((uint8_t)axis, (uint16_t)ppm)) {
                 request->send(400, "application/json",
                               "{\"error\":\"Failed to calibrate encoder\"}");
                 return;
               }

               if (!apiConfigSave()) {
                 request->send(500, "application/json",
                               "{\"error\":\"Failed to save configuration\"}");
                 return;
               }

               request->send(200, "application/json", "{\"success\":true}");
             });

  // POST /api/gcode - Execute G-code command
  server->on(
      "/api/gcode", HTTP_POST, nullptr, nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
             size_t index, size_t total) {
        if (!requireAuth(request)) return;

        // INPUT VALIDATION: Check body size to prevent overflow
        if (!validateBodySize(len, total)) {
          request->send(413, "application/json",
                        "{\"error\":\"Request body too large\"}");
          return;
        }

        // PHASE 5.10: Assemble chunked requests (supports multi-chunk POST)
        uint8_t *complete_data = nullptr;
        size_t complete_len = 0;
        if (!assembleChunkedRequest(data, len, index, total,
                                   &complete_data, &complete_len)) {
          return; // Still accumulating chunks or error
        }

        if (!apiRateLimiterCheck(API_ENDPOINT_JOG, 0)) { // Reuse jog rate limit
          request->send(429, "application/json",
                        "{\"error\":\"Rate limit exceeded\"}");
          return;
        }

        // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
        // fragmentation
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, complete_data, complete_len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"JSON parse failed\"}");
          return;
        }

        const char *command = doc["command"];
        if (!command || strlen(command) == 0) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing or empty command\"}");
          return;
        }

        // SECURITY FIX: Validate command against whitelist to prevent injection
        if (!isValidGcodeCommand(command)) {
          logWarning("[WEB] [SECURITY] Rejected invalid G-code command: %s", command);
          request->send(400, "application/json",
                        "{\"error\":\"Invalid or unauthorized G-code command\"}");
          return;
        }

        // Execute G-code command
        bool success = gcodeParser.processCommand(command);

        // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
        // fragmentation
        JsonDocument response;
        response["success"] = success;
        response["command"] = command;

        char response_buffer[256];
        serializeJson(response, response_buffer, sizeof(response_buffer));
        request->send(success ? 200 : 400, "application/json", response_buffer);
      });

  // GET /api/alarms - Get active alarms and fault history
  server->on("/api/alarms", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      request->send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
      return;
    }

    // MEMORY FIX: Use StaticJsonDocument as allocator sized for fault array +
    // stats
    JsonDocument doc;

    // E-stop status
    doc["estop_active"] = emergencyStopIsActive();

    // Fault ring buffer (recent faults)
    JsonArray faults = doc["faults"].to<JsonArray>();
    uint8_t count = faultGetRingBufferEntryCount();

    for (uint8_t i = 0; i < count && i < 10; i++) { // Max 10 recent faults
      const fault_entry_t *entry = faultGetRingBufferEntry(i);
      if (entry) {
        JsonObject fault = faults.add<JsonObject>();
        fault["timestamp"] = entry->timestamp;
        fault["severity"] = faultSeverityToString(entry->severity);
        fault["code"] = faultCodeToString(entry->code);
        fault["axis"] = entry->axis;
        fault["value"] = entry->value;
        fault["message"] = entry->message;
      }
    }

    // Fault statistics
    fault_stats_t stats = faultGetStats();
    doc["stats"]["total"] = stats.total_faults;
    doc["stats"]["encoder"] = stats.encoder_faults;
    doc["stats"]["motion"] = stats.motion_faults;
    doc["stats"]["safety"] = stats.safety_faults;

    char response[2048];
    serializeJson(doc, response, sizeof(response));
    request->send(200, "application/json", response);
  });

  // POST /api/estop/trigger - Trigger emergency stop
  server->on("/api/estop/trigger", HTTP_POST,
             [this](AsyncWebServerRequest *request) {
               if (!requireAuth(request)) return;

               emergencyStopSetActive(true);
               request->send(200, "application/json",
                             "{\"success\":true,\"estop_active\":true}");
             });

  // POST /api/estop/reset - Request E-stop recovery
  server->on("/api/estop/reset", HTTP_POST,
             [this](AsyncWebServerRequest *request) {
               if (!requireAuth(request)) return;

               bool success = emergencyStopRequestRecovery();

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               doc["success"] = success;
               doc["estop_active"] = emergencyStopIsActive();

               char response[128];
               serializeJson(doc, response, sizeof(response));
               request->send(success ? 200 : 400, "application/json", response);
             });

  // PHASE 5.10: Security - 404 handler already registered at line 224 with auth
  // Duplicate handler removed to prevent auth bypass

  // ============================================================================
  // HARDWARE PIN CONFIGURATION API
  // ============================================================================

  // GET /api/hardware/pins - Get all pins and signal definitions
  server->on("/api/hardware/pins", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    JsonDocument doc;
    doc["success"] = true;

    // Build signals array
    JsonArray signals = doc["signals"].to<JsonArray>();
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
      JsonObject sig = signals.add<JsonObject>();
      sig["key"] = signalDefinitions[i].key;
      sig["name"] = signalDefinitions[i].name;
      sig["desc"] = signalDefinitions[i].desc;
      sig["default_pin"] = signalDefinitions[i].default_gpio;
      sig["current_pin"] = getPin(signalDefinitions[i].key);
      sig["type"] = signalDefinitions[i].type;
    }

    // Build pins array
    JsonArray pins = doc["pins"].to<JsonArray>();
    for (size_t i = 0; i < PIN_COUNT; i++) {
      JsonObject pin = pins.add<JsonObject>();
      pin["gpio"] = pinDatabase[i].gpio;
      pin["silk"] = pinDatabase[i].silk;
      pin["type"] = pinDatabase[i].type;
      pin["voltage"] = pinDatabase[i].voltage;
      pin["note"] = pinDatabase[i].note;
      
      const char* assigned = checkPinConflict(pinDatabase[i].gpio, nullptr);
      if (assigned) {
        pin["assigned"] = assigned;
      }
    }

    char response[4096];
    size_t written = serializeJson(doc, response, sizeof(response));
    if (written >= sizeof(response)) {
      request->send(500, "application/json", "{\"error\":\"Buffer overflow\"}");
      return;
    }
    request->send(200, "application/json", response);
  });

  // POST /api/hardware/pins - Set a pin assignment
  server->on("/api/hardware/pins", HTTP_POST, 
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
      request->send(200);
    },
    NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
           size_t index, size_t total) {
      if (!validateBodySize(len, total)) {
        request->send(413, "application/json", "{\"error\":\"Request too large\"}");
        return;
      }

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, data, len);
      
      if (err) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }

      const char* key = doc["key"] | "";
      int8_t pin = doc["pin"] | -1;

      if (strlen(key) == 0 || pin < 0) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing key or pin\"}");
        return;
      }

      bool success = setPin(key, pin);
      
      JsonDocument resp;
      resp["success"] = success;
      if (!success) {
        resp["error"] = "Invalid pin assignment (conflict or type mismatch)";
      }

      char response[128];
      serializeJson(resp, response, sizeof(response));
      request->send(success ? 200 : 400, "application/json", response);
    });

  // POST /api/hardware/pins/save - Persist config and reboot
  server->on("/api/hardware/pins/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
    delay(100);
    ESP.restart();
  });

  // POST /api/hardware/pins/reset - Reset all pins to defaults
  server->on("/api/hardware/pins/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
      char nvs_key[40];
      safe_snprintf(nvs_key, sizeof(nvs_key), "pin_%s", signalDefinitions[i].key);
      configSetInt(nvs_key, -1);
    }
    configUnifiedSave();

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Reset to defaults. Rebooting...\"}");
    delay(100);
    ESP.restart();
  });

  // ==========================================================================
  // FILE MANAGER - Simple web-based file editor for development
  // ==========================================================================

  // GET /filemanager - Simple HTML file manager UI
  server->on("/filemanager", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 File Manager</title>
<style>
body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee}
h1{color:#10b981}
.file{display:flex;justify-content:space-between;padding:10px;margin:5px 0;background:#2a2a4e;border-radius:5px}
.file:hover{background:#3a3a5e}
.btn{padding:5px 10px;margin:0 2px;border:none;border-radius:3px;cursor:pointer}
.btn-dl{background:#3b82f6;color:#fff}
.btn-del{background:#ef4444;color:#fff}
.upload-form{margin:20px 0;padding:15px;background:#2a2a4e;border-radius:8px}
input[type=file]{margin:10px 0}
.btn-upload{background:#10b981;color:#fff;padding:10px 20px}
a{color:#10b981;text-decoration:none}
.size{color:#888;font-size:12px}
</style>
</head>
<body>
<h1>ESP32 File Manager</h1>
<div class="upload-form">
<form method="POST" action="/api/files/upload" enctype="multipart/form-data">
Path: <input type="text" name="path" value="/" style="width:200px;padding:5px">
<input type="file" name="file" required>
<button type="submit" class="btn btn-upload">Upload</button>
</form>
</div>
<div id="files">Loading...</div>
<script>
fetch('/api/files',{credentials:'include'}).then(r=>r.json()).then(data=>{
if(!data||!Array.isArray(data)){document.getElementById('files').innerHTML='<p>Error loading files</p>';return;}
let h='';
data.forEach(f=>{
if(f.size>0){
const path=f.path||f.name;
h+=`<div class="file"><span><a href="${path}" target="_blank">${path}</a> <span class="size">(${f.size} bytes)</span></span>
<span><a href="${path}" download class="btn btn-dl">Download</a>
<button onclick="del('${path}')" class="btn btn-del">Delete</button></span></div>`;
}
});
document.getElementById('files').innerHTML=h||'<p>No files</p>';
}).catch(e=>{document.getElementById('files').innerHTML='<p>Error: '+e+'</p>';});
function del(path){if(confirm('Delete '+path+'?'))fetch('/api/files?name='+encodeURIComponent(path),{method:'DELETE',credentials:'include'}).then(()=>location.reload());}
</script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html);
  });

  // GET /api/files/list - List all files
  server->on("/api/files/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    
    String json = "{\"files\":[";
    bool first = true;
    
    // Recursive file listing
    std::function<void(const char*)> listDir = [&](const char* dirname) {
      File root = LittleFS.open(dirname);
      if (!root || !root.isDirectory()) return;
      
      File file = root.openNextFile();
      while (file) {
        if (file.isDirectory()) {
          String subdir = String(dirname) + "/" + file.name();
          listDir(subdir.c_str());
        } else {
          if (!first) json += ",";
          first = false;
          String path = String(dirname);
          if (path != "/") path += "/";
          path += file.name();
          json += "{\"path\":\"" + path + "\",\"size\":" + String(file.size()) + "}";
        }
        file = root.openNextFile();
      }
    };
    
    listDir("/");
    json += "]}";
    request->send(200, "application/json", json);
  });

  // GET /api/files/download - Download a file
  server->on("/api/files/download", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!request->hasParam("path")) {
      request->send(400, "text/plain", "Missing path parameter");
      return;
    }
    String path = request->getParam("path")->value();
    if (!LittleFS.exists(path)) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    request->send(LittleFS, path, "application/octet-stream");
  });

  // DELETE /api/files/delete - Delete a file
  server->on("/api/files/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!request->hasParam("path")) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing path\"}");
      return;
    }
    String path = request->getParam("path")->value();
    if (LittleFS.remove(path)) {
      request->send(200, "application/json", "{\"success\":true}");
    } else {
      request->send(500, "application/json", "{\"success\":false,\"error\":\"Delete failed\"}");
    }
  });

  // POST /api/files/upload - Upload a file (multipart)
  server->on("/api/files/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
      request->send(200, "text/html", "<html><body><h2>Upload complete!</h2><a href='/filemanager'>Back to File Manager</a></body></html>");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      static File uploadFile;
      String path = "/";
      if (request->hasParam("path", true)) {
        path = request->getParam("path", true)->value();
      }
      if (path.endsWith("/")) path += filename;
      else if (path.indexOf('.') < 0) path += "/" + filename;
      
      if (index == 0) {
        logInfo("[FM] Upload: %s", path.c_str());
        uploadFile = LittleFS.open(path, "w");
      }
      if (uploadFile && len) {
        uploadFile.write(data, len);
      }
      if (final && uploadFile) {
        uploadFile.close();
        logInfo("[FM] Upload complete: %s (%u bytes)", path.c_str(), index + len);
      }
    });

  // 1. Static Files (Moved to end to allow API routes to match first)
  // No auth needed for static assets (bundled CSS/JS/HTML)
  server->serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html");
}

// --- Handlers ---

void WebServerManager::handleJogBody(AsyncWebServerRequest *request,
                                     uint8_t *data, size_t len, size_t index,
                                     size_t total) {
  // INPUT VALIDATION: Check body size to prevent overflow
  if (!validateBodySize(len, total)) {
    request->send(413, "application/json",
                  "{\"error\":\"Request body too large\"}");
    return;
  }

  // PHASE 5.10: Assemble chunked requests (supports multi-chunk POST)
  uint8_t *complete_data = nullptr;
  size_t complete_len = 0;
  if (!assembleChunkedRequest(data, len, index, total,
                              &complete_data, &complete_len)) {
    return; // Still accumulating chunks or error
  }

  // PHASE 5.1: Rate limiting check
  if (!apiRateLimiterCheck(API_ENDPOINT_JOG, 0)) {
    request->send(429, "application/json",
                  "{\"error\":\"Rate limit exceeded\"}");
    return;
  }

  // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
  // fragmentation
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, complete_data, complete_len);

  if (error) {
    logError("[WEB] JSON parse failed: %s", error.c_str());
    return;
  }

  const char *direction = doc["direction"];
  float distance = doc["distance"] | 10.0f;
  float speed = doc["speed"] | 50.0f;

  if (!direction)
    return;

  logDebug("[WEB] Jog: %s, %.1f mm, %.1f mm/s", direction, distance,
                speed);

  // Map direction strings to Motion API calls
  if (strcmp(direction, "X+") == 0)
    motionMoveRelative(distance, 0, 0, 0, speed);
  else if (strcmp(direction, "X-") == 0)
    motionMoveRelative(-distance, 0, 0, 0, speed);
  else if (strcmp(direction, "Y+") == 0)
    motionMoveRelative(0, distance, 0, 0, speed);
  else if (strcmp(direction, "Y-") == 0)
    motionMoveRelative(0, -distance, 0, 0, speed);
  else if (strcmp(direction, "Z+") == 0)
    motionMoveRelative(0, 0, distance, 0, speed);
  else if (strcmp(direction, "Z-") == 0)
    motionMoveRelative(0, 0, -distance, 0, speed);
  else if (strcmp(direction, "A+") == 0)
    motionMoveRelative(0, 0, 0, distance, speed);
  else if (strcmp(direction, "A-") == 0)
    motionMoveRelative(0, 0, 0, -distance, speed);
  else if (strcmp(direction, "STOP") == 0)
    motionStop();
}

void WebServerManager::handleFirmwareUpload(AsyncWebServerRequest *request,
                                            uint8_t *data, size_t len,
                                            size_t index, size_t total) {
  // PHASE 5.1: Handle firmware upload for OTA update

  // First chunk - start the update
  if (index == 0) {
    logInfo("[WEB] [OTA] Starting firmware upload: %zu bytes", total);

    if (!otaUpdaterStartUpdate(total, "firmware.bin")) {
      request->send(400, "application/json",
                    "{\"error\":\"Failed to start OTA update\"}");
      return;
    }
  }

  // Receive and write chunk
  if (!otaUpdaterReceiveChunk(data, len)) {
    request->send(400, "application/json",
                  "{\"error\":\"Failed to write firmware chunk\"}");
    otaUpdaterCancel();
    return;
  }

  // Last chunk - finalize update
  if (index + len >= total) {
    logInfo("[WEB] [OTA] Firmware upload complete, finalizing...");

    if (otaUpdaterFinalize()) {
      // Response will be sent after reboot timer expires
      request->send(200, "application/json",
                    "{\"status\":\"Firmware installed. Rebooting...\"}");
    } else {
      request->send(400, "application/json",
                    "{\"error\":\"Firmware validation failed\"}");
    }
  }
}

void WebServerManager::onWsEvent(AsyncWebSocket *server,
                                 AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data,
                                 size_t len) {
  if (type == WS_EVT_CONNECT) {
    logInfo("[WEB] WS Client #%u connected from %s", client->id(),
                  client->remoteIP().toString().c_str());
    broadcastState();
  } else if (type == WS_EVT_DISCONNECT) {
    logInfo("[WEB] WS Client #%u disconnected", client->id());
  }
}

void WebServerManager::broadcastState() {
  if (ws->count() == 0)
    return;

  // PHASE 5.10: Make atomic snapshot of current_status under spinlock
  // This prevents torn reads while building JSON
  decltype(current_status) status_snapshot;
  portENTER_CRITICAL(&statusSpinlock);
  memcpy(&status_snapshot, &current_status, sizeof(current_status));
  portEXIT_CRITICAL(&statusSpinlock);

  // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
  // fragmentation This function is called frequently (WebSocket broadcasts),
  // critical for stability
  JsonDocument doc;
  doc["status"] = status_snapshot.status;
  doc["x"] = status_snapshot.x_pos;
  doc["y"] = status_snapshot.y_pos;
  doc["z"] = status_snapshot.z_pos;
  doc["a"] = status_snapshot.a_pos;

  // VFD telemetry (PHASE 5.5)
  JsonObject vfd = doc["vfd"].to<JsonObject>();
  vfd["current_amps"] = status_snapshot.vfd_current_amps;
  vfd["frequency_hz"] = status_snapshot.vfd_frequency_hz;
  vfd["thermal_percent"] = status_snapshot.vfd_thermal_percent;
  vfd["fault_code"] = status_snapshot.vfd_fault_code;
  vfd["stall_threshold_amps"] = status_snapshot.vfd_threshold_amps;
  vfd["calibration_valid"] = status_snapshot.vfd_calibration_valid;

  // Axis metrics (PHASE 5.6) - per-axis
  JsonObject axis = doc["axis"].to<JsonObject>();
  JsonObject x_metrics = axis["x"].to<JsonObject>();
  x_metrics["quality"] = status_snapshot.axis_metrics[0].quality_score;
  x_metrics["jitter_mms"] = status_snapshot.axis_metrics[0].jitter_mms;
  x_metrics["stalled"] = status_snapshot.axis_metrics[0].stalled;
  x_metrics["vfd_error_percent"] =
      status_snapshot.axis_metrics[0].vfd_error_percent;

  JsonObject y_metrics = axis["y"].to<JsonObject>();
  y_metrics["quality"] = status_snapshot.axis_metrics[1].quality_score;
  y_metrics["jitter_mms"] = status_snapshot.axis_metrics[1].jitter_mms;
  y_metrics["stalled"] = status_snapshot.axis_metrics[1].stalled;
  y_metrics["vfd_error_percent"] =
      status_snapshot.axis_metrics[1].vfd_error_percent;

  JsonObject z_metrics = axis["z"].to<JsonObject>();
  z_metrics["quality"] = status_snapshot.axis_metrics[2].quality_score;
  z_metrics["jitter_mms"] = status_snapshot.axis_metrics[2].jitter_mms;
  z_metrics["stalled"] = status_snapshot.axis_metrics[2].stalled;
  z_metrics["vfd_error_percent"] =
      status_snapshot.axis_metrics[2].vfd_error_percent;

  size_t len =
      serializeJson(doc, json_response_buffer, sizeof(json_response_buffer));
  ws->textAll(json_response_buffer, len);
}

void WebServerManager::setSystemStatus(const char *status) {
  if (status) {
    // PHASE 5.10: Protect current_status with spinlock
    portENTER_CRITICAL(&statusSpinlock);
    strncpy(current_status.status, status, 31);
    current_status.status[31] = '\0';
    portEXIT_CRITICAL(&statusSpinlock);
  }
}

void WebServerManager::setAxisPosition(char axis, float position) {
  // PHASE 5.10: Protect current_status with spinlock
  portENTER_CRITICAL(&statusSpinlock);
  switch (axis) {
  case 'X':
    current_status.x_pos = position;
    break;
  case 'Y':
    current_status.y_pos = position;
    break;
  case 'Z':
    current_status.z_pos = position;
    break;
  case 'A':
    current_status.a_pos = position;
    break;
  }
  portEXIT_CRITICAL(&statusSpinlock);
}

void WebServerManager::setSystemUptime(uint32_t seconds) {
  // PHASE 5.10: Protect current_status with spinlock
  portENTER_CRITICAL(&statusSpinlock);
  current_status.uptime_sec = seconds;
  portEXIT_CRITICAL(&statusSpinlock);
}

// ============================================================================
// VFD TELEMETRY SETTERS (PHASE 5.5)
// ============================================================================

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

// ============================================================================
// AXIS METRICS SETTERS (PHASE 5.6) - Per-axis
// ============================================================================

void WebServerManager::setAxisQualityScore(uint8_t axis,
                                           uint32_t quality_score) {
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
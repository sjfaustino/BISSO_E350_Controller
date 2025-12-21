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
#include "config_keys.h"       // Configuration keys
#include "config_unified.h"    // NVS configuration
#include "dashboard_metrics.h" // PHASE 5.3: Web UI dashboard metrics
#include "encoder_diagnostics.h" // PHASE 5.3: Encoder health monitoring
#include "fault_logging.h"       // PHASE 1: Alarm and E-stop management
#include "gcode_parser.h"        // PHASE 1: G-code execution
#include "load_manager.h"        // PHASE 5.3: Graceful degradation under load
#include "motion.h"
#include "motion_state.h" // Read-only state access
#include "openapi.h"      // PHASE 6: OpenAPI/Swagger specification generation
#include "spindle_current_monitor.h" // PHASE 5.1: Spindle telemetry
#include "string_safety.h"           // Safe string operations
#include "system_telemetry.h" // PHASE 5.1: Comprehensive system telemetry
#include "task_performance_monitor.h" // PHASE 5.1: Task performance metrics
#include "vfd_current_calibration.h"  // PHASE 5.5: Current calibration
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Global instance
WebServerManager webServer(80);

// Credentials loaded from NVS (PHASE 5.1: Security hardening)
static char http_username[CONFIG_VALUE_LEN] = "admin";
static char http_password[CONFIG_VALUE_LEN] = "password";
static bool password_change_enforced = false;

// Security Constants
#define MAX_REQUEST_BODY_SIZE 8192  // Maximum size for POST body (8KB)
#define MAX_CONFIG_KEY_LENGTH 64    // Maximum length for config keys
#define MAX_CONFIG_VALUE_LENGTH 256 // Maximum length for config values

/**
 * @brief Check authentication and send 401 if failed
 * @param request The async web request
 * @return true if authenticated, false if authentication was requested
 * @note Helper function to reduce code duplication
 */
static bool requireAuth(AsyncWebServerRequest *request) {
  if (!request->authenticate(http_username, http_password)) {
    request->requestAuthentication();
    return false;
  }
  return true;
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

// Telemetry Buffer
static char json_response_buffer[WEB_BUFFER_SIZE];

WebServerManager::WebServerManager(uint16_t port)
    : server(nullptr), ws(nullptr), port(port) {
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
  if (server)
    delete server;
  if (ws)
    delete ws;
}

// Load credentials from NVS (PHASE 5.1: Security hardening)
void WebServerManager::loadCredentials() {
  const char *user = configGetString(KEY_WEB_USERNAME, "admin");
  const char *pass = configGetString(KEY_WEB_PASSWORD, "password");
  int pw_changed = configGetInt(KEY_WEB_PW_CHANGED, 0);

  strncpy(http_username, user, CONFIG_VALUE_LEN - 1);
  http_username[CONFIG_VALUE_LEN - 1] = '\0';
  strncpy(http_password, pass, CONFIG_VALUE_LEN - 1);
  http_password[CONFIG_VALUE_LEN - 1] = '\0';

  if (pw_changed == 0) {
    // SECURITY WARNING: Default credentials in use
    Serial.println();
    Serial.println(
        "╔══════════════════════════════════════════════════════════════╗");
    Serial.println(
        "║  ⚠️  SECURITY WARNING: DEFAULT CREDENTIALS IN USE!  ⚠️       ║");
    Serial.println(
        "╠══════════════════════════════════════════════════════════════╣");
    Serial.println(
        "║  Username: admin    Password: password                       ║");
    Serial.println(
        "║                                                              ║");
    Serial.println(
        "║  Change immediately using CLI command:                       ║");
    Serial.println(
        "║    web_setpass <new_password>                                ║");
    Serial.println(
        "║                                                              ║");
    Serial.println(
        "║  Password must be at least 8 characters.                     ║");
    Serial.println(
        "╚══════════════════════════════════════════════════════════════╝");
    Serial.println();
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

// Minimum password length for security
#define MIN_PASSWORD_LENGTH 8

// Set new password (for CLI command)
void WebServerManager::setPassword(const char *new_password) {
  if (!new_password || strlen(new_password) < MIN_PASSWORD_LENGTH) {
    Serial.printf("[WEB] [ERR] Password must be at least %d characters\n",
                  MIN_PASSWORD_LENGTH);
    return;
  }

  // Check password doesn't match common weak passwords
  const char *weak_passwords[] = {"password", "12345678", "admin123",
                                  "qwerty12", "00000000"};
  for (int i = 0; i < 5; i++) {
    if (strcmp(new_password, weak_passwords[i]) == 0) {
      Serial.println(
          "[WEB] [ERR] Password too weak - choose a stronger password");
      return;
    }
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
  ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
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
  // PHASE 5.6: Initialize configuration API
  apiConfigInit();

  // 1. Static Files (Protected)
  server->serveStatic("/", SPIFFS, "/")
      .setDefaultFile("index.html")
      .setAuthentication(http_username, http_password);

  // 2. API Status (Protected, Rate Limited)
  server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      request->send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
      return;
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

    char response[256];
    serializeJson(doc, response, sizeof(response));
    request->send(200, "application/json", response);
  });

  // 3. API Jog (Protected, Rate Limited)
  server
      ->on(
          "/api/jog", HTTP_POST,
          [](AsyncWebServerRequest *request) { request->send(200); }, NULL,
          [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                 size_t index, size_t total) {
            this->handleJogBody(request, data, len, index, total);
          })
      .setAuthentication(http_username, http_password);

  // 4. API Spindle Telemetry (Protected, Rate Limited) - PHASE 5.1
  server->on("/api/spindle", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_SPINDLE, 0)) {
      request->send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
      return;
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

    char response[512];
    serializeJson(doc, response, sizeof(response));
    request->send(200, "application/json", response);
  });

  // 5. API Task Performance Metrics (Protected, Rate Limited) - PHASE 5.1
  server->on("/api/metrics", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    // PHASE 5.1: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      request->send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
      return;
    }

    // MEMORY FIX: Stack allocation - 2KB is safe for async handlers (8KB stack)
    char metrics_buffer[2048];
    size_t metrics_size = perfMonitorExportJSON(metrics_buffer, sizeof(metrics_buffer));
    if (metrics_size == 0) {
      request->send(500, "application/json",
                    "{\"error\":\"Failed to export metrics\"}");
      return;
    }

    request->send(200, "application/json", metrics_buffer);
  });

  // 6. API OTA Firmware Update (Protected, Large Upload) - PHASE 5.1
  server->on("/api/update/status", HTTP_GET,
             [this](AsyncWebServerRequest *request) {
               if (!requireAuth(request)) return;

               // Stack allocation - 512 bytes is safe for async handlers
               char status_buffer[512];
               otaUpdaterExportJSON(status_buffer, sizeof(status_buffer));
               request->send(200, "application/json", status_buffer);
             });

  server
      ->on(
          "/api/update", HTTP_POST,
          [](AsyncWebServerRequest *request) {
            request->send(202, "application/json",
                          "{\"status\":\"Upload in progress...\"}");
          },
          NULL,
          [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                 size_t index, size_t total) {
            this->handleFirmwareUpload(request, data, len, index, total);
          })
      .setAuthentication(http_username, http_password);

  // 7. API Comprehensive System Telemetry (Protected, Rate Limited) - PHASE 5.1
  server->on(
      "/api/telemetry", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;

        // PHASE 5.1: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
          request->send(429, "application/json",
                        "{\"error\":\"Rate limit exceeded\"}");
          return;
        }

        // MEMORY FIX: Stack allocation - 3KB is safe for async handlers (8KB stack)
        char telemetry_buffer[3072];
        size_t telemetry_size = telemetryExportJSON(telemetry_buffer, sizeof(telemetry_buffer));
        if (telemetry_size == 0) {
          request->send(500, "application/json",
                        "{\"error\":\"Failed to export telemetry\"}");
          return;
        }

        request->send(200, "application/json", telemetry_buffer);
      });

  // Lightweight telemetry for high-frequency polling
  server->on("/api/telemetry/compact", HTTP_GET,
             [this](AsyncWebServerRequest *request) {
               if (!requireAuth(request)) return;

               // Stack allocation - 512 bytes is safe for async handlers
               char compact_buffer[512];
               telemetryExportCompactJSON(compact_buffer,
                                          sizeof(compact_buffer));
               request->send(200, "application/json", compact_buffer);
             });

  // 8. API Endpoint Discovery (Unprotected for auto-discovery) - PHASE 5.2
  server->on(
      "/api/endpoints", HTTP_GET, [this](AsyncWebServerRequest *request) {
        char *endpoints_buffer = (char *)malloc(4096);
        if (!endpoints_buffer) {
          request->send(500, "application/json",
                        "{\"error\":\"Memory allocation failed\"}");
          return;
        }

        size_t endpoints_size = apiEndpointsExportJSON(endpoints_buffer, 4096);
        if (endpoints_size == 0) {
          free(endpoints_buffer);
          request->send(500, "application/json",
                        "{\"error\":\"Failed to export endpoints\"}");
          return;
        }

        request->send(200, "application/json", endpoints_buffer);
        free(endpoints_buffer);
      });

  // 8.5. OpenAPI Specification (Unprotected for Swagger UI) - PHASE 6
  server->on(
      "/api/openapi.json", HTTP_GET, [this](AsyncWebServerRequest *request) {
        char *openapi_buffer = (char *)malloc(8192);
        if (!openapi_buffer) {
          request->send(500, "application/json",
                        "{\"error\":\"Memory allocation failed\"}");
          return;
        }

        size_t openapi_size = openAPIGenerateJSON(openapi_buffer, 8192);
        if (openapi_size == 0) {
          free(openapi_buffer);
          request->send(500, "application/json",
                        "{\"error\":\"Failed to generate OpenAPI spec\"}");
          return;
        }

        if (!openAPIValidate(openapi_buffer)) {
          free(openapi_buffer);
          request->send(500, "application/json",
                        "{\"error\":\"Generated invalid OpenAPI spec\"}");
          return;
        }

        request->send(200, "application/json", openapi_buffer);
        free(openapi_buffer);
      });

  // 8.6. Swagger UI Documentation (Unprotected for discovery) - PHASE 6
  server->on("/api/docs", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/pages/swagger-ui.html", "text/html");
  });

  // 9. API Health Check (Protected, Rate Limited) - PHASE 5.2
  server->on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    // PHASE 5.2: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      request->send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
      return;
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

    snprintf(health_buffer, sizeof(health_buffer),
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

    request->send(200, "application/json", health_buffer);
  });

  // 10. PHASE 5.3: API Encoder Diagnostics (Protected, Rate Limited)
  server->on(
      "/api/encoder/diagnostics", HTTP_GET,
      [this](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;

        // PHASE 5.3: Rate limiting check
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
          request->send(429, "application/json",
                        "{\"error\":\"Rate limit exceeded\"}");
          return;
        }

        // MEMORY FIX: Stack allocation - 2KB is safe for async handlers (8KB stack)
        char diag_buffer[2048];
        size_t diag_size = encoderDiagnosticsExportJSON(diag_buffer, sizeof(diag_buffer));
        if (diag_size == 0) {
          request->send(500, "application/json",
                        "{\"error\":\"Failed to export encoder diagnostics\"}");
          return;
        }

        request->send(200, "application/json", diag_buffer);
      });

  // 11. PHASE 5.3: API Load Status (Protected, Rate Limited)
  server->on("/api/load", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    // PHASE 5.3: Rate limiting check
    if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
      request->send(429, "application/json",
                    "{\"error\":\"Rate limit exceeded\"}");
      return;
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

    snprintf(load_buffer, sizeof(load_buffer),
             "{\"state\":\"%s\",\"cpu_percent\":%d,\"time_in_state_ms\":%lu,"
             "\"state_changed\":%s,\"emergency_estop\":%s}",
             state_str, load_status.current_cpu_percent,
             (unsigned long)load_status.time_in_state_ms,
             load_status.state_changed ? "true" : "false",
             load_status.emergency_estop_initiated ? "true" : "false");

    request->send(200, "application/json", load_buffer);
  });

  // 12. PHASE 5.3: API Dashboard Metrics (Protected, for Web UI)
  server->on(
      "/api/dashboard/metrics", HTTP_GET,
      [this](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;

        // PHASE 5.3: Rate limiting check (less strict for dashboard updates)
        if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
          request->send(429, "application/json",
                        "{\"error\":\"Rate limit exceeded\"}");
          return;
        }

        // Stack allocation - 512 bytes is safe for async handlers
        char metrics_buffer[512];
        size_t metrics_size =
            dashboardMetricsExportJSON(metrics_buffer, sizeof(metrics_buffer));
        if (metrics_size == 0) {
          request->send(500, "application/json",
                        "{\"error\":\"Failed to export dashboard metrics\"}");
          return;
        }

        request->send(200, "application/json", metrics_buffer);
      });

  // 13. DELEGATE FILE MANAGEMENT
  // Registers /api/files (GET, DELETE) and /api/upload (POST)
  apiRegisterFileRoutes(server, http_username, http_password);

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
        serializeJson(doc, response, sizeof(response));
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

               if (!apiRateLimiterCheck(API_ENDPOINT_CONFIG, 0)) {
                 request->send(429, "application/json",
                               "{\"error\":\"Rate limit exceeded\"}");
                 return;
               }

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               DeserializationError error = deserializeJson(doc, data, len);

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

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               DeserializationError error = deserializeJson(doc, data, len);

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
        serializeJson(doc, response, sizeof(response));
        request->send(200, "application/json", response);
      });

  // POST /api/encoder/calibrate - Calibrate encoder axis
  server->on("/api/encoder/calibrate", HTTP_POST, nullptr, nullptr,
             [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                    size_t index, size_t total) {
               if (!requireAuth(request)) return;

               if (!apiRateLimiterCheck(API_ENDPOINT_CONFIG, 0)) {
                 request->send(429, "application/json",
                               "{\"error\":\"Rate limit exceeded\"}");
                 return;
               }

               // MEMORY FIX: Use StaticJsonDocument as allocator to prevent
               // heap fragmentation
               JsonDocument doc;
               DeserializationError error = deserializeJson(doc, data, len);

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

        if (!apiRateLimiterCheck(API_ENDPOINT_JOG, 0)) { // Reuse jog rate limit
          request->send(429, "application/json",
                        "{\"error\":\"Rate limit exceeded\"}");
          return;
        }

        // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
        // fragmentation
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

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

  server->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });
}

// --- Handlers ---

void WebServerManager::handleJogBody(AsyncWebServerRequest *request,
                                     uint8_t *data, size_t len, size_t index,
                                     size_t total) {
  // PHASE 5.1: Rate limiting check
  if (!apiRateLimiterCheck(API_ENDPOINT_JOG, 0)) {
    request->send(429, "application/json",
                  "{\"error\":\"Rate limit exceeded\"}");
    return;
  }

  // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
  // fragmentation
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    Serial.printf("[WEB] [ERR] JSON parse failed: %s\n", error.c_str());
    return;
  }

  const char *direction = doc["direction"];
  float distance = doc["distance"] | 10.0f;
  float speed = doc["speed"] | 50.0f;

  if (!direction)
    return;

  Serial.printf("[WEB] Jog: %s, %.1f mm, %.1f mm/s\n", direction, distance,
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
    Serial.printf("[WEB] [OTA] Starting firmware upload: %zu bytes\n", total);

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
    Serial.println("[WEB] [OTA] Firmware upload complete, finalizing...");

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
    Serial.printf("[WEB] WS Client #%u connected from %s\n", client->id(),
                  client->remoteIP().toString().c_str());
    broadcastState();
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WEB] WS Client #%u disconnected\n", client->id());
  }
}

void WebServerManager::broadcastState() {
  if (ws->count() == 0)
    return;

  // MEMORY FIX: Use StaticJsonDocument as allocator to prevent heap
  // fragmentation This function is called frequently (WebSocket broadcasts),
  // critical for stability
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
  x_metrics["vfd_error_percent"] =
      current_status.axis_metrics[0].vfd_error_percent;

  JsonObject y_metrics = axis["y"].to<JsonObject>();
  y_metrics["quality"] = current_status.axis_metrics[1].quality_score;
  y_metrics["jitter_mms"] = current_status.axis_metrics[1].jitter_mms;
  y_metrics["stalled"] = current_status.axis_metrics[1].stalled;
  y_metrics["vfd_error_percent"] =
      current_status.axis_metrics[1].vfd_error_percent;

  JsonObject z_metrics = axis["z"].to<JsonObject>();
  z_metrics["quality"] = current_status.axis_metrics[2].quality_score;
  z_metrics["jitter_mms"] = current_status.axis_metrics[2].jitter_mms;
  z_metrics["stalled"] = current_status.axis_metrics[2].stalled;
  z_metrics["vfd_error_percent"] =
      current_status.axis_metrics[2].vfd_error_percent;

  size_t len =
      serializeJson(doc, json_response_buffer, sizeof(json_response_buffer));
  ws->textAll(json_response_buffer, len);
}

void WebServerManager::setSystemStatus(const char *status) {
  if (status) {
    strncpy(current_status.status, status, 31);
    current_status.status[31] = '\0';
  }
}

void WebServerManager::setAxisPosition(char axis, float position) {
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

void WebServerManager::setAxisQualityScore(uint8_t axis,
                                           uint32_t quality_score) {
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
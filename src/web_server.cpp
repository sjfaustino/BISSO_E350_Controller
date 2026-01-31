/**
 * @file web_server.cpp
 * @brief Web Server Implementation with REST API
 * @details Implements static file serving and REST API for configuration.
 */

#include "web_server.h"
#include "gcode_parser.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "motion_buffer.h"  // For buffer telemetry
#include "motion_state.h" // Added for execution tracking
#include "api_config.h"
#include "serial_logger.h"
#include "hardware_config.h"
#include "plc_iface.h"
#include "board_inputs.h"
#include <ETH.h>
#include "gcode_parser.h"
#include "system_telemetry.h"
#include "fault_logging.h"
#include "spindle_current_monitor.h"
#include "encoder_wj66.h"
#include "rs485_autodetect.h"
#include "config_keys.h"
#include "gcode_queue.h"
#include "ota_manager.h"
#include "mcu_info.h"
#include "yhtc05_modbus.h"
#include "firmware_version.h"
#include "boot_validation.h"
#include "api_routes.h"
#include <PsychicJson.h>
#include "lcd_interface.h"  // Added for LCD mirroring
#include "lcd_message.h"    // Added for M117 telemetry
#include "task_manager.h"   // Added for Stack monitoring

// Telemetry History Buffer (last 60 samples, sampled every 5s = 5mins)
#define HISTORY_BUFFER_SIZE 60
struct history_sample_t {
    uint8_t cpu;
    uint32_t heap;
    float spindle;
};
history_sample_t telemetry_history[HISTORY_BUFFER_SIZE];
int history_head = 0;
int history_count = 0;
static uint32_t last_history_sample_ms = 0;

// Forward Declaration
bool webAuthenticate(PsychicRequest *request);

void updateHistory(uint8_t cpu, uint32_t heap, float spindle) {
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
    otaInit();
    
    // TUNE: Increase parallel sockets to 6 for Dashboard stability (many assets + WS)
    server.config.stack_size = 8192;
    server.config.max_open_sockets = 6; // Allow 6 parallel (browser standard)
    server.config.lru_purge_enable = true;
    server.config.send_wait_timeout = 15; // More headroom for chunked deliveries
    server.config.recv_wait_timeout = 15;

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
    
    // Register all API routes (extracted for maintainability)
    setupRoutes();
    
    // Initialize status cache
    memset(&current_status, 0, sizeof(current_status));
    strncpy(current_status.status, "IDLE", sizeof(current_status.status));
}

// ============================================================================
// JSON RESPONSE HELPER (P2 DRY Improvement)
// ============================================================================

/**
 * @brief Send JSON response with proper content type
 * @param response PsychicHttp response object
 * @param doc ArduinoJson document to serialize
 * @param status HTTP status code (default 200)
 * @return esp_err_t result
 */
esp_err_t sendJsonResponse(PsychicResponse* response, JsonDocument& doc, int status) {
    response->setCode(status);
    response->setContentType("application/json");

    // Start chunking and send headers
    response->sendHeaders();

    // Use a small stack buffer for the chunk printer to avoid heap churn
    // Typical JSON responses are < 2KB, so 1KB chunks are efficient.
    uint8_t buffer[1024];
    ChunkPrinter printer(response, buffer, sizeof(buffer));

    // Serialize directly to the chunk printer (streaming)
    serializeJson(doc, printer);

    // Finalize the response
    printer.flush();
    return response->finishChunking();
}

// ============================================================================
// ROUTE REGISTRATION (P0 Refactor: Extracted from init())
// ============================================================================
void WebServerManager::setupRoutes() {
    // --- API Routes (delegated to modular files) ---
    registerTelemetryRoutes(server);
    registerGcodeRoutes(server);
    registerMotionRoutes(server);
    registerNetworkRoutes(server);
    registerHardwareRoutes(server);
    registerSystemRoutes(server);
    
    logPrintln("[WEB] All API routes registered");
    
    
    // --- WebSocket Handler for real-time telemetry ---
    // --- WebSocket Handler for real-time telemetry ---
    wsHandler.onOpen([this](PsychicWebSocketClient *client) {
        // logPrintf("[WS] Client connected: %s\n", client->remoteIP().toString().c_str());
        
        // PHASE 6.3: Memory Optimization
        // DO NOT send initial state here. It causes heap churn during page navigation.
        // The client will pick up the next periodic broadcast (within 500ms).
        // This elimiantes a ~2KB allocation per page load.
    });
    
    wsHandler.onClose([this](PsychicWebSocketClient *client) {
        // logPrintf("[WS] Client disconnected: %s\n", client->remoteIP().toString().c_str());
    });
    
    wsHandler.onFrame([this](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        // Handle incoming messages (commands from web UI)
        if (frame->type == HTTPD_WS_TYPE_TEXT) {
            String msg = String((char*)frame->payload, frame->len);
            
            // Parse JSON for commands
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, msg);
            
            if (!error) {
                const char* type = doc["type"] | "";
                
                // Handle Ping/Pong for latency tracking
                if (strcmp(type, "ping") == 0) {
                    // Send immediate Pong
                    request->reply("{\"type\":\"pong\"}");
                    return ESP_OK;
                }
            }

            // Filter out heartbeat pings to keep logs clean
            if (msg.indexOf("ping") == -1) {
                logPrintf("[WS] Received: %s\r\n", msg.c_str());
            }
        }
        return ESP_OK;
    });
    
    server.on("/ws", &wsHandler);
    logPrintln("[WEB] WebSocket handler registered at /ws");
    
    // Serve static files from root (MUST be after API routes)
    // PHASE 6: Enable browser caching to reduce load on LittleFS
    // --- Static File Routing ---
    
    // Noise suppressor: Intercept /bootlog.txt requests to prevent VFS "[E] open() failed" logs
    server.on("/bootlog.txt", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        if (!LittleFS.exists("/bootlog.txt")) {
            return response->send(404, "text/plain", "Boot log missing");
        }
        PsychicFileResponse streamResponse(response, LittleFS, "/bootlog.txt");
        return streamResponse.send();
    });

    // Main static handler (serves from LittleFS /)
    server.serveStatic("/", LittleFS, "/", "public, max-age=3600");
}

void WebServerManager::begin() {
    logPrintln("[WEB] Starting Server");
    
    // PHASE 6.2: Memory-safe configuration (Optimized for standard browser parallelism)
    // 4 sockets provide a good balance between concurrency and heap usage.
    server.config.max_open_sockets = 4;
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

void WebServerManager::setSpindleEfficiency(float load_ratio) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.spindle_efficiency = load_ratio;
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

void WebServerManager::setAxisMaintenanceWarning(uint8_t axis, bool warned) {
    if (axis < 3) {
        portENTER_CRITICAL(&statusSpinlock);
        current_status.axis_metrics[axis].maintenance_warning = warned;
        portEXIT_CRITICAL(&statusSpinlock);
    }
}

// --- JSON Builder Helper ---
// --- JSON Builder Helper is now replaced by serializeTelemetryToBuffer ---


// PHASE 6.4: String-based JSON serialization to eliminate heap churn
size_t WebServerManager::serializeTelemetryToBuffer(char* buffer, size_t buffer_size, const system_telemetry_t& telemetry, bool full) {
    if (!buffer || buffer_size < 1024) return 0;

    char ver_str[32];
    snprintf(ver_str, sizeof(ver_str), "v%d.%d.%d", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);

    lcd_message_t custom_msg;
    bool has_lcd_msg = lcdMessageGet(&custom_msg);

    char lcd_lines[LCD_ROWS][LCD_COLS + 1];
    lcdInterfaceGetContent(lcd_lines);

    bool moving = motionIsMoving();
    uint8_t active_axis = motionGetActiveAxis();
    float req_feedrate = gcodeParser.getCurrentFeedRate();
    float actual_feedrate = (active_axis < MOTION_AXES) ? motionGetCalibratedFeedRate(active_axis, req_feedrate / 60.0f) : req_feedrate;

    char rev_str[16];
    mcuGetRevisionString(rev_str, sizeof(rev_str));

    char serial_str[32];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(serial_str, sizeof(serial_str), "BS-E350-%02X%02X", (uint8_t)(mac >> 8), (uint8_t)mac);

    portENTER_CRITICAL(&statusSpinlock);

    int n = snprintf(buffer, buffer_size,
        "{\"system\":{\"status\":\"%s\",\"health\":\"%s\",\"uptime_sec\":%lu,\"cpu_percent\":%u,\"free_heap_bytes\":%lu,\"temperature\":%.1f,"
        "\"firmware_version\":\"%s\",\"build_date\":\"%s\",\"lcd_msg\":\"%s\",\"lcd_msg_id\":%llu%s%s%s%s%s%s%s%s%s%s%s},"
        "\"x_mm\":%.3f,\"y_mm\":%.3f,\"z_mm\":%.3f,\"a_mm\":%.3f,"
        "\"motion_active\":%s,\"motion\":{\"moving\":%s,\"buffer_count\":%d,\"buffer_capacity\":%d,\"dro_connected\":%s},"
        "\"vfd\":{\"current_amps\":%.2f,\"frequency_hz\":%.2f,\"thermal_percent\":%d,\"fault_code\":%u,"
        "\"stall_threshold\":%.2f,\"calibration_valid\":%s,\"connected\":%s,\"rpm\":%.1f,\"speed_m_s\":%.2f,\"efficiency\":%.2f},"
        "\"axis\":{\"x\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s},"
        "\"y\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s},"
        "\"z\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s}},"
        "\"network\":{\"wifi_connected\":%s,\"signal_percent\":%u},"
        "\"parser\":{\"absolute_mode\":%s,\"feedrate\":%.1f,\"actual_feedrate\":%.1f},"
        "\"lcd\":{\"lines\":[\"%s\",\"%s\",\"%s\",\"%s\"]}",
        current_status.status,
        telemetryGetHealthStatusString(telemetry.health_status),
        (unsigned long)current_status.uptime_sec,
        telemetry.cpu_usage_percent,
        (unsigned long)telemetry.free_heap_bytes,
        telemetry.temperature,
        ver_str,
        __DATE__,
        has_lcd_msg ? (const char*)custom_msg.text : "",
        has_lcd_msg ? (unsigned long long)custom_msg.timestamp_ms : 0ULL,
        full ? (telemetry.plc_hardware_present ? ",\"plc_hardware_present\":true" : ",\"plc_hardware_present\":false") : "",
        full ? ",\"hw_model\":\"BISSO E350\"" : "",
        full ? ",\"hw_mcu\":\"" : "", full ? mcuGetModelName() : "", full ? "\"" : "",
        full ? ",\"hw_revision\":\"" : "", full ? rev_str : "", full ? "\"" : "",
        full ? ",\"hw_serial\":\"" : "", full ? serial_str : "", full ? "\"" : "",
        telemetry.axis_x_mm, telemetry.axis_y_mm, telemetry.axis_z_mm, telemetry.axis_a_mm,
        moving ? "true" : "false",
        moving ? "true" : "false",
        motionBuffer.available(),
        motionBuffer.getCapacity(),
        current_status.dro_connected ? "true" : "false",
        current_status.vfd_current_amps, current_status.vfd_frequency_hz, current_status.vfd_thermal_percent, current_status.vfd_fault_code,
        current_status.vfd_threshold_amps, current_status.vfd_calibration_valid ? "true" : "false", current_status.vfd_connected ? "true" : "false",
        current_status.spindle_rpm, current_status.spindle_speed_m_s, current_status.spindle_efficiency,
        current_status.axis_metrics[0].quality_score, current_status.axis_metrics[0].jitter_mms, current_status.axis_metrics[0].vfd_error_percent, current_status.axis_metrics[0].quality_score < 10 ? "true" : "false", current_status.axis_metrics[0].maintenance_warning ? "true" : "false",
        current_status.axis_metrics[1].quality_score, current_status.axis_metrics[1].jitter_mms, current_status.axis_metrics[1].vfd_error_percent, current_status.axis_metrics[1].quality_score < 10 ? "true" : "false", current_status.axis_metrics[1].maintenance_warning ? "true" : "false",
        current_status.axis_metrics[2].quality_score, current_status.axis_metrics[2].jitter_mms, current_status.axis_metrics[2].vfd_error_percent, current_status.axis_metrics[2].quality_score < 10 ? "true" : "false", current_status.axis_metrics[2].maintenance_warning ? "true" : "false",
        telemetry.wifi_connected ? "true" : "false",
        telemetry.wifi_signal_strength,
        (gcodeParser.getDistanceMode() == G_MODE_ABSOLUTE) ? "true" : "false",
        req_feedrate,
        actual_feedrate,
        lcd_lines[0], lcd_lines[1], lcd_lines[2], lcd_lines[3]
    );

    portEXIT_CRITICAL(&statusSpinlock);

    if (n < 0 || (size_t)n >= buffer_size) return (size_t)n;

    size_t offset = (size_t)n;

    // Execution status if moving
    if (moving) {
        offset += snprintf(buffer + offset, buffer_size - offset,
            ",\"exec\":{\"cmd\":\"%s\",\"progress\":%.1f,\"eta\":%lu}",
            motionGetCurrentCommand(),
            motionGetExecutionProgress(),
            (unsigned long)motionGetEstimatedTimeRemaining());
    }

    // Stack Monitor
    task_stats_t* stats = taskGetStatsArray();
    int stats_count = taskGetStatsCount();
    if (stats != nullptr) {
        offset += snprintf(buffer + offset, buffer_size - offset, ",\"stack\":{");
        bool first = true;
        for (int i = 0; i < stats_count; i++) {
            if (stats[i].name != nullptr) {
                offset += snprintf(buffer + offset, buffer_size - offset, "%s\"%s\":%u", first ? "" : ",", stats[i].name, stats[i].stack_high_water);
                first = false;
                if (offset >= buffer_size - 10) break;
            }
        }
        offset += snprintf(buffer + offset, buffer_size - offset, "}");
    }

    // Close Root
    if (offset < buffer_size - 1) {
        buffer[offset++] = '}';
        buffer[offset] = '\0';
    }

    return offset;
}

// --- Broadcast state to all connected WebSocket clients ---
void WebServerManager::broadcastState() {
    // PHASE 6.2: Skip broadcast if heap is critically fragmented
    // This gives the heap time to recover during file serving
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest_block < 10240) { // Increased to 10KB to prevent fragmentation issues
        // Heap is too fragmented - skip this broadcast cycle
        return;
    }
    
    // Get fresh telemetry snapshot
    system_telemetry_t telemetry = telemetryGetSnapshot();
    
    // PHASE 6.4: String-based serialization (No heap churn)
    char buffer[2048];
    size_t len = serializeTelemetryToBuffer(buffer, sizeof(buffer), telemetry, false);
    
    if (len > 0 && len < sizeof(buffer)) {
        // Wrap in try-catch to prevent crash if clients disconnect during broadcast
        try {
            wsHandler.sendAll(buffer);
        } catch (...) {
            logWarning("[WS] Broadcast failed - client disconnected");
        }
    } else if (len >= sizeof(buffer)) {
        logError("[WS] Broadcast failed - Buffer overflow (%u bytes)", (uint32_t)len);
    }

    // Update history tracking
    updateHistory(telemetry.cpu_usage_percent, telemetry.free_heap_bytes, current_status.vfd_current_amps);
}

// --- Credentials Stubs ---
void WebServerManager::loadCredentials() {}
void WebServerManager::setPassword(const char* new_password) {}
bool WebServerManager::isPasswordChangeRequired() { return false; }

// --- Legacy Support ---
void WebServerManager::handleClient() {}

// --- Authentication Helper ---
bool webAuthenticate(PsychicRequest *request) {
    // Check if auth is enabled (default: true)
    if (configGetInt(KEY_WEB_AUTH_ENABLED, 1) == 0) {
        return true;
    }

    // Get credentials from config
    String username = configGetString(KEY_WEB_USERNAME, "admin");
    String password = configGetString(KEY_WEB_PASSWORD, "bisso");
    
    // Use PsychicHttp's built-in digest authentication
    // Note: authenticate() checks if the request has valid credentials
    // If not, it sends the 401 challenge specific to Digest Auth
    return request->authenticate(username.c_str(), password.c_str());
}
// getWebSocketHandler is defined inline in web_server.h

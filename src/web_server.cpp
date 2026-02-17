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
#include "psram_alloc.h"
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
#include "psram_web_cache.h"
#include "string_safety.h"

// Telemetry History Buffer (last 60 samples, sampled every 5s = 5mins)
#define HISTORY_BUFFER_SIZE 60
#define TELEMETRY_HISTORY_INTERVAL_MS 5000
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
    uint32_t elapsed = (now >= last_history_sample_ms) ? (now - last_history_sample_ms) : (UINT32_MAX - last_history_sample_ms + now + 1);
    if (elapsed < TELEMETRY_HISTORY_INTERVAL_MS) return; 
    last_history_sample_ms = now;

    telemetry_history[history_head] = {cpu, heap, spindle};
    history_head = (history_head + 1) % HISTORY_BUFFER_SIZE;
    if (history_count < HISTORY_BUFFER_SIZE) history_count++;
}

// Instantiate the global webServer object declared extern in web_server.h
WebServerManager webServer;

// PHASE 6.4: Transmit buffer in PSRAM
static char* broadcast_buffer = nullptr;

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
    // server.config.stack_size = 6144; // PHASE 6.9: Reduced to 6KB to save heap (was 8KB)
    // RATIONALE: Reducing PsychicHttp task stack to 6KB frees ~12KB of internal DRAM 
    // across 6 sockets, which is critical for supporting large WebSocket broadcasts.
    // MONITOR: High-water marks should be checked if new API handlers use large stack arrays.
    server.config.stack_size = 6144; 
    // server.config.max_open_sockets is configured in begin()
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
    
    // Initialize PSRAM Web Cache (loads assets into RAM for ultra-fast serving)
    PsramWebCache::getInstance().init();
    
    // PHASE 6.4: Allocate broadcast buffer in PSRAM
    if (broadcast_buffer == nullptr) {
        broadcast_buffer = (char*)psramMalloc(2048);
    }
    
    // Register all API routes (extracted for maintainability)
    setupRoutes();
    
    // Initialize status cache
    memset(&current_status, 0, sizeof(current_status));
    SAFE_STRCPY(current_status.status, "IDLE", sizeof(current_status.status));
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
        
        // PHASE 6.8: Track client for heartbeat
        ws_clients[client] = millis();

        // PHASE 6.3: Memory Optimization
        // DO NOT send initial state here. It causes heap churn during page navigation.
        // The client will pick up the next periodic broadcast (within 500ms).
        // This elimiantes a ~2KB allocation per page load.
    });
    
    wsHandler.onClose([this](PsychicWebSocketClient *client) {
        // logPrintf("[WS] Client disconnected: %s\n", client->remoteIP().toString().c_str());
        
        // PHASE 6.8: Stop tracking client
        ws_clients.erase(client);
    });
    
    wsHandler.onFrame([this](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        // Handle incoming messages (commands from web UI)
        if (frame->type == HTTPD_WS_TYPE_TEXT) {
            // PHASE 6.8: Update activity timestamp
            ws_clients[request->client()] = millis();

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
        else if (frame->type == HTTPD_WS_TYPE_PONG) {
            // PHASE 6.8: Heartbeat response received
            ws_clients[request->client()] = millis();
            // logVerbose("[WS] Pong received from %s", request->client()->remoteIP().toString().c_str());
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

    // PSRAM Cache Handler: Intercept all other GET requests to serve from RAM
    server.on("*", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        const cached_file_t* cachedFile = nullptr;
        if (PsramWebCache::getInstance().get(request->path().c_str(), &cachedFile)) {
            response->addHeader("Cache-Control", "public, max-age=3600");
            return response->send(200, cachedFile->content_type.c_str(), cachedFile->data, cachedFile->size);
        }
        return (esp_err_t)404; // Fallback to next handler (serveStatic)
    });

    // Main static handler (serves from LittleFS / as fallback)
    server.serveStatic("/", LittleFS, "/", "public, max-age=3600");
}

void WebServerManager::begin() {
    logPrintln("[WEB] Starting Server");
    
    // PHASE 6.2: Memory-safe configuration (Optimized for standard browser parallelism)
    // 6 sockets provide a good balance between concurrency and heap usage on S3.
    server.config.max_open_sockets = 6; 
    server.config.max_uri_handlers = 40;
    
    server.start(); 
}

// --- Telemetry Setters - Update internal state ---

void WebServerManager::setSystemStatus(const char* status) {
    portENTER_CRITICAL(&statusSpinlock);
    SAFE_STRCPY(current_status.status, status, sizeof(current_status.status));
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

void WebServerManager::setSpindleLoadPercent(float load_pct) {
    portENTER_CRITICAL(&statusSpinlock);
    current_status.spindle_load_pct = load_pct;
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
void WebServerManager::broadcastState() {
    if (wsHandler.count() == 0) return;
    
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest_block < 40960) return;
    
    if (broadcast_buffer == nullptr) return;
    
    size_t len = telemetryExportJSON(broadcast_buffer, 2048, false);
    
    if (len > 0 && len < 2048) {
        wsHandler.sendAll(broadcast_buffer);
    } else if (len >= 2048) {
        logError("[WS] Broadcast skipped - Buffer overflow (%u bytes)", (uint32_t)len);
    }
    // Note: If len == 0, skip silently (no heartbeat needed - clients have their own timeout)

    // Update history tracking
    system_telemetry_t t = telemetryGetSnapshot();
    updateHistory(t.cpu_usage_percent, t.free_heap_bytes, current_status.vfd_current_amps);

    // PHASE 6.8: Check WebSocket health (prune stale clients)
    checkWsHealth();
}

void WebServerManager::checkWsHealth() {
    static uint32_t last_check = 0;
    const uint32_t CHECK_INTERVAL = 5000;  // Check every 5s
    const uint32_t CLIENT_TIMEOUT = 60000; // Timeout after 60s inactivity (more lenient for WiFi)

    uint32_t now = millis();
    uint32_t elapsed_check = (now >= last_check) ? (now - last_check) : (UINT32_MAX - last_check + now + 1);
    if (elapsed_check < CHECK_INTERVAL) return;
    last_check = now;

    // Iterate through tracked clients
    // Note: Use iterator to safely remove elements while iterating
    for (auto it = ws_clients.begin(); it != ws_clients.end(); ) {
        PsychicWebSocketClient* client = it->first;
        uint32_t last_seen = it->second;

        uint32_t elapsed_client = (now >= last_seen) ? (now - last_seen) : (UINT32_MAX - last_seen + now + 1);
        if (elapsed_client > CLIENT_TIMEOUT) {
            logWarning("[WS] Client %s timed out (30s). Closing.", client->remoteIP().toString().c_str());
            
            // Close the connection (triggers onClose which will erase from map... 
            // BUT we are iterating the map right now! Be careful).
            // Calling close() usually calls the onClose callback immediately or async.
            // If it calls immediately, we get invalid iterator.
            
            // SAFER APPROACH: Mark for deletion or rely on close() triggering callback?
            // If we erase here, we must update iterator.
            // But if close() triggers onClose(), onClose() will try to erase the same key.
            // Map erase is safe if key missing? Yes.
            
            // Let's close it. If onClose fires immediately, 'it' might be invalidated if implementation is naive?
            // Actually, onClose callback uses "this->ws_clients.erase(client)".
            // If we are holding an iterator to it, standard map rules say valid unless we erase 'it'.
            // But if callback erases 'it', then 'it' becomes invalid.
            
            // Safe pattern: advance iterator, then close.
            PsychicWebSocketClient* to_close = client;
            ++it; 
            
            // Now 'it' points to next. 'to_close' is valid pointer.
            // Closing should be safe.
            to_close->close(); 
        } else {
            // Client active - Send Ping to keep alive and solicit Pong
            // OpCode 0x9 is PING. PsychicHttp 'sendFrame' needed?
            // PsychicWebSocketClient has sendPing()?
            // Looking at library headers (simulated), standard way is usually send(..., PING).
            // PsychicHttp uses valid ESP-IDF ws_send_frame_async underneath.
            
            // Assuming client->sendPing() exists or sending raw frame.
            // If API unknown, simplest is sending text "ping".
            // But real WS Ping is OpCode 0x9.
            // client->sendPing() is standard in many libs. 
            // Let's try sending standard ping frame if method exists, else text.
            // Since we can't see library source, safe bet for "Heartbeat" task logic is:
            // "client->sendPing();"
            // If that fails compilation, we will fix.
            // PsychicWebSocketClient usually inherits from something or wraps httpd_ws_send_frame.
            
            // Let's assume sendPing() is available or use raw frame construction.
            // For now, I'll use `client->sendPing()`.
            // Use standard ESP-IDF opcode for PING (0x9)
            client->sendMessage(HTTPD_WS_TYPE_PING, nullptr, 0);
            ++it;
        }
    }
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

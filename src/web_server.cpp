/**
 * @file web_server.cpp
 * @brief Minimal Web Server Implementation
 * @details Implements minimal static file serving matching the WebServerManager interface.
 */

#include "web_server.h"
#include <LittleFS.h>

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
    Serial.println("[WEB] Minimal Init (Tuned)");
    
    // TUNE: Increase stack size and enable LRU purge for concurrency
    server.config.stack_size = 8192;
    server.config.lru_purge_enable = true;

    // Mount Filesystem
    if (!LittleFS.begin()) {
        Serial.println("[WEB] Failed to mount LittleFS");
        return;
    }
    
    // Serve static files from root, disable caching to force reload of index.html
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
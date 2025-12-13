/**
 * @file web_server.h
 * @brief Async Web Server manager with WebSockets, Auth, and File Management
 * @project Gemini v1.3.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "config_unified.h"
#include "system_constants.h" 

class WebServerManager {
public:
    WebServerManager(uint16_t port = 80);
    ~WebServerManager();

    // Initialization
    void init();
    void begin();

    // Legacy support (No-op in Async mode)
    void handleClient();

    // Telemetry & State
    void setSystemStatus(const char* status);
    void setAxisPosition(char axis, float position);
    void setSystemUptime(uint32_t seconds);

    // Push state to all connected WebSocket clients
    void broadcastState();

    // Credentials Management (PHASE 5.1: Security hardening)
    void loadCredentials();
    bool isPasswordChangeRequired();
    void setPassword(const char* new_password);

private:
    AsyncWebServer* server;
    AsyncWebSocket* ws;
    uint16_t port;
    
    // Internal State Cache
    struct {
        char status[32];
        float x_pos, y_pos, z_pos, a_pos;
        uint32_t uptime_sec;
    } current_status;

    // Handlers
    void setupRoutes();
    void handleJogBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleFirmwareUpload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

    // File Manager Handlers
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
};

extern WebServerManager webServer;

#endif // WEB_SERVER_H
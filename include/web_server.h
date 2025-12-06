/**
 * @file web_server.h
 * @brief Web Server manager for UI and JSON API
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include "config_unified.h"
#include "system_constants.h" 

// Forward declarations
class WebServer;

class WebServerManager {
public:
    WebServerManager(uint16_t port = 80);
    ~WebServerManager();

    // Initialization
    void init();
    void begin();
    void handleClient();

    // Status updates
    void setSystemStatus(const char* status);
    void setAxisPosition(char axis, float position);
    void setSystemUptime(uint32_t seconds);

private:
    WebServer* server;
    uint16_t port;
    
    // Status data
    struct {
        char status[32];
        float x_pos, y_pos, z_pos, a_pos;
        uint32_t uptime_sec;
    } current_status;

    // HTTP handlers
    void handleRoot();
    void handleNotFound();
    void handleStatus();
    void handleJog();
    void handleSettings();
    void handleDiagnostics();
    
    // Helper methods
    void serveFile(const char* filename, const char* contentType);
};

extern WebServerManager webServer;

#endif // WEB_SERVER_H
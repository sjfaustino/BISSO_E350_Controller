#include "web_server.h"
#include <ArduinoJson.h>

// Global instance
WebServerManager webServer(80);

WebServerManager::WebServerManager(uint16_t port) : port(port), server(nullptr) {
    memset(&current_status, 0, sizeof(current_status));
    strcpy(current_status.status, "INITIALIZING");
    current_status.z_pos = 25.0; // Default Z
}

WebServerManager::~WebServerManager() {
    if (server) {
        delete server;
    }
}

void WebServerManager::init() {
    // Initialize SPIFFS for storing web files
    if (!SPIFFS.begin(true)) {
        Serial.println("[WEB] SPIFFS Mount Failed");
        return;
    }
    Serial.println("[WEB] SPIFFS mounted successfully");
    
    // Create server instance
    server = new WebServer(port);
    
    // Setup HTTP routes
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/index.html", HTTP_GET, [this]() { handleRoot(); });
    server->on("/jog.html", HTTP_GET, [this]() { serveFile("/jog.html", "text/html"); });
    server->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server->on("/api/jog", HTTP_POST, [this]() { handleJog(); });
    server->onNotFound([this]() { handleNotFound(); });
    
    Serial.println("[WEB] Server initialized on port 80");
}

void WebServerManager::begin() {
    if (server) {
        server->begin();
        Serial.println("[WEB] Web server started");
    }
}

void WebServerManager::handleClient() {
    if (server) {
        server->handleClient();
    }
}

void WebServerManager::handleRoot() {
    serveFile("/index.html", "text/html");
}

void WebServerManager::serveFile(const char* filename, const char* contentType) {
    if (!server) return;
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        server->send(404, "text/plain", "File not found");
        return;
    }
    
    server->streamFile(file, contentType);
    file.close();
}

void WebServerManager::handleStatus() {
    if (!server) return;
    
    String json = getStatusJSON();
    server->send(200, "application/json", json);
}

String WebServerManager::getStatusJSON() {
    JsonDocument doc;
    
    doc["status"] = current_status.status;
    doc["x_pos"] = current_status.x_pos;
    doc["y_pos"] = current_status.y_pos;
    doc["z_pos"] = current_status.z_pos;
    doc["a_pos"] = current_status.a_pos;
    doc["uptime"] = current_status.uptime_sec;
    
    String json;
    serializeJson(doc, json);
    return json;
}

void WebServerManager::handleJog() {
    if (!server) return;
    
    if (!server->hasArg("plain")) {
        server->send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }
    
    String body = server->arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    
    const char* direction = doc["direction"];
    Serial.printf("[WEB-JOG] Direction: %s\n", direction);
    
    // TODO: Integrate with motion control
    
    server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebServerManager::handleNotFound() {
    if (!server) return;
    server->send(404, "text/plain", "Not Found");
}

void WebServerManager::handleSettings() {
    if (!server) return;
    server->send(200, "text/plain", "Settings endpoint");
}

void WebServerManager::handleDiagnostics() {
    if (!server) return;
    server->send(200, "text/plain", "Diagnostics endpoint");
}

void WebServerManager::setSystemStatus(const char* status) {
    if (status) {
        strncpy(current_status.status, status, sizeof(current_status.status) - 1);
        current_status.status[sizeof(current_status.status) - 1] = '\0';
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

#include "web_server.h"
#include "motion.h"
#include <ArduinoJson.h>

// Global instance
WebServerManager webServer(80);

// Static buffer for JSON responses
static char json_response_buffer[WEB_BUFFER_SIZE];

// FIX: Reordered initialization list to match header declaration order (server, then port)
WebServerManager::WebServerManager(uint16_t port) : server(nullptr), port(port) {
    memset(&current_status, 0, sizeof(current_status));
    strcpy(current_status.status, "INITIALIZING");
    current_status.z_pos = 25.0; 
}

WebServerManager::~WebServerManager() {
    if (server) delete server;
}

void WebServerManager::init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[WEB] [FAIL] SPIFFS Mount Failed");
        return;
    }
    Serial.println("[WEB] [OK] SPIFFS mounted");
    
    server = new WebServer(port);
    
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/index.html", HTTP_GET, [this]() { handleRoot(); });
    server->on("/jog.html", HTTP_GET, [this]() { serveFile("/jog.html", "text/html"); });
    server->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server->on("/api/jog", HTTP_POST, [this]() { handleJog(); });
    server->onNotFound([this]() { handleNotFound(); });
    
    Serial.println("[WEB] [OK] Server initialized");
}

void WebServerManager::begin() {
    if (server) {
        server->begin();
        Serial.println("[WEB] [OK] Started");
    }
}

void WebServerManager::handleClient() {
    if (server) server->handleClient();
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
    
    JsonDocument doc;
    doc["status"] = current_status.status;
    doc["x_pos"] = current_status.x_pos;
    doc["y_pos"] = current_status.y_pos;
    doc["z_pos"] = current_status.z_pos;
    doc["a_pos"] = current_status.a_pos;
    doc["uptime"] = current_status.uptime_sec;
    
    serializeJson(doc, json_response_buffer, sizeof(json_response_buffer));
    server->send(200, "application/json", json_response_buffer);
}

void WebServerManager::handleJog() {
    if (!server) return;
    
    if (!server->hasArg("plain")) {
        server->send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }
    
    String body = server->arg("plain");
    JsonDocument doc;
    
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        Serial.print("[WEB] [ERR] JSON parse error: ");
        Serial.println(error.c_str());
        server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    
    const char* direction = doc["direction"];
    float distance = doc["distance"] | 10.0f;     
    float speed = doc["speed"] | 50.0f;            
    
    if (!direction || distance <= 0 || distance > 500 || speed <= 0 || speed > 200) {
        Serial.println("[WEB] [ERR] Invalid parameters");
        server->send(400, "application/json", 
            "{\"error\":\"Invalid: distance must be 0-500, speed 0-200\"}");
        return;
    }
    
    Serial.printf("[WEB] Jog: %s, %.1f mm, %.1f mm/s\n", direction, distance, speed);
    
    bool success = false;
    if (strcmp(direction, "X+") == 0) { motionMoveRelative(distance, 0, 0, 0, speed); success = true; } 
    else if (strcmp(direction, "X-") == 0) { motionMoveRelative(-distance, 0, 0, 0, speed); success = true; } 
    else if (strcmp(direction, "Y+") == 0) { motionMoveRelative(0, distance, 0, 0, speed); success = true; } 
    else if (strcmp(direction, "Y-") == 0) { motionMoveRelative(0, -distance, 0, 0, speed); success = true; } 
    else if (strcmp(direction, "Z+") == 0) { motionMoveRelative(0, 0, distance, 0, speed); success = true; } 
    else if (strcmp(direction, "Z-") == 0) { motionMoveRelative(0, 0, -distance, 0, speed); success = true; } 
    else if (strcmp(direction, "A+") == 0) { motionMoveRelative(0, 0, 0, distance, speed); success = true; } 
    else if (strcmp(direction, "A-") == 0) { motionMoveRelative(0, 0, 0, -distance, speed); success = true; } 
    else if (strcmp(direction, "STOP") == 0) { motionStop(); success = true; }
    
    if (success) {
        JsonDocument response;
        response["status"] = "ok";
        response["direction"] = direction;
        // Note: motionGetPosition returns int32, division by float literal ensures float result
        response["x_pos"] = motionGetPosition(0) / 1000.0f;
        response["y_pos"] = motionGetPosition(1) / 1000.0f;
        response["z_pos"] = motionGetPosition(2) / 1000.0f;
        response["a_pos"] = motionGetPosition(3) / 1000.0f;
        
        serializeJson(response, json_response_buffer, sizeof(json_response_buffer));
        server->send(200, "application/json", json_response_buffer);
        Serial.println("[WEB] [OK] Jog executed");
    } else {
        server->send(500, "application/json", "{\"error\":\"Command failed\"}");
        Serial.println("[WEB] [FAIL] Jog failed");
    }
}

void WebServerManager::handleNotFound() { if (!server) return; server->send(404, "text/plain", "Not Found"); }
void WebServerManager::handleSettings() { if (!server) return; server->send(200, "text/plain", "Settings"); }
void WebServerManager::handleDiagnostics() { if (!server) return; server->send(200, "text/plain", "Diagnostics"); }

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
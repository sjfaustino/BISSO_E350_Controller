#include "web_server.h"
#include "motion.h"
#include <ArduinoJson.h>

// Global instance
WebServerManager webServer(80);

// Buffer for WebSocket broadcasts
static char json_response_buffer[WEB_BUFFER_SIZE];

WebServerManager::WebServerManager(uint16_t port) : server(nullptr), ws(nullptr), port(port) {
    memset(&current_status, 0, sizeof(current_status));
    strcpy(current_status.status, "INITIALIZING");
    current_status.z_pos = 25.0; 
}

WebServerManager::~WebServerManager() {
    if (server) delete server;
    if (ws) delete ws;
}

void WebServerManager::init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[WEB] [FAIL] SPIFFS Mount Failed");
        return;
    }
    Serial.println("[WEB] [OK] SPIFFS mounted");
    
    server = new AsyncWebServer(port);
    ws = new AsyncWebSocket("/ws");
    
    // Setup WebSocket
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
    // No-op for AsyncWebServer
    // Kept to prevent breaking existing main.cpp loop calls if any
}

void WebServerManager::setupRoutes() {
    // 1. Static Files (Non-blocking)
    server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // 2. API Status (GET)
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["status"] = current_status.status;
        doc["x_pos"] = current_status.x_pos;
        doc["y_pos"] = current_status.y_pos;
        doc["z_pos"] = current_status.z_pos;
        doc["a_pos"] = current_status.a_pos;
        doc["uptime"] = current_status.uptime_sec;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 3. API Jog (POST) with Body Handling
    server->on("/api/jog", HTTP_POST, 
        [](AsyncWebServerRequest *request){ request->send(200); }, 
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            this->handleJogBody(request, data, len, index, total);
        }
    );
    
    server->onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not Found");
    });
}

void WebServerManager::handleJogBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Note: Simple implementation assuming small JSON body < buffer size
    // In production, accumulation might be needed for large packets, 
    // but Jog commands are tiny (~50 bytes).
    
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

void WebServerManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT){
        Serial.printf("[WEB] WS Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        // Send immediate update on connect
        broadcastState();
    } else if(type == WS_EVT_DISCONNECT){
        Serial.printf("[WEB] WS Client #%u disconnected\n", client->id());
    }
}

void WebServerManager::broadcastState() {
    // Only broadcast if clients are connected
    if (ws->count() == 0) return;

    JsonDocument doc;
    doc["status"] = current_status.status;
    doc["x"] = current_status.x_pos; // Shortened keys for efficiency
    doc["y"] = current_status.y_pos;
    doc["z"] = current_status.z_pos;
    doc["a"] = current_status.a_pos;
    
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
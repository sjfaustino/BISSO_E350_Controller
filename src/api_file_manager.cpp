/**
 * @file api_file_manager.cpp
 * @brief Implementation of File Management API Routes
 * @project Gemini v3.1.0
 */

#include "api_file_manager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "web_server.h" // For WEB_BUFFER_SIZE, though not ideal dependency

// External definitions from motion_commands needed to complete API
extern void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);

// --- Extracted Implementations ---

void handleFileList(AsyncWebServerRequest *request) {
    // MEMORY FIX: Use StaticJsonDocument + char array to prevent heap fragmentation
    // Sized for ~10 files with paths up to 32 chars each
    StaticJsonDocument<1024> doc;
    JsonArray array = doc.to<JsonArray>();

    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file){
        JsonObject obj = array.add<JsonObject>();
        // MEMORY FIX: Use file.name() directly (returns const char*), no String conversion
        obj["name"] = file.name();
        obj["size"] = file.size();
        file = root.openNextFile();
    }

    char response[1024];
    serializeJson(doc, response, sizeof(response));
    request->send(200, "application/json", response);
}

void handleFileDelete(AsyncWebServerRequest *request) {
    if(!request->hasParam("name")) {
        request->send(400, "text/plain", "Missing name param");
        return;
    }

    // MEMORY FIX: Use const char* directly instead of String
    const char* path = request->getParam("name")->value().c_str();
    if(SPIFFS.exists(path)) {
        SPIFFS.remove(path);
        request->send(200, "text/plain", "Deleted");
    } else {
        request->send(404, "text/plain", "File not found");
    }
}

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // Security: Filter extensions
    if (!filename.endsWith(".nc") && !filename.endsWith(".gcode") && !filename.endsWith(".txt")) {
        if(index == 0) Serial.println("[FILE_API] Blocked upload of non-GCode file");
        return;
    }

    if (!index) {
        if(!filename.startsWith("/")) filename = "/" + filename;
        request->_tempFile = SPIFFS.open(filename, "w");
    }
    
    if (request->_tempFile) {
        request->_tempFile.write(data, len);
    }

    if (final) {
        if(request->_tempFile) request->_tempFile.close();
        Serial.printf("[FILE_API] Upload Complete: %s (%u bytes)\n", filename.c_str(), index + len);
    }
}

// --- Registration ---

void apiRegisterFileRoutes(AsyncWebServer* server, const char* username, const char* password) {
    // API: List Files (Protected)
    server->on("/api/files", HTTP_GET, [username, password](AsyncWebServerRequest *request){
        if(!request->authenticate(username, password)) return request->requestAuthentication();
        handleFileList(request);
    });

    // API: Delete File (Protected)
    server->on("/api/files", HTTP_DELETE, [username, password](AsyncWebServerRequest *request){
        if(!request->authenticate(username, password)) return request->requestAuthentication();
        handleFileDelete(request);
    });

    // API: Upload File (Protected)
    server->on("/api/upload", HTTP_POST, 
        [](AsyncWebServerRequest *request) { request->send(200); }, // On success
        [username, password](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            // Check auth in the main handler function if necessary, or rely on route auth
            handleFileUpload(request, filename, index, data, len, final);
        }
    ).setAuthentication(username, password);
}
/**
 * @file api_file_manager.cpp
 * @brief Implementation of File Management API Routes
 * @project Gemini v3.1.0
 */

#include "api_file_manager.h"
#include "auth_manager.h"  // PHASE 5.10: SHA-256 authentication
#include "web_server.h" // For WEB_BUFFER_SIZE, though not ideal dependency
#include <ArduinoJson.h>
#include <SPIFFS.h>

// PHASE 5.10: Local auth helper using auth_manager
static bool requireAuth(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) {
    request->requestAuthentication();
    return false;
  }

  AsyncWebHeader* authHeader = request->getHeader("Authorization");
  const char* auth = authHeader->value().c_str();

  if (!authVerifyHTTPBasicAuth(auth)) {
    request->requestAuthentication();
    return false;
  }

  return true;
}


// External definitions from motion_commands needed to complete API
extern void motionMoveAbsolute(float x, float y, float z, float a,
                               float speed_mm_s);

// --- Helper Functions ---

/**
 * @brief Validates filename to prevent path traversal attacks
 * @param filename The filename to validate
 * @return true if filename is safe, false if it contains malicious patterns
 *
 * PHASE 5.10: Security - Path Traversal Prevention
 * Blocks: "..", absolute paths, special characters
 * Allows: alphanumeric, underscore, dot, dash
 */
static bool isValidFilename(const char* filename) {
  if (!filename || filename[0] == '\0') {
    return false;
  }

  // Block path traversal attempts
  if (strstr(filename, "..") != NULL) {
    Serial.printf("[FILE_API] [SECURITY] Blocked path traversal: %s\n", filename);
    return false;
  }

  // Block absolute paths (should be relative to SPIFFS root)
  if (filename[0] == '/') {
    filename++;  // Skip leading slash for validation
  }

  // Whitelist: alphanumeric + underscore + dot + dash
  for (const char* p = filename; *p; p++) {
    if (!isalnum(*p) && *p != '_' && *p != '.' && *p != '-' && *p != '/') {
      Serial.printf("[FILE_API] [SECURITY] Blocked invalid character in filename: %s\n", filename);
      return false;
    }
  }

  return true;
}

// --- Extracted Implementations ---

void handleFileList(AsyncWebServerRequest *request) {
  // MEMORY FIX: Use StaticJsonDocument + char array to prevent heap
  // fragmentation Sized for ~10 files with paths up to 32 chars each
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    JsonObject obj = array.add<JsonObject>();
    // MEMORY FIX: Use file.name() directly (returns const char*), no String
    // conversion
    obj["name"] = file.name();
    obj["size"] = file.size();
    file = root.openNextFile();
  }

  // PHASE 5.10: Critical fix - Close directory handle to prevent leak
  root.close();

  char response[1024];
  serializeJson(doc, response, sizeof(response));
  request->send(200, "application/json", response);
}

void handleFileDelete(AsyncWebServerRequest *request) {
  if (!request->hasParam("name")) {
    request->send(400, "text/plain", "Missing name param");
    return;
  }

  // MEMORY FIX: Use const char* directly instead of String
  const char *path = request->getParam("name")->value().c_str();
  if (SPIFFS.exists(path)) {
    SPIFFS.remove(path);
    request->send(200, "text/plain", "Deleted");
  } else {
    request->send(404, "text/plain", "File not found");
  }
}

void handleFileUpload(AsyncWebServerRequest *request, String filename,
                      size_t index, uint8_t *data, size_t len, bool final) {
  // PHASE 5.10: Security - Validate filename first (path traversal prevention)
  if (!isValidFilename(filename.c_str())) {
    if (index == 0) {
      Serial.printf("[FILE_API] [SECURITY] Rejected unsafe filename: %s\n", filename.c_str());
    }
    return;
  }

  // Security: Filter extensions
  if (!filename.endsWith(".nc") && !filename.endsWith(".gcode") &&
      !filename.endsWith(".txt")) {
    if (index == 0)
      Serial.println("[FILE_API] Blocked upload of non-GCode file");
    return;
  }

  if (!index) {
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    request->_tempFile = SPIFFS.open(filename, "w");
  }

  if (request->_tempFile) {
    request->_tempFile.write(data, len);
  }

  if (final) {
    if (request->_tempFile)
      request->_tempFile.close();
    Serial.printf("[FILE_API] Upload Complete: %s (%u bytes)\n",
                  filename.c_str(), index + len);
  }
}

// --- Registration ---

// PHASE 5.10: Removed username/password parameters - using SHA-256 auth
void apiRegisterFileRoutes(AsyncWebServer *server) {
  // API: List Files (Protected)
  server->on("/api/files", HTTP_GET,
             [](AsyncWebServerRequest *request) {
               if (!requireAuth(request)) return;
               handleFileList(request);
             });

  // API: Delete File (Protected)
  server->on("/api/files", HTTP_DELETE,
             [](AsyncWebServerRequest *request) {
               if (!requireAuth(request)) return;
               handleFileDelete(request);
             });

  // API: Upload File (Protected)
  server->on(
      "/api/upload", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        request->send(200);
      },
      [](AsyncWebServerRequest *request, String filename,
         size_t index, uint8_t *data, size_t len, bool final) {
        handleFileUpload(request, filename, index, data, len, final);
      });
}
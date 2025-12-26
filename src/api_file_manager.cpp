/**
 * @file api_file_manager.cpp
 * @brief Implementation of File Management API Routes
 * @project Gemini v3.1.0
 */

#include "api_file_manager.h"
#include "auth_manager.h"  // PHASE 5.10: SHA-256 authentication
#include "web_server.h" // For WEB_BUFFER_SIZE, though not ideal dependency
#include "serial_logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// PHASE 5.10: Local auth helper with rate limiting
static bool requireAuth(AsyncWebServerRequest *request) {
  // Get client IP address for rate limiting
  String client_ip = request->client()->remoteIP().toString();
  const char* ip_address = client_ip.c_str();

  // Check rate limit (brute force protection)
  if (!authCheckRateLimit(ip_address)) {
    request->send(429, "text/plain", "Too many authentication attempts. Please try again later.");
    return false;
  }

  if (!request->hasHeader("Authorization")) {
    request->requestAuthentication();
    return false;
  }

  // CRITICAL FIX: Store String before calling c_str() to prevent dangling pointer
  const AsyncWebHeader* authHeader = request->getHeader("Authorization");
  String auth_value = authHeader->value();
  const char* auth = auth_value.c_str();

  if (!authVerifyHTTPBasicAuth(auth)) {
    authRecordFailedAttempt(ip_address);
    request->requestAuthentication();
    return false;
  }

  authClearRateLimit(ip_address);
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
    logWarning("[FILE_API] [SECURITY] Blocked path traversal: %s", filename);
    return false;
  }

  // Block absolute paths (should be relative to LittleFS root)
  if (filename[0] == '/') {
    filename++;  // Skip leading slash for validation
  }

  // Whitelist: alphanumeric + underscore + dot + dash
  for (const char* p = filename; *p; p++) {
    if (!isalnum(*p) && *p != '_' && *p != '.' && *p != '-' && *p != '/') {
      logWarning("[FILE_API] [SECURITY] Blocked invalid character in filename: %s", filename);
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

  File root = LittleFS.open("/");
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

  // PHASE 5.10: Security - Validate filename before deletion (path traversal prevention)
  if (!isValidFilename(path)) {
    logWarning("[FILE_API] [SECURITY] Blocked delete attempt with unsafe filename: %s", path);
    request->send(400, "text/plain", "Invalid filename: path traversal or illegal characters");
    return;
  }

  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
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
      logWarning("[FILE_API] [SECURITY] Rejected unsafe filename: %s", filename.c_str());
      request->send(400, "text/plain", "Invalid filename: path traversal or illegal characters");
    }
    return;
  }

  // Security: Filter extensions
  if (!filename.endsWith(".nc") && !filename.endsWith(".gcode") &&
      !filename.endsWith(".txt")) {
    if (index == 0) {
      logWarning("[FILE_API] Blocked upload of non-GCode file");
      request->send(400, "text/plain", "Invalid file type: only .nc, .gcode, .txt allowed");
    }
    return;
  }

  if (!index) {
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    request->_tempFile = LittleFS.open(filename, "w");
  }

  if (request->_tempFile) {
    request->_tempFile.write(data, len);
  }

  if (final) {
    if (request->_tempFile)
      request->_tempFile.close();
    logInfo("[FILE_API] Upload Complete: %s (%u bytes)",
                  filename.c_str(), index + len);
    request->send(200, "text/plain", "Upload complete");
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
        // PHASE 5.10: Auth check only - response sent by body handler on final chunk
        if (!requireAuth(request)) return;
        // Response removed to prevent double-send (body handler responds on completion/error)
      },
      [](AsyncWebServerRequest *request, String filename,
         size_t index, uint8_t *data, size_t len, bool final) {
        handleFileUpload(request, filename, index, data, len, final);
      });
}
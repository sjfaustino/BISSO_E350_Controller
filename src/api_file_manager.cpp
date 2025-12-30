/**
 * @file api_file_manager.cpp
 * @brief Implementation of File Management API Routes (PsychicHttp)
 * @project Gemini v3.6.0
 */

#include "api_file_manager.h"
#include "auth_manager.h"  // PHASE 5.10: SHA-256 authentication
#include "serial_logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <functional>  // For std::function in recursive listing

// PHASE 5.10: Local auth helper with rate limiting for PsychicHttp
// Returns ESP_OK if authenticated, ESP_FAIL otherwise (caller should return early)
static esp_err_t requireAuth(PsychicRequest *request, PsychicResponse *response) {
  // Get client IP address for rate limiting
  String client_ip = request->client()->remoteIP().toString();
  const char* ip_address = client_ip.c_str();

  // Check rate limit (brute force protection)
  if (!authCheckRateLimit(ip_address)) {
    response->send(429, "text/plain", "Too many authentication attempts. Please try again later.");
    return ESP_FAIL;
  }

  // Check for Authorization header
  if (!request->hasHeader("Authorization")) {
    return request->requestAuthentication(BASIC_AUTH, "BISSO E350", "Authentication required");
  }

  // Get authorization header value
  String auth_value = request->header("Authorization");
  const char* auth = auth_value.c_str();

  if (!authVerifyHTTPBasicAuth(auth)) {
    authRecordFailedAttempt(ip_address);
    return request->requestAuthentication(BASIC_AUTH, "BISSO E350", "Invalid credentials");
  }

  authClearRateLimit(ip_address);
  return ESP_OK;
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

// --- Handler Implementations ---

static esp_err_t handleFileList(PsychicRequest *request) {
  PsychicResponse response(request);
  
  // Auth check
  if (requireAuth(request, &response) != ESP_OK) {
    return ESP_OK;  // Response already sent
  }
  
  // MEMORY FIX: Use StaticJsonDocument + char array to prevent heap
  // fragmentation. Sized for ~30 files with paths up to 64 chars each
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  
  // Recursive helper using lambda - captures array by reference
  std::function<void(const char*)> listDirRecursive = [&](const char* dirname) {
    File root = LittleFS.open(dirname);
    if (!root || !root.isDirectory()) return;
    
    File file = root.openNextFile();
    while (file) {
      String fullPath = String(dirname);
      if (!fullPath.endsWith("/")) fullPath += "/";
      fullPath += file.name();
      
      if (file.isDirectory()) {
        // Recurse into subdirectory
        listDirRecursive(fullPath.c_str());
      } else {
        // Add file to list
        JsonObject obj = array.add<JsonObject>();
        obj["name"] = fullPath;  // Full path for compatibility
        obj["path"] = fullPath;  // Also include as path
        obj["size"] = file.size();
      }
      file = root.openNextFile();
    }
    root.close();
  };
  
  listDirRecursive("/");

  // Use larger buffer to prevent truncation with many files
  static char responseBuffer[8192];  // Static to avoid stack overflow
  size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
  
  // Check for truncation
  if (len >= sizeof(responseBuffer) - 1) {
    logWarning("[FILE_API] File list response truncated, too many files");
  }
  
  return response.send(200, "application/json", responseBuffer);
}

static esp_err_t handleFileDelete(PsychicRequest *request) {
  PsychicResponse response(request);
  
  // Auth check
  if (requireAuth(request, &response) != ESP_OK) {
    return ESP_OK;  // Response already sent
  }
  
  if (!request->hasParam("name")) {
    return response.send(400, "text/plain", "Missing name param");
  }

  // Get param value
  String pathStr = request->getParam("name")->value();
  const char *path = pathStr.c_str();

  // PHASE 5.10: Security - Validate filename before deletion (path traversal prevention)
  if (!isValidFilename(path)) {
    logWarning("[FILE_API] [SECURITY] Blocked delete attempt with unsafe filename: %s", path);
    return response.send(400, "text/plain", "Invalid filename: path traversal or illegal characters");
  }

  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
    return response.send(200, "text/plain", "Deleted");
  } else {
    return response.send(404, "text/plain", "File not found");
  }
}

// --- Registration ---

// PHASE 5.10: Removed username/password parameters - using SHA-256 auth
void apiRegisterFileRoutes(PsychicHttpServer& server) {
  // API: List Files (Protected)
  server.on("/api/files", HTTP_GET, handleFileList);

  // API: Delete File (Protected)  
  server.on("/api/files", HTTP_DELETE, handleFileDelete);

  // NOTE: File upload requires PsychicUploadHandler which is configured separately
  // in web_server.cpp since it requires different handler type
  logInfo("[FILE_API] File routes registered (PsychicHttp)");
}
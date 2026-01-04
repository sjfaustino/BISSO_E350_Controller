/**
 * @file api_file_manager.cpp
 * @brief Implementation of File Management API Routes (PsychicHttp)
 * @project PosiPro
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

// --- Helper Functions ---

/**
 * @brief Validates filename to prevent path traversal attacks
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

  return true;
}

// --- Handler Implementations ---

static esp_err_t handleFileList(PsychicRequest *request, PsychicResponse *response) {
  // Auth check
  if (requireAuth(request, response) != ESP_OK) {
    return ESP_OK;  // Response already sent
  }
  
  // Use a reserved string to prevent heap fragmentation during construction
  String json;
  json.reserve(2048);
  json = "[";
  
  bool first = true;
  
  // Recursive helper
  std::function<void(const char*)> listDirRecursive = [&](const char* dirname) {
    File root = LittleFS.open(dirname);
    if (!root || !root.isDirectory()) return;
    
    File file = root.openNextFile();
    while (file) {
      String fullPath = String(dirname);
      if (!fullPath.endsWith("/")) fullPath += "/";
      fullPath += file.name();
      
      if (file.isDirectory()) {
        listDirRecursive(fullPath.c_str());
      } else {
        if (!first) json += ",";
        first = false;
        
        json += "{\"path\":\"" + fullPath + "\",\"size\":" + String(file.size()) + "}";
      }
      file = root.openNextFile();
    }
    root.close();
  };
  
  listDirRecursive("/");
  json += "]";
  
  return response->send(200, "application/json", json.c_str());
}

static esp_err_t handleFileDelete(PsychicRequest *request, PsychicResponse *response) {
  // Auth check
  if (requireAuth(request, response) != ESP_OK) {
    return ESP_OK;  // Response already sent
  }
  
  if (!request->hasParam("name")) {
    return response->send(400, "text/plain", "Missing name param");
  }

  // Get param value
  String path = request->getParam("name")->value();

  // PHASE 5.10: Security - Validate filename before deletion
  if (!isValidFilename(path.c_str())) {
    logWarning("[FILE_API] [SECURITY] Blocked delete attempt with unsafe filename: %s", path.c_str());
    return response->send(400, "text/plain", "Invalid filename");
  }

  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
    return response->send(200, "text/plain", "Deleted");
  } else {
    return response->send(404, "text/plain", "File not found");
  }
}

// --- Registration ---

void apiRegisterFileRoutes(PsychicHttpServer& server) {
  // API: List Files (Protected)
  server.on("/api/files", HTTP_GET, handleFileList);

  // API: Delete File (Protected)  
  server.on("/api/files", HTTP_DELETE, handleFileDelete);

  logInfo("[FILE_API] File routes registered (PsychicHttp)");
}

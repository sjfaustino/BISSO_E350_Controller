/**
 * @file api_file_manager.cpp
 * @brief Implementation of File Management API Routes (PsychicHttp)
 * @project PosiPro
 */

#include "api_file_manager.h"
#include "auth_manager.h"  // PHASE 5.10: SHA-256 authentication
#include "serial_logger.h"
#include <ArduinoJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include "sd_card_manager.h"
#include "trash_bin_manager.h"
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
  
  String path = "/";
  if (request->hasParam("path")) {
    path = request->getParam("path")->value();
  }

  bool use_sd = path.startsWith("/sd");
  if (use_sd) {
    if (!sdCardIsMounted()) {
      return response->send(503, "text/plain", "SD Card not mounted");
    }
    path = path.substring(3); // Remove "/sd"
    if (path == "") path = "/";
  }

  FS *fs;
  if (use_sd) fs = &SD;
  else fs = &LittleFS;

  File root = fs->open(path);
  
  if (!root || !root.isDirectory()) {
    return response->send(404, "text/plain", "Directory not found");
  }

  response->setContentType("application/json");
  response->sendChunk((uint8_t*)"[", 1);
  
  bool first = true;
  File file = root.openNextFile();
  while (file) {
    if (!first) response->sendChunk((uint8_t*)",", 1);
    first = false;
    
    char entry[512];
    snprintf(entry, sizeof(entry), 
      "{\"name\":\"%s\",\"dir\":%s,\"size\":%u,\"time\":%u}", 
      file.name(), 
      file.isDirectory() ? "true" : "false", 
      (uint32_t)file.size(),
      (uint32_t)file.getLastWrite()
    );
    response->sendChunk((uint8_t*)entry, strlen(entry));
    file = root.openNextFile();
  }
  root.close();
  
  response->sendChunk((uint8_t*)"]", 1);
  return response->finishChunking();
}

static esp_err_t handleMakeDir(PsychicRequest *request, PsychicResponse *response) {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;

    if (!request->hasParam("path")) return response->send(400, "text/plain", "Missing path");
    String path = request->getParam("path")->value();

    bool use_sd = path.startsWith("/sd");
    FS *fs;
    if (use_sd) {
        if (!sdCardIsMounted()) return response->send(503, "text/plain", "SD not mounted");
        fs = &SD;
        path = path.substring(3);
    } else {
        fs = &LittleFS;
    }

    if (fs->mkdir(path)) {
        return response->send(200, "text/plain", "Created");
    }
    return response->send(500, "text/plain", "Failed to create directory");
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

  bool use_sd = path.startsWith("/sd");
  FS *fs;
  if (use_sd) {
      if (!sdCardIsMounted()) return response->send(503, "text/plain", "SD not mounted");
      fs = &SD;
      path = path.substring(3);
  } else {
      fs = &LittleFS;
  }

  if (fs->exists(path)) {
    // Check if it's a file or directory
    File f = fs->open(path);
    bool is_dir = f.isDirectory();
    f.close();

    bool success = trashBinMoveToTrash(fs, path.c_str());
    if (success) {
        return response->send(200, "text/plain", "Moved to Trash");
    }
    return response->send(500, "text/plain", "Failed to move to Trash");
  } else {
    return response->send(404, "text/plain", "File not found");
  }
}

static esp_err_t handleFileBulkDelete(PsychicRequest *request, PsychicResponse *response) {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;
    
    String body = request->body();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (error || !doc.is<JsonArray>()) {
        return response->send(400, "text/plain", "Invalid JSON array");
    }

    JsonArray paths = doc.as<JsonArray>();
    int deleted = 0;
    int failed = 0;

    for (JsonVariant pathVar : paths) {
        String path = pathVar.as<String>();
        if (!isValidFilename(path.c_str())) {
            failed++;
            continue;
        }

        bool use_sd = path.startsWith("/sd");
        FS *fs;
        if (use_sd) {
            if (!sdCardIsMounted()) { failed++; continue; }
            fs = &SD;
            path = path.substring(3);
        } else {
            fs = &LittleFS;
        }

        if (fs->exists(path)) {
            bool success = trashBinMoveToTrash(fs, path.c_str());
            if (success) deleted++;
            else failed++;
        } else {
            failed++;
        }
    }

    char result[128];
    snprintf(result, sizeof(result), "{\"deleted\":%d,\"failed\":%d}", deleted, failed);
    return response->send(200, "application/json", result);
}

// --- Registration ---

static esp_err_t handleFileRename(PsychicRequest *request, PsychicResponse *response) {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;

    if (!request->hasParam("src") || !request->hasParam("dest")) {
        return response->send(400, "text/plain", "Missing src or dest");
    }

    String src = request->getParam("src")->value();
    String dest = request->getParam("dest")->value();

    bool use_sd = src.startsWith("/sd");
    FS *fs;
    if (use_sd) {
        if (!sdCardIsMounted()) return response->send(503, "text/plain", "SD not mounted");
        fs = &SD;
        src = src.substring(3);
        if (dest.startsWith("/sd")) dest = dest.substring(3);
    } else {
        fs = &LittleFS;
    }

    if (fs->rename(src, dest)) {
        return response->send(200, "text/plain", "Renamed");
    }
    return response->send(500, "text/plain", "Rename failed");
}

static esp_err_t handleUploadRequest(PsychicRequest *request, PsychicResponse *response) {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;
    return response->send(200, "text/plain", "Upload started");
}

static esp_err_t handleFileUpload(PsychicRequest *request, const String& filename, uint64_t index, uint8_t* data, size_t len, bool final) {
    // Extract destination path from parameter "path"
    String dest = "/";
    if (request->hasParam("path")) {
        dest = request->getParam("path")->value();
    }
    if (!dest.endsWith("/")) dest += "/";
    dest += filename;

    bool use_sd = dest.startsWith("/sd");
    FS *fs;
    if (use_sd) {
        if (!sdCardIsMounted()) return ESP_FAIL;
        fs = &SD;
        dest = dest.substring(3);
    } else {
        fs = &LittleFS;
    }

    File f;
    if (index == 0) {
        f = fs->open(dest, FILE_WRITE);
    } else {
        f = fs->open(dest, FILE_APPEND);
    }

    if (!f) {
        logError("[FILE_API] Could not open %s for write", dest.c_str());
        return ESP_FAIL;
    }

    f.write(data, len);
    f.close();

    if (final) {
        logInfo("[FILE_API] Uploaded: %s", dest.c_str());
    }
    return ESP_OK;
}

static esp_err_t handleFileRead(PsychicRequest *request, PsychicResponse *response) {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;
    if (!request->hasParam("path")) return response->send(400, "text/plain", "Missing path");
    String path = request->getParam("path")->value();

    bool use_sd = path.startsWith("/sd");
    FS *fs;
    if (use_sd) {
        if (!sdCardIsMounted()) return response->send(503, "text/plain", "SD not mounted");
        fs = &SD;
        path = path.substring(3);
    } else {
        fs = &LittleFS;
    }

    if (!fs->exists(path)) return response->send(404, "text/plain", "File not found");

    // Check size - limit to 512KB for editing stability
    File f = fs->open(path, FILE_READ);
    if (f.size() > 512 * 1024) {
        f.close();
        return response->send(413, "text/plain", "File too large to edit (>512KB)");
    }
    f.close();

    PsychicFileResponse streamResponse(response, *fs, path);
    return streamResponse.send();
}

static esp_err_t handleFileSave(PsychicRequest *request, PsychicResponse *response) {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;
    if (!request->hasParam("path")) return response->send(400, "text/plain", "Missing path");
    String path = request->getParam("path")->value();
    String content = request->body();

    bool use_sd = path.startsWith("/sd");
    FS *fs;
    if (use_sd) {
        if (!sdCardIsMounted()) return response->send(503, "text/plain", "SD not mounted");
        fs = &SD;
        path = path.substring(3);
    } else {
        fs = &LittleFS;
    }

    File f = fs->open(path, FILE_WRITE);
    if (!f) return response->send(500, "text/plain", "Could not open file for writing");

    f.print(content);
    f.close();

    logInfo("[FILE_API] Saved: %s", path.c_str());
    return response->send(200, "text/plain", "Saved");
}

static esp_err_t handleTrashRestore(PsychicRequest *request, PsychicResponse *response) {
  if (requireAuth(request, response) != ESP_OK) return ESP_OK;

  String body = request->body();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error || !doc.is<JsonObject>() || !doc.containsKey("path")) {
    return response->send(400, "text/plain", "Invalid JSON: missing path");
  }

  String fullPath = doc["path"].as<String>();
  if (!isValidFilename(fullPath.c_str())) {
      return response->send(400, "text/plain", "Invalid filename");
  }

  bool use_sd = fullPath.startsWith("/sd");
  FS *fs;
  String internalPath = fullPath;
  if (use_sd) {
      if (!sdCardIsMounted()) return response->send(503, "text/plain", "SD not mounted");
      fs = &SD;
      internalPath = fullPath.substring(3); // Remove "/sd"
      if (internalPath == "") internalPath = "/";
  } else {
      fs = &LittleFS;
  }

  if (trashBinRestore(fs, internalPath.c_str())) {
      return response->send(200, "text/plain", "Restored");
  }
  return response->send(500, "text/plain", "Restore failed (target may exist)");
}

void apiRegisterFileRoutes(PsychicHttpServer& server) {
  // API: List Files (Protected)
  server.on("/api/files", HTTP_GET, handleFileList);

  // API: Delete File (Protected)  
  server.on("/api/files", HTTP_DELETE, handleFileDelete);

  // API: Bulk Delete (Protected)
  server.on("/api/files/bulk-delete", HTTP_POST, handleFileBulkDelete);

  // API: Make Directory (Protected)
  server.on("/api/files/mkdir", HTTP_POST, handleMakeDir);

  // API: Rename File (Protected)
  server.on("/api/files/rename", HTTP_POST, handleFileRename);

  // API: Upload File (Protected)
  PsychicUploadHandler *upHandler = new PsychicUploadHandler();
  upHandler->onUpload(handleFileUpload);
  upHandler->onRequest(handleUploadRequest);
  server.on("/api/files/upload", HTTP_POST, upHandler);

  // API: Read File (Protected)
  server.on("/api/files/read", HTTP_GET, handleFileRead);

  // API: Save File (Protected)
  server.on("/api/files/save", HTTP_POST, handleFileSave);

  // API: Restore from Trash (Protected)
  server.on("/api/trash/restore", HTTP_POST, handleTrashRestore);

  logInfo("[FILE_API] File routes updated for SD, Folders, Uploads, and Trash Restore");
}

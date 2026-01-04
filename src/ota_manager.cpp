#include "ota_manager.h"
#include "firmware_version.h"
#include "serial_logger.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>

static int ota_progress = 0;
static bool ota_active = false;
static bool ota_check_complete = false;
static UpdateCheckResult cached_result = {false, "", "", ""};

// The GitHub repository info
static const char* GITHUB_API_URL = "https://api.github.com/repos/sjfaustino/BISSO_E350_Controller/releases/latest";

void otaInit(void) {
    ota_progress = 0;
    ota_active = false;
    ota_check_complete = false;
    memset(&cached_result, 0, sizeof(cached_result));
}

UpdateCheckResult otaCheckForUpdate(void) {
    UpdateCheckResult result = {false, "", "", ""};
    
    WiFiClientSecure client;
    client.setInsecure(); // GitHub uses common CA, but for simplicity we skip cert validation here or could use root CA
    
    HTTPClient http;
    logInfo("[OTA] Checking GitHub for updates: %s", GITHUB_API_URL);
    
    if (http.begin(client, GITHUB_API_URL)) {
        http.addHeader("User-Agent", "ESP32-OTA-Client");
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                const char* tag_name = doc["tag_name"]; // e.g., "v1.0.1"
                if (tag_name) {
                    strncpy(result.latest_version, tag_name, sizeof(result.latest_version));
                    
                    // Parse GitHub version (format: "vX.Y.Z" or "X.Y.Z")
                    int gh_major = 0, gh_minor = 0, gh_patch = 0;
                    const char* ver_start = tag_name;
                    if (ver_start[0] == 'v' || ver_start[0] == 'V') ver_start++;
                    sscanf(ver_start, "%d.%d.%d", &gh_major, &gh_minor, &gh_patch);
                    
                    // Compare with current firmware version
                    bool is_newer = false;
                    if (gh_major > FIRMWARE_VERSION_MAJOR) {
                        is_newer = true;
                    } else if (gh_major == FIRMWARE_VERSION_MAJOR) {
                        if (gh_minor > FIRMWARE_VERSION_MINOR) {
                            is_newer = true;
                        } else if (gh_minor == FIRMWARE_VERSION_MINOR) {
                            if (gh_patch > FIRMWARE_VERSION_PATCH) {
                                is_newer = true;
                            }
                        }
                    }
                    
                    logInfo("[OTA] Current: v%d.%d.%d, GitHub: v%d.%d.%d, Newer: %s",
                            FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH,
                            gh_major, gh_minor, gh_patch,
                            is_newer ? "YES" : "NO");
                    
                    if (is_newer) {
                        result.available = true;
                        
                        // Find the firmware.bin asset
                        JsonArray assets = doc["assets"];
                        for (JsonObject asset : assets) {
                            const char* name = asset["name"];
                            if (name && strstr(name, ".bin") != NULL) {
                                strncpy(result.download_url, asset["browser_download_url"], sizeof(result.download_url));
                                break;
                            }
                        }
                        
                        const char* body = doc["body"];
                        if (body) {
                            strncpy(result.release_notes, body, sizeof(result.release_notes));
                        }
                    }
                }
            } else {
                 logError("[OTA] JSON parse failed: %s", error.c_str());
            }
        } else {
            logError("[OTA] HTTP GET failed, error: %s", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
    
    return result;
}

static void ota_task(void* pvParameters) {
    char* url = (char*)pvParameters;
    ota_active = true;
    ota_progress = 0;
    
    logInfo("[OTA] Starting firmware download from: %s", url);
    
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    if (http.begin(client, url)) {
        // Handle redirects
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            if (contentLength > 0) {
                logInfo("[OTA] File size: %d bytes", contentLength);
                
                if (Update.begin(contentLength)) {
                    WiFiClient* stream = http.getStreamPtr();
                    size_t written = Update.writeStream(*stream);
                    
                    if (written == (size_t)contentLength) {
                        logInfo("[OTA] Written: %u successfully", written);
                    } else {
                        logError("[OTA] Written only : %u/%u", written, contentLength);
                    }
                    
                    if (Update.end()) {
                        logInfo("[OTA] Update finished successfully!");
                        if (Update.isFinished()) {
                            logInfo("[OTA] Update fully finished. Rebooting...");
                            vTaskDelay(2000 / portTICK_PERIOD_MS);
                            ESP.restart();
                        }
                    } else {
                        logError("[OTA] Update failed! Error: %s", Update.errorString());
                    }
                } else {
                    logError("[OTA] Not enough space to begin update");
                }
            } else {
                logError("[OTA] Content-Length was zero");
            }
        } else {
            logError("[OTA] Download failed, HTTP: %d", httpCode);
        }
        http.end();
    }
    
    free(url);
    ota_active = false;
    vTaskDelete(NULL);
}

bool otaPerformUpdate(const char* download_url) {
    if (ota_active) return false;
    
    char* url_copy = strdup(download_url);
    if (!url_copy) return false;
    
    // Create background task for update so we don't block the web server
    xTaskCreate(ota_task, "ota_task", 8192, url_copy, 5, NULL);
    return true;
}

int otaGetProgress(void) {
    if (!ota_active) return 0;
    // Update.progress() / Update.size()
    return ota_progress;
}

bool otaIsUpdating(void) {
    return ota_active;
}

// --- Background Check Task ---

static void ota_check_task(void* pvParameters) {
    // Wait a bit for network to stabilize
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    logInfo("[OTA] Starting background update check...");
    cached_result = otaCheckForUpdate();
    ota_check_complete = true;
    
    if (cached_result.available) {
        logInfo("[OTA] Update available: %s", cached_result.latest_version);
    } else {
        logInfo("[OTA] Firmware is up to date");
    }
    
    vTaskDelete(NULL);
}

void otaStartBackgroundCheck(void) {
    if (ota_check_complete) return; // Already checked
    
    xTaskCreate(ota_check_task, "ota_check", 8192, NULL, 3, NULL);
}

const UpdateCheckResult* otaGetCachedResult(void) {
    return &cached_result;
}

bool otaCheckComplete(void) {
    return ota_check_complete;
}

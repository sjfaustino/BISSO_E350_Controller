/**
 * @file api_routes_system.cpp
 * @brief System, Config, OTA, and Fault API Routes
 * @details Handles /api/system/..., /api/config/..., /api/ota/..., /api/faults/...
 */

#include "api_routes.h"
#include "api_config.h"
#include "config_unified.h"
#include "config_keys.h"
#include "fault_logging.h"
#include "ota_manager.h"
#include "rs485_autodetect.h"
#include "firmware_version.h"
#include "serial_logger.h"
#include <ArduinoJson.h>
#include <time.h>

void registerSystemRoutes(PsychicHttpServer& server) {
    
    // GET /api/config/get?category=N
    server.on("/api/config/get", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        int category = 0;
        if (request->hasParam("category")) {
            category = request->getParam("category")->value().toInt();
        }
        
        logDebug("[WEB] Config GET: category=%d", category);
        
        JsonDocument doc;
        JsonDocument configDoc;
        if (apiConfigGet((config_category_t)category, configDoc)) {
            doc["success"] = true;
            doc["config"] = configDoc.as<JsonVariant>();
            return sendJsonResponse(response, doc);
        }
        
        logWarning("[WEB] Config GET failed: category %d not found", category);
        return response->send(404, "application/json", "{\"success\":false,\"error\":\"Not found\"}");
    });
    
    // POST /api/config/set
    server.on("/api/config/set", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        int category = doc["category"] | 0;
        const char* key = doc["key"] | "";
        JsonVariant value = doc["value"];
        
        char error_msg[128] = {0};
        if (!apiConfigValidate((config_category_t)category, key, value, error_msg, sizeof(error_msg))) {
            JsonDocument resp;
            resp["error"] = error_msg;
            return sendJsonResponse(response, resp, 400);
        }
        
        if (apiConfigSet((config_category_t)category, key, value)) {
            apiConfigSave();
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(500, "application/json", "{\"error\":\"Failed to set config\"}");
    });

    // GET /api/config
    server.on("/api/config", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        apiConfigGet(CONFIG_CATEGORY_MOTION, doc);
        apiConfigGet(CONFIG_CATEGORY_VFD, doc);
        apiConfigGet(CONFIG_CATEGORY_ENCODER, doc);
        
        return sendJsonResponse(response, doc);
    });

    // POST /api/config
    server.on("/api/config", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        deserializeJson(doc, request->body());
        
        if (doc["key"].is<const char*>() && doc["value"].is<const char*>()) {
            const char* key = doc["key"];
            const char* value = doc["value"];
            
            if (strchr(value, '.')) configSetFloat(key, atof(value));
            else configSetInt(key, atoi(value));
            
            configUnifiedSave();
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"Missing key/value\"}");
    });

    // POST /api/config/batch
    server.on("/api/config/batch", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, request->body());
        if (err) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        int count = 0;
        JsonObject obj = doc.as<JsonObject>();
        for (JsonPair kv : obj) {
            const char* key = kv.key().c_str();
            JsonVariant val = kv.value();
            
            // Validate I2C speed (only 100000 or 400000 Hz allowed)
            if (strcmp(key, KEY_I2C_SPEED) == 0) {
                int32_t speed = val.as<int32_t>();
                if (speed != 100000 && speed != 400000) {
                    logWarning("[WEB] Invalid I2C speed %d, using 100000", speed);
                    speed = 100000;  // Default to standard mode
                }
                configSetInt(key, speed);
                count++;
                continue;
            }
            
            if (val.is<int>() || val.is<long>()) {
                configSetInt(key, val.as<int32_t>());
            } else if (val.is<float>() || val.is<double>()) {
                configSetFloat(key, val.as<float>());
            } else if (val.is<const char*>()) {
                const char* strVal = val.as<const char*>();
                if (strchr(strVal, '.')) configSetFloat(key, atof(strVal));
                else configSetInt(key, atoi(strVal));
            }
            count++;
        }
        
        configUnifiedSave();
        logInfo("[WEB] Batch config saved %d keys", count);
        
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"count\":%d}", count);
        return response->send(200, "application/json", resp);
    });

    // GET /api/config/backup
    server.on("/api/config/backup", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        apiConfigPopulate(doc);
        
        time_t now;
        time(&now);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        doc["timestamp"] = timeStr;
        
        char verStr[32];
        snprintf(verStr, sizeof(verStr), "v%d.%d.%d", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);
        doc["firmware"] = verStr;
        
        char filename[128];
        char fileTime[32];
        strftime(fileTime, sizeof(fileTime), "%Y%m%d-%H%M%S", gmtime(&now));
        snprintf(filename, sizeof(filename), "attachment; filename=\"config-backup-%s.json\"", fileTime);
        
        response->addHeader("Content-Disposition", filename);
        return sendJsonResponse(response, doc);
    });

    // POST /api/config/restore
    server.on("/api/config/restore", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
         JsonDocument doc;
         DeserializationError error = deserializeJson(doc, request->body());
         if (error) {
             return response->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
         }
         
         if (apiConfigImportJSON(doc)) {
             configUnifiedSave(); // Persist changes
             return response->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration restored. Rebooting...\"}");
         } else {
             return response->send(400, "application/json", "{\"success\":false,\"error\":\"Import failed\"}");
         }
    });

    // POST /api/config/detect-rs485
    server.on("/api/config/detect-rs485", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        int32_t baud = rs485AutodetectBaud();
        
        char json[128];
        if (baud > 0) {
            snprintf(json, sizeof(json), "{\"success\":true, \"baud\": %lu}", (unsigned long)baud);
            return response->send(200, "application/json", json);
        } else if (baud == -1) {
            return response->send(200, "application/json", "{\"success\":false, \"error\": \"No RS485 devices are enabled.\"}");
        } else {
            return response->send(200, "application/json", "{\"success\":false, \"error\": \"No RS485 devices found\"}");
        }
    });

    // GET /api/faults
    server.on("/api/faults", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        response->setContentType("application/json");
        
        const char* header = "{ \"success\": true, \"faults\": [";
        response->sendChunk((uint8_t*)header, strlen(header));

        uint8_t count = faultGetHistoryCount();
        bool first = true;
        for (uint8_t i = 0; i < count; i++) {
            fault_entry_t entry;
            if (faultGetHistoryEntry(i, &entry)) {
                if (!first) response->sendChunk((uint8_t*)",", 1);
                first = false;

                char buf[512];
                size_t len = snprintf(buf, sizeof(buf),
                    "{\"code\":%d,\"description\":\"%s\",\"severity\":\"%s\",\"timestamp\":%lu,\"message\":\"%s\"}",
                    entry.code, faultCodeToString(entry.code), faultSeverityToString(entry.severity),
                    (unsigned long)entry.timestamp, entry.message);
                response->sendChunk((uint8_t*)buf, len);
            }
        }
        
        const char* footer = "] }";
        response->sendChunk((uint8_t*)footer, strlen(footer));
        return response->finishChunking();
    });

    // DELETE /api/faults
    server.on("/api/faults", HTTP_DELETE, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        faultClearHistory(); 
        return response->send(200, "application/json", "{\"success\":true, \"message\":\"Fault logs cleared\"}");
    });

    // POST /api/faults/clear
    server.on("/api/faults/clear", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        faultClearHistory();
        return response->send(200, "application/json", "{\"success\":true}");
    });

    // GET /api/ota/check (OPTIMIZED: snprintf)
    server.on("/api/ota/check", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        const UpdateCheckResult* res = otaGetCachedResult();
        
        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            "{\"check_complete\":%s,\"available\":%s,\"latest_version\":\"%s\",\"url\":\"%s\",\"notes\":\"%s\"}",
            otaCheckComplete() ? "true" : "false",
            res->available ? "true" : "false",
            res->latest_version,
            res->download_url,
            res->release_notes
        );
        
        return response->send(200, "application/json", buffer);
    });

    // GET /api/ota/latest (OPTIMIZED: snprintf)
    server.on("/api/ota/latest", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        const UpdateCheckResult* result = otaGetCachedResult();
        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            "{\"available\":%s,\"latest_version\":\"%s\",\"download_url\":\"%s\",\"release_notes\":\"%s\"}",
            result->available ? "true" : "false",
            result->latest_version,
            result->download_url,
            result->release_notes
        );
        
        return response->send(200, "application/json", buffer);
    });
    
    // POST /api/ota/update
    server.on("/api/ota/update", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        deserializeJson(doc, request->body());
        
        const char* url = nullptr;
        if (doc["url"].is<const char*>()) {
            url = doc["url"];
        } else {
            const UpdateCheckResult* result = otaGetCachedResult();
            if (result->available && strlen(result->download_url) > 0) {
                url = result->download_url;
            }
        }
        
        if (!url) {
             return response->send(400, "application/json", "{\"error\":\"No update URL available\"}");
        }
        
        if (otaPerformUpdate(url)) {
            return response->send(200, "application/json", "{\"success\":true, \"message\":\"Update started\"}");
        } else {
            return response->send(500, "application/json", "{\"error\":\"Failed to start update\"}");
        }
    });
    
    // GET /api/ota/status (OPTIMIZED: snprintf)
    server.on("/api/ota/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "{\"updating\":%s,\"progress\":%d}",
            otaIsUpdating() ? "true" : "false",
            otaGetProgress()
        );
        
        return response->send(200, "application/json", buffer);
    });

    // POST /api/system/reboot
    server.on("/api/system/reboot", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        esp_err_t err = response->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
        delay(100);
        ESP.restart();
        return err;
    });
    
    logDebug("[WEB] System routes registered");
}

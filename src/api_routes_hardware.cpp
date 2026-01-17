/**
 * @file api_routes_hardware.cpp
 * @brief Hardware I/O and Pin Mapping API Routes
 * @details Handles /api/hardware/..., /api/io/..., /api/logs/...
 */

#include "api_routes.h"
#include "plc_iface.h"
#include "board_inputs.h"
#include "hardware_config.h"
#include "config_unified.h"
#include "config_keys.h"
#include "yhtc05_modbus.h"
#include "boot_validation.h"
#include "serial_logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

void registerHardwareRoutes(PsychicHttpServer& server) {
    
    // GET /api/io/status
    server.on("/api/io/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        doc["success"] = true;
        
        uint8_t in_bits = elboI73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        
        doc["estop"] = (board_in & 0x08) != 0; 
        doc["door"] = (board_in & 0x10) != 0;
        doc["probe"] = (board_in & 0x20) != 0;
        doc["limit_x"] = (board_in & 0x01) != 0;
        doc["limit_y"] = (board_in & 0x02) != 0;
        doc["limit_z"] = (board_in & 0x04) != 0;
        
        doc["spindle_on"] = (out_bits & 0x01) == 0;
        doc["coolant_on"] = (out_bits & 0x02) == 0;
        doc["vacuum_on"] = (out_bits & 0x04) == 0;
        doc["alarm_on"] = (out_bits & 0x80) == 0;

        doc["raw_in"] = in_bits;
        doc["raw_out"] = out_bits;
        doc["raw_board"] = board_in;

        return sendJsonResponse(response, doc);
    });
    
    // GET /api/hardware/io
    server.on("/api/hardware/io", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        doc["success"] = true;
        
        uint8_t in_bits = elboI73GetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        
        JsonArray inputs = doc["inputs"].to<JsonArray>();
        for (int i = 0; i < 8; i++) {
            JsonObject in = inputs.add<JsonObject>();
            in["state"] = (in_bits & (1 << i)) != 0;
            in["name"] = String("I73-") + i;
        }
        for (int i = 0; i < 8; i++) {
            JsonObject in = inputs.add<JsonObject>();
            in["state"] = (board_in & (1 << i)) != 0;
            in["name"] = String("B-X") + (i+1);
        }

        JsonArray outputs = doc["outputs"].to<JsonArray>();
        for (int i = 0; i < 8; i++) {
            JsonObject out = outputs.add<JsonObject>();
            out["state"] = (out_bits & (1 << i)) == 0;
            out["name"] = String("Y") + (i+1);
        }

        doc["estop"] = (board_in & 0x08) != 0;

        return sendJsonResponse(response, doc);
    });

    // GET /api/hardware/pins
    server.on("/api/hardware/pins", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        
        JsonArray pinsArray = doc["pins"].to<JsonArray>();
        for (size_t i = 0; i < PIN_COUNT; i++) {
            JsonObject p = pinsArray.add<JsonObject>();
            p["gpio"] = pinDatabase[i].gpio;
            p["silk"] = pinDatabase[i].silk;
            p["type"] = pinDatabase[i].type;
            p["note"] = pinDatabase[i].note;
        }

        JsonArray sigsArray = doc["signals"].to<JsonArray>();
        for (size_t i = 0; i < SIGNAL_COUNT; i++) {
            JsonObject s = sigsArray.add<JsonObject>();
            s["key"] = signalDefinitions[i].key;
            s["name"] = signalDefinitions[i].name;
            s["type"] = signalDefinitions[i].type;
            s["current_pin"] = getPin(signalDefinitions[i].key);
            s["default_pin"] = signalDefinitions[i].default_gpio;
        }

        return sendJsonResponse(response, doc);
    });

    // POST /api/hardware/pins
    server.on("/api/hardware/pins", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, request->body());
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }

        JsonObject assignments = doc.as<JsonObject>();
        bool all_ok = true;
        int count = 0;
        for (JsonPair kv : assignments) {
            if (!setPin(kv.key().c_str(), kv.value().as<int8_t>(), true)) {
                all_ok = false;
            }
            count++;
        }

        configUnifiedSave();
        logInfo("[WEB] Batch pin save: %d pins", count);

        if (all_ok) {
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"One or more assignments failed\"}");
    });

    // POST /api/hardware/pins/reset
    server.on("/api/hardware/pins/reset", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        for (size_t i = 0; i < SIGNAL_COUNT; i++) {
            char nvs_key[40];
            snprintf(nvs_key, sizeof(nvs_key), "pin_%s", signalDefinitions[i].key);
            configSetInt(nvs_key, -1);
        }
        configUnifiedSave();
        return response->send(200, "application/json", "{\"success\":true}");
    });

    // GET /api/hardware/tachometer
    server.on("/api/hardware/tachometer", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
         const yhtc05_state_t* state = yhtc05GetState();
         JsonDocument doc;
         doc["enabled"] = state->enabled;
         doc["rpm"] = state->rpm;
         doc["pulse_count"] = state->pulse_count;
         doc["peak_rpm"] = state->peak_rpm;
         doc["spinning"] = state->is_spinning;
         doc["stalled"] = state->is_stalled;
         doc["error_count"] = state->error_count;
         
         return sendJsonResponse(response, doc);
    });

    // GET /api/logs/boot - Stream boot log directly from filesystem
    server.on("/api/logs/boot", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        if (!LittleFS.exists("/bootlog.txt") || bootLogGetSize() == 0) {
            return response->send(200, "text/plain", "(No boot log available)");
        }
        
        // Stream the file directly to the socket - 0 extra heap used for content
        PsychicFileResponse streamResponse(response, LittleFS, "/bootlog.txt");
        return streamResponse.send();
    });
    
    // DELETE /api/logs/boot
    server.on("/api/logs/boot", HTTP_DELETE, [](PsychicRequest *request, PsychicResponse *response) {
        if (LittleFS.exists("/bootlog.txt")) {
            if (LittleFS.remove("/bootlog.txt")) {
                logInfo("[WEB] Boot log deleted");
                return response->send(200, "application/json", "{\"success\":true}");
            } else {
                return response->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to delete file\"}");
            }
        }
        return response->send(200, "application/json", "{\"success\":true,\"message\":\"No boot log to delete\"}");
    });
    
    logDebug("[WEB] Hardware routes registered");
}

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
#include <Wire.h>
#include "rs485_device_registry.h"

void registerHardwareRoutes(PsychicHttpServer& server) {
    
    // GET /api/io/status (OPTIMIZED: snprintf, no heap)
    server.on("/api/io/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        uint8_t in_bits = elboI73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        
        char buffer[384];
        snprintf(buffer, sizeof(buffer),
            "{\"success\":true,"
            "\"estop\":%s,\"door\":%s,\"probe\":%s,"
            "\"limit_x\":%s,\"limit_y\":%s,\"limit_z\":%s,"
            "\"spindle_on\":%s,\"coolant_on\":%s,\"vacuum_on\":%s,\"alarm_on\":%s,"
            "\"raw_in\":%u,\"raw_out\":%u,\"raw_board\":%u}",
            (board_in & 0x08) ? "true" : "false",
            (board_in & 0x10) ? "true" : "false",
            (board_in & 0x20) ? "true" : "false",
            (board_in & 0x01) ? "true" : "false",
            (board_in & 0x02) ? "true" : "false",
            (board_in & 0x04) ? "true" : "false",
            (out_bits & 0x01) == 0 ? "true" : "false",
            (out_bits & 0x02) == 0 ? "true" : "false",
            (out_bits & 0x04) == 0 ? "true" : "false",
            (out_bits & 0x80) == 0 ? "true" : "false",
            in_bits, out_bits, board_in
        );

        return response->send(200, "application/json", buffer);
    });
    
    // GET /api/hardware/io (OPTIMIZED: snprintf, no heap)
    server.on("/api/hardware/io", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        uint8_t in_bits = elboI73GetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        
        char buffer[1536];
        char* p = buffer;
        size_t remain = sizeof(buffer);
        int n;

        n = snprintf(p, remain, "{\"success\":true,\"inputs\":[");
        if (n > 0) { p += n; remain -= n; }

        // I73 Inputs
        for (int i = 0; i < 8; i++) {
            n = snprintf(p, remain, "{\"state\":%s,\"name\":\"I73-%d\"},", 
                (in_bits & (1 << i)) != 0 ? "true" : "false", i);
            if (n > 0) { p += n; remain -= n; }
        }
        // Board Inputs
        for (int i = 0; i < 8; i++) {
            n = snprintf(p, remain, "{\"state\":%s,\"name\":\"B-X%d\"}%s", 
                (board_in & (1 << i)) != 0 ? "true" : "false", i+1, (i < 7) ? "," : "");
            if (n > 0) { p += n; remain -= n; }
        }

        n = snprintf(p, remain, "],\"outputs\":[");
        if (n > 0) { p += n; remain -= n; }

        // Outputs
        for (int i = 0; i < 8; i++) {
            n = snprintf(p, remain, "{\"state\":%s,\"name\":\"Y%d\"}%s", 
                (out_bits & (1 << i)) == 0 ? "true" : "false", i+1, (i < 7) ? "," : "");
            if (n > 0) { p += n; remain -= n; }
        }

        snprintf(p, remain, "],\"estop\":%s}", (board_in & 0x08) != 0 ? "true" : "false");
        
        return response->send(200, "application/json", buffer);
    });

    // GET /api/hardware/pins (OPTIMIZED: Chunked streaming, no large buffer)
    server.on("/api/hardware/pins", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        response->setContentType("application/json");

        const char* header = "{\"success\":true,\"pins\":[";
        response->sendChunk((uint8_t*)header, strlen(header));

        for (size_t i = 0; i < PIN_COUNT; i++) {
            char buf[256];
            size_t len = snprintf(buf, sizeof(buf), "{\"gpio\":%d,\"silk\":\"%s\",\"type\":\"%s\",\"note\":\"%s\"}%s",
                pinDatabase[i].gpio, pinDatabase[i].silk, pinDatabase[i].type, 
                pinDatabase[i].note ? pinDatabase[i].note : "", (i < PIN_COUNT - 1) ? "," : "");
            response->sendChunk((uint8_t*)buf, len);
        }

        const char* signals_start = "],\"signals\":[";
        response->sendChunk((uint8_t*)signals_start, strlen(signals_start));

        for (size_t i = 0; i < SIGNAL_COUNT; i++) {
            char buf[256];
            size_t len = snprintf(buf, sizeof(buf), "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"current_pin\":%d,\"default_pin\":%d}%s",
                signalDefinitions[i].key, signalDefinitions[i].name, signalDefinitions[i].type,
                getPin(signalDefinitions[i].key), signalDefinitions[i].default_gpio,
                (i < SIGNAL_COUNT - 1) ? "," : "");
            response->sendChunk((uint8_t*)buf, len);
        }

        const char* footer = "]}";
        response->sendChunk((uint8_t*)footer, strlen(footer));
        
        return response->finishChunking();
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
    
    // POST /api/hardware/i2c/test - Comprehensive I2C bus scan
    server.on("/api/hardware/i2c/test", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        
        // Define all known I2C devices to scan
        struct I2CDevice {
            uint8_t addr;
            const char* name;
        };
        
        const I2CDevice devices[] = {
            {0x21, "I73 Input"},      // Limit switches & sensors (PCF8574)
            {0x22, "Board Inputs"},   // KC868-A16 onboard inputs
            {0x24, "Q73 Output"},     // Relays & VFD control (PCF8574)
            {0x27, "LCD Display"},    // LCD backpack (PCF8574)
            {0x3F, "LCD Alt"}         // LCD backpack alternate address (PCF8574A)
        };
        
        JsonArray found = doc["devices"].to<JsonArray>();
        int count = 0;
        
        for (const auto& dev : devices) {
            Wire.beginTransmission(dev.addr);
            uint8_t error = Wire.endTransmission();
            if (error == 0) {
                JsonObject d = found.add<JsonObject>();
                char addr_str[6];
                snprintf(addr_str, sizeof(addr_str), "0x%02X", dev.addr);
                d["address"] = addr_str;
                d["name"] = dev.name;
                count++;
            }
        }
        
        doc["success"] = (count > 0);
        doc["count"] = count;
        
        if (count > 0) {
            logInfo("[WEB] I2C scan: %d devices found", count);
        } else {
            doc["error"] = "No I2C devices found";
            logWarning("[WEB] I2C scan: No devices found");
        }
        
        return sendJsonResponse(response, doc);
    });
    
    // GET /api/hardware/rs485/status - Get RS485 bus health
    server.on("/api/hardware/rs485/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        JsonDocument doc;
        
        const rs485_registry_state_t* state = rs485GetState();
        uint8_t device_count = 0;
        rs485_device_t** devices = rs485GetDevices(&device_count);
        
        // Bus status
        uint32_t now = millis();
        uint32_t time_since_response = now - state->last_successful_response_ms;
        // Healthy only if we have devices and they are responding
        bool healthy = (device_count > 0) && (time_since_response < RS485_WATCHDOG_TIMEOUT_MS);
        
        doc["healthy"] = healthy;
        doc["watchdog_alert"] = state->watchdog_alert_active;
        doc["device_count"] = device_count;
        doc["total_transactions"] = state->total_transactions;
        doc["total_errors"] = state->total_errors;
        doc["baud_rate"] = state->baud_rate;
        doc["bus_busy"] = state->bus_busy;
        
        // Calculate error rate
        float error_rate = 0.0f;
        if (state->total_transactions > 0) {
            error_rate = (float)state->total_errors / (float)state->total_transactions * 100.0f;
        }
        doc["error_rate"] = error_rate;
        
        // Device list
        JsonArray devArray = doc["devices"].to<JsonArray>();
        for (uint8_t i = 0; i < device_count; i++) {
            rs485_device_t* dev = devices[i];
            if (!dev) continue;
            
            JsonObject d = devArray.add<JsonObject>();
            d["name"] = dev->name;
            d["address"] = dev->slave_address;
            d["enabled"] = dev->enabled;
            d["poll_count"] = dev->poll_count;
            d["error_count"] = dev->error_count;
            d["consecutive_errors"] = dev->consecutive_errors;
            d["healthy"] = dev->consecutive_errors < 3;
        }
        
        return sendJsonResponse(response, doc);
    });
    
    logDebug("[WEB] Hardware routes registered");
}

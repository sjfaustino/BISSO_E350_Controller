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
        uint8_t aux_bits = elboQ73GetAuxRawState();
        
        char buffer[384];
        snprintf(buffer, sizeof(buffer),
            "{\"success\":true,"
            "\"estop\":%s,\"door\":%s,\"probe\":%s,"
            "\"limit_x\":%s,\"limit_y\":%s,\"limit_z\":%s,"
            "\"spindle_on\":%s,\"coolant_on\":%s,\"vacuum_on\":%s,\"alarm_on\":%s,"
            "\"raw_in\":%u,\"raw_out\":%u,\"raw_aux\":%u,\"raw_board\":%u}",
            (board_in & 0x08) ? "true" : "false", // X4
            (board_in & 0x10) ? "true" : "false", // X5
            (board_in & 0x20) ? "true" : "false", // X6
            (board_in & 0x01) ? "true" : "false", // X1
            (board_in & 0x02) ? "true" : "false", // X2
            (board_in & 0x04) ? "true" : "false", // X3
            (out_bits & 0x01) == 0 ? "true" : "false", // Y1 Spindle
            (aux_bits & 0x10) == 0 ? "true" : "false", // Y13 Coolant (Bit 4)
            (aux_bits & 0x20) == 0 ? "true" : "false", // Y14 Vacuum (Bit 5)
            (out_bits & 0x80) == 0 ? "true" : "false", // Y8 Alarm
            in_bits, out_bits, aux_bits, board_in
        );

        return response->send(200, "application/json", buffer);
    });
    
    // GET /api/hardware/io (STABLE: Chunked streaming, no large buffer)
    server.on("/api/hardware/io", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        uint8_t in_bits = elboI73GetRawState();
        uint8_t out_bits = elboQ73GetRawState();
        uint8_t board_in = boardInputsGetRawState();
        
        response->setContentType("application/json");
        response->sendHeaders();

        char chunk[256];
        int n;

        response->sendChunk((uint8_t*)"{\"success\":true,\"inputs\":[", 26);

        // Board Inputs (Bank 1: X1-X8 @ 0x22)
        for (int i = 0; i < 8; i++) {
            n = snprintf(chunk, sizeof(chunk), "{\"state\":%s,\"name\":\"X%d\"},", 
                (board_in & (1 << i)) != 0 ? "true" : "false", i + 1);
            response->sendChunk((uint8_t*)chunk, n);
        }
        // I73 Inputs (Bank 2: X9-X16 @ 0x21)
        for (int i = 0; i < 8; i++) {
            n = snprintf(chunk, sizeof(chunk), "{\"state\":%s,\"name\":\"X%d\"}%s", 
                (in_bits & (1 << i)) != 0 ? "true" : "false", i + 9, (i < 7) ? "," : "");
            response->sendChunk((uint8_t*)chunk, n);
        }

        response->sendChunk((uint8_t*)"],\"outputs\":[", 14);

        // Outputs (Bank 1: Y1-Y8)
        for (int i = 0; i < 8; i++) {
            n = snprintf(chunk, sizeof(chunk), "{\"state\":%s,\"name\":\"Y%d\"},", 
                (out_bits & (1 << i)) == 0 ? "true" : "false", i+1);
            response->sendChunk((uint8_t*)chunk, n);
        }
        // Outputs (Bank 2: Y9-Y16)
        uint8_t aux_bits = elboQ73GetAuxRawState();
        for (int i = 0; i < 8; i++) {
            n = snprintf(chunk, sizeof(chunk), "{\"state\":%s,\"name\":\"Y%d\"}%s", 
                (aux_bits & (1 << i)) == 0 ? "true" : "false", i+9, (i < 7) ? "," : "");
            response->sendChunk((uint8_t*)chunk, n);
        }

        n = snprintf(chunk, sizeof(chunk), "],\"estop\":%s}", (board_in & 0x08) != 0 ? "true" : "false");
        response->sendChunk((uint8_t*)chunk, n);
        
        return response->finishChunking();
    });

    // GET /api/hardware/pins (FIXED: Overflow-safe efficient chunked streaming)
    server.on("/api/hardware/pins", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        response->setContentType("application/json");
        response->sendHeaders(); // CRITICAL: Must send headers before first chunk

        response->sendChunk((uint8_t*)"{\"success\":true,\"pins\":[", 24);

        char chunk[1024];
        char* p = chunk;
        size_t rem = sizeof(chunk);

        auto flushIfFull = [&](size_t needed) {
            if (needed >= rem) {
                response->sendChunk((uint8_t*)chunk, p - chunk);
                p = chunk;
                rem = sizeof(chunk);
            }
        };

        for (size_t i = 0; i < PIN_COUNT; i++) {
            char entry[256];
            int n = snprintf(entry, sizeof(entry), "{\"gpio\":%d,\"silk\":\"%s\",\"type\":\"%s\",\"note\":\"%s\"}%s",
                pinDatabase[i].gpio, pinDatabase[i].silk, pinDatabase[i].type, 
                pinDatabase[i].note ? pinDatabase[i].note : "", (i < PIN_COUNT - 1) ? "," : "");
            
            flushIfFull(n);
            memcpy(p, entry, n);
            p += n; rem -= n;
        }
        response->sendChunk((uint8_t*)chunk, p - chunk);
        p = chunk; rem = sizeof(chunk);

        response->sendChunk((uint8_t*)"],\"signals\":[", 13);
        for (size_t i = 0; i < SIGNAL_COUNT; i++) {
            char entry[256];
            int n = snprintf(entry, sizeof(entry), "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"current_pin\":%d,\"default_pin\":%d}%s",
                signalDefinitions[i].key, signalDefinitions[i].name, signalDefinitions[i].type,
                getPin(signalDefinitions[i].key), signalDefinitions[i].default_gpio,
                (i < SIGNAL_COUNT - 1) ? "," : "");

            flushIfFull(n);
            memcpy(p, entry, n);
            p += n; rem -= n;
        }
        response->sendChunk((uint8_t*)chunk, p - chunk);
        response->sendChunk((uint8_t*)"]}", 2);
        
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
            JsonVariant val = kv.value();
            int16_t pin = val.as<int16_t>();

            // If value is null or invalid conversion to number (yielding 0), skip it.
            // Pin 0 is not a valid mappable pin for any of our signals.
            if (pin == 0 && val.isNull()) continue;

            if (!setPin(kv.key().c_str(), pin, true)) {
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
            // Use setPin to ensure consistent NVS key usage and logging
            setPin(signalDefinitions[i].key, -1, true);
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

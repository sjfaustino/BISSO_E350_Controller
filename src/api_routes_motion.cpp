/**
 * @file api_routes_motion.cpp
 * @brief Encoder and Motion Hardware API Routes
 * @details Handles /api/encoder/..., /api/hardware/wj66/...
 */

#include "api_routes.h"
#include "api_config.h"
#include "encoder_wj66.h"
#include "serial_logger.h"
#include <ArduinoJson.h>

void registerMotionRoutes(PsychicHttpServer& server) {
    
    // POST /api/encoder/calibrate
    server.on("/api/encoder/calibrate", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
        
        uint8_t axis = doc["axis"] | 0;
        uint16_t ppm = doc["ppm"] | 0;
        
        if (apiConfigCalibrateEncoder(axis, ppm)) {
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"Calibration failed\"}");
    });

    // POST /api/hardware/wj66/baud
    server.on("/api/hardware/wj66/baud", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        
        uint32_t baud = doc["baud"] | 0;
        if (wj66SetBaud(baud)) {
            return response->send(200, "application/json", "{\"success\":true}");
        }
        return response->send(400, "application/json", "{\"error\":\"Invalid baud rate\"}");
    });

    // POST /api/hardware/wj66/detect
    server.on("/api/hardware/wj66/detect", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        // Don't block the web server - spawn a background task
        xTaskCreate([](void* param) {
            logInfo("[WJ66] Autodetect task starting...");
            wj66Autodetect();
            logInfo("[WJ66] Autodetect task complete");
            vTaskDelete(NULL);
        }, "wj66_detect", 4096, NULL, 1, NULL);
        
        return response->send(200, "application/json", "{\"success\":true,\"message\":\"Detection started\"}");
    });
    
    logDebug("[WEB] Motion routes registered");
}

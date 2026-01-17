/**
 * @file api_routes_network.cpp
 * @brief Network and Time API Routes
 * @details Handles /api/network/..., /api/time/...
 */

#include "api_routes.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <WiFi.h>
#include <ETH.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

void registerNetworkRoutes(PsychicHttpServer& server) {
    
    // GET /api/network/status
    server.on("/api/network/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        JsonDocument doc;
        
        // WiFi Status
        bool wifi_connected = WiFi.isConnected();
        doc["wifi_connected"] = wifi_connected;
        doc["wifi_ssid"] = wifi_connected ? WiFi.SSID() : "--";
        doc["wifi_ip"] = wifi_connected ? WiFi.localIP().toString() : "0.0.0.0";
        doc["wifi_rssi"] = wifi_connected ? WiFi.RSSI() : -100;
        doc["wifi_mac"] = WiFi.macAddress();
        doc["wifi_gateway"] = wifi_connected ? WiFi.gatewayIP().toString() : "0.0.0.0";
        doc["wifi_dns"] = wifi_connected ? WiFi.dnsIP().toString() : "0.0.0.0";
        
        // Signal quality percentage
        int rssi = WiFi.RSSI();
        int quality = 0;
        if (rssi <= -100) quality = 0;
        else if (rssi >= -50) quality = 100;
        else quality = 2 * (rssi + 100);
        doc["signal_quality"] = quality;

        // Ethernet Status
        int eth_en = configGetInt(KEY_ETH_ENABLED, 0);
        bool eth_up = false;
        
        if (eth_en) {
            eth_up = ETH.linkUp();
            doc["eth_connected"] = eth_up;
            doc["eth_ip"] = eth_up ? ETH.localIP().toString() : "0.0.0.0";
            doc["eth_mac"] = ETH.macAddress();
            doc["eth_speed"] = ETH.linkSpeed();
            doc["eth_duplex"] = ETH.fullDuplex();
            doc["eth_gateway"] = eth_up ? ETH.gatewayIP().toString() : "0.0.0.0";
        } else {
            doc["eth_connected"] = false;
            doc["eth_ip"] = "0.0.0.0";
            doc["eth_mac"] = "00:00:00:00:00:00"; 
            doc["eth_speed"] = 0;
            doc["eth_duplex"] = false;
            doc["eth_gateway"] = "0.0.0.0";
        }
        
        doc["uptime_ms"] = millis();
        
        return sendJsonResponse(response, doc);
    });

    // POST /api/network/reconnect
    server.on("/api/network/reconnect", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        WiFi.disconnect();
        WiFi.begin();
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Reconnection triggered";
        
        return sendJsonResponse(response, doc);
    });

    // GET /api/time
    server.on("/api/time", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

        JsonDocument doc;
        doc["timestamp"] = (uint32_t)now;
        doc["formatted"] = buf;
        doc["synced"] = (timeinfo.tm_year > (2020 - 1900));

        return sendJsonResponse(response, doc);
    });

    // POST /api/time/sync
    server.on("/api/time/sync", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, request->body());
        
        if (error) {
            return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }

        if (!doc["timestamp"].is<JsonVariant>()) {
            return response->send(400, "application/json", "{\"error\":\"Missing timestamp\"}");
        }

        uint32_t timestamp = doc["timestamp"];
        struct timeval tv;
        tv.tv_sec = timestamp;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        time_t now = timestamp;
        struct tm* timeinfo = localtime(&now);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);

        String out = String("{\"status\":\"success\",\"time\":\"") + buf + "\"}";
        return response->send(200, "application/json", out.c_str());
    });
    
    logDebug("[WEB] Network routes registered");
}

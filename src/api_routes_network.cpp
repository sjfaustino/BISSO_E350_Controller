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
    
    // GET /api/network/status (OPTIMIZED: snprintf, no heap)
    server.on("/api/network/status", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        // WiFi Status
        bool wifi_connected = WiFi.isConnected();
        int rssi = WiFi.RSSI();
        int quality = 0;
        if (rssi <= -100) quality = 0;
        else if (rssi >= -50) quality = 100;
        else quality = 2 * (rssi + 100);

        // Ethernet Status
        int eth_en = configGetInt(KEY_ETH_ENABLED, 0);
        bool eth_up = eth_en ? ETH.linkUp() : false;
        
        char buffer[512];
        if (eth_en && eth_up) {
            snprintf(buffer, sizeof(buffer),
                "{\"wifi_connected\":%s,\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\",\"wifi_rssi\":%d,"
                "\"wifi_mac\":\"%s\",\"wifi_gateway\":\"%s\",\"wifi_dns\":\"%s\",\"signal_quality\":%d,"
                "\"eth_connected\":true,\"eth_ip\":\"%s\",\"eth_mac\":\"%s\",\"eth_speed\":%d,"
                "\"eth_duplex\":%s,\"eth_gateway\":\"%s\",\"uptime_ms\":%lu}",
                wifi_connected ? "true" : "false",
                wifi_connected ? WiFi.SSID().c_str() : "--",
                wifi_connected ? WiFi.localIP().toString().c_str() : "0.0.0.0",
                wifi_connected ? rssi : -100,
                WiFi.macAddress().c_str(),
                wifi_connected ? WiFi.gatewayIP().toString().c_str() : "0.0.0.0",
                wifi_connected ? WiFi.dnsIP().toString().c_str() : "0.0.0.0",
                quality,
                ETH.localIP().toString().c_str(),
                ETH.macAddress().c_str(),
                (int)ETH.linkSpeed(),
                ETH.fullDuplex() ? "true" : "false",
                ETH.gatewayIP().toString().c_str(),
                (unsigned long)millis()
            );
        } else {
            snprintf(buffer, sizeof(buffer),
                "{\"wifi_connected\":%s,\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\",\"wifi_rssi\":%d,"
                "\"wifi_mac\":\"%s\",\"wifi_gateway\":\"%s\",\"wifi_dns\":\"%s\",\"signal_quality\":%d,"
                "\"eth_connected\":false,\"eth_ip\":\"0.0.0.0\",\"eth_mac\":\"00:00:00:00:00:00\","
                "\"eth_speed\":0,\"eth_duplex\":false,\"eth_gateway\":\"0.0.0.0\",\"uptime_ms\":%lu}",
                wifi_connected ? "true" : "false",
                wifi_connected ? WiFi.SSID().c_str() : "--",
                wifi_connected ? WiFi.localIP().toString().c_str() : "0.0.0.0",
                wifi_connected ? rssi : -100,
                WiFi.macAddress().c_str(),
                wifi_connected ? WiFi.gatewayIP().toString().c_str() : "0.0.0.0",
                wifi_connected ? WiFi.dnsIP().toString().c_str() : "0.0.0.0",
                quality,
                (unsigned long)millis()
            );
        }
        
        return response->send(200, "application/json", buffer);
    });

    // POST /api/network/reconnect (OPTIMIZED: static string)
    server.on("/api/network/reconnect", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        WiFi.disconnect();
        WiFi.begin();
        
        return response->send(200, "application/json", "{\"success\":true,\"message\":\"Reconnection triggered\"}");
    });

    // GET /api/time (OPTIMIZED: snprintf, no heap)
    server.on("/api/time", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        char formatted[32];
        strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "{\"timestamp\":%lu,\"formatted\":\"%s\",\"synced\":%s}",
            (unsigned long)now,
            formatted,
            (timeinfo.tm_year > (2020 - 1900)) ? "true" : "false"
        );

        return response->send(200, "application/json", buffer);
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

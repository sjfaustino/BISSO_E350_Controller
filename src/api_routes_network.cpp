/**
 * @file api_routes_network.cpp
 * @brief Network and Time API Routes
 * @details Handles /api/network/..., /api/time/...
 */

#include "api_routes.h"
#include "network_manager.h"
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

        // Ethernet Status - Use NetworkManager instead of ETH object to avoid null handle log spam
        bool eth_connected = networkManager.isEthernetConnected();
        
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "{\"wifi_connected\":%s,\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\",\"wifi_rssi\":%d,"
            "\"wifi_mac\":\"%s\",\"wifi_gateway\":\"%s\",\"wifi_dns\":\"%s\",\"signal_quality\":%d,"
            "\"eth_connected\":%s,\"eth_ip\":\"%s\",\"eth_mac\":\"%s\",\"eth_speed\":%d,"
            "\"uptime_ms\":%lu}",
            wifi_connected ? "true" : "false",
            wifi_connected ? WiFi.SSID().c_str() : "--",
            wifi_connected ? WiFi.localIP().toString().c_str() : "0.0.0.0",
            wifi_connected ? rssi : -100,
            WiFi.macAddress().c_str(),
            wifi_connected ? WiFi.gatewayIP().toString().c_str() : "0.0.0.0",
            wifi_connected ? WiFi.dnsIP().toString().c_str() : "0.0.0.0",
            quality,
            eth_connected ? "true" : "false",
            eth_connected ? networkManager.getEthernetIP().c_str() : "0.0.0.0",
            networkManager.getEthernetMAC().c_str(),
            (int)networkManager.getEthernetLinkSpeed(),
            (unsigned long)millis()
        );
        
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

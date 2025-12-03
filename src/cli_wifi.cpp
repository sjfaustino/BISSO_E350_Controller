#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h" 
#include <WiFi.h>
#include <Arduino.h>

static const char* wifiGetStatusString(wl_status_t status) {
    switch (status) {
        case WL_CONNECTED: return "CONNECTED";
        case WL_DISCONNECTED: return "DISCONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        default: return "OTHER";
    }
}

void cmd_wifi_scan(int argc, char** argv) {
    Serial.println("[WIFI] Scanning...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    if (n == 0) Serial.println("[WIFI] No networks found.");
    else {
        Serial.printf("[WIFI] Found %d networks:\n", n);
        for (int i = 0; i < n; ++i) {
            Serial.printf("  %2d: %-32.32s | %d dBm\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
            delay(10);
        }
    }
    WiFi.scanDelete();
}

void cmd_wifi_connect(int argc, char** argv) {
    if (argc < 4) {
        Serial.println("[WIFI] Usage: wifi connect <ssid> <password>");
        return;
    }
    Serial.printf("[WIFI] Connecting to '%s'...\n", argv[2]);
    WiFi.mode(WIFI_STA);
    WiFi.begin(argv[2], argv[3]);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
        delay(500);
        watchdogFeed("CLI"); // Prevent WDT reset
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] [OK] Connected!");
        Serial.print("[WIFI] IP: "); Serial.println(WiFi.localIP());
    } else {
        Serial.printf("[WIFI] [FAIL] Connection failed: %s\n", wifiGetStatusString(WiFi.status()));
    }
}

void cmd_wifi_status(int argc, char** argv) {
    Serial.println("\n[WIFI] === Status ===");
    Serial.printf("  Status: %s\n", wifiGetStatusString(WiFi.status()));
    Serial.printf("  MAC:    %s\n", WiFi.macAddress().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  SSID:   %s\n", WiFi.SSID().c_str());
        Serial.printf("  IP:     %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI:   %d dBm\n", WiFi.RSSI());
    }
}

void cmd_wifi_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[WIFI] === Network Management ===");
        Serial.println("Usage: wifi [scan | connect | status]");
        return;
    }
    if (strcmp(argv[1], "scan") == 0) cmd_wifi_scan(argc, argv);
    else if (strcmp(argv[1], "connect") == 0) cmd_wifi_connect(argc, argv);
    else if (strcmp(argv[1], "status") == 0) cmd_wifi_status(argc, argv);
    else Serial.printf("[WIFI] Error: Unknown parameter '%s'.\n", argv[1]);
}

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi management", cmd_wifi_main);
}
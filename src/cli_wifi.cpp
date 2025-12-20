#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "config_unified.h"  // For OTA password config
#include "config_keys.h"     // For KEY_OTA_PASSWORD
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

    // CRITICAL FIX: Non-blocking connection to prevent freezing motion control
    // WiFi connects in background - don't block CLI task with delay() loops
    Serial.println("[WIFI] [OK] Connection initiated (non-blocking)");
    Serial.println("[WIFI] Note: WiFi connects in background during normal operation");
    Serial.println("[WIFI] Use 'wifi status' to check connection progress");
    Serial.println();
    Serial.println("[WIFI] SAFETY: This command does NOT block motion control");
    Serial.println("[WIFI] Connection will complete within 10-20 seconds");

    // Show immediate status
    Serial.printf("[WIFI] Current status: %s\n", wifiGetStatusString(WiFi.status()));
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

// SECURITY: OTA password management command
void cmd_ota_setpass(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[OTA] === OTA Password Management ===");
        Serial.println("Usage: ota_setpass <new_password>");
        Serial.println("Note: Password must be at least 8 characters");
        Serial.println("      Requires reboot to take effect");

        // Show current status
        int ota_pw_changed = configGetInt(KEY_OTA_PW_CHANGED, 0);
        if (ota_pw_changed == 0) {
            Serial.println("\nCurrent: DEFAULT PASSWORD (insecure!)");
        } else {
            Serial.println("\nCurrent: CUSTOM PASSWORD (secure)");
        }
        return;
    }

    const char* new_password = argv[1];

    // Validate password strength
    if (strlen(new_password) < 8) {
        Serial.println("[OTA] [ERR] Password must be at least 8 characters");
        return;
    }

    // Save to NVS
    configSetString(KEY_OTA_PASSWORD, new_password);
    configSetInt(KEY_OTA_PW_CHANGED, 1);
    configUnifiedSave();

    Serial.println("[OTA] [OK] Password updated successfully");
    Serial.println("[OTA] [WARN] Reboot required for changes to take effect");
    Serial.println("[OTA] Use command: reboot");
}

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi management", cmd_wifi_main);
    cliRegisterCommand("ota_setpass", "Set OTA update password", cmd_ota_setpass);
}
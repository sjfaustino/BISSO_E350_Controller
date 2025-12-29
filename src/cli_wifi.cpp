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
    logPrintln("[WIFI] Scanning...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    if (n == 0) logPrintln("[WIFI] No networks found.");
    else {
        logPrintf("[WIFI] Found %d networks:\r\n", n);
        for (int i = 0; i < n; ++i) {
            logPrintf("  %2d: %-32.32s | %d dBm\r\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
            delay(10);
        }
    }
    WiFi.scanDelete();
}

void cmd_wifi_connect(int argc, char** argv) {
    if (argc < 4) {
        logPrintln("[WIFI] Usage: wifi connect <ssid> <password>");
        return;
    }
    logPrintf("[WIFI] Connecting to '%s'...\n", argv[2]);
    WiFi.mode(WIFI_STA);
    WiFi.begin(argv[2], argv[3]);

    // CRITICAL FIX: Non-blocking connection to prevent freezing motion control
    // WiFi connects in background - don't block CLI task with delay() loops
    logPrintln("[WIFI] [OK] Connection initiated (non-blocking)");
    logPrintln("[WIFI] Note: WiFi connects in background during normal operation");
    logPrintln("[WIFI] Use 'wifi status' to check connection progress");
    logPrintln("");
    logPrintln("[WIFI] SAFETY: This command does NOT block motion control");
    logPrintln("[WIFI] Connection will complete within 10-20 seconds");

    // Show immediate status
    logPrintf("[WIFI] Current status: %s\r\n", wifiGetStatusString(WiFi.status()));
}

void cmd_wifi_status(int argc, char** argv) {
    logPrintln("\n[WIFI] === Status ===");
    logPrintf("  Status: %s\r\n", wifiGetStatusString(WiFi.status()));
    logPrintf("  MAC:    %s\r\n", WiFi.macAddress().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        logPrintf("  SSID:   %s\r\n", WiFi.SSID().c_str());
        logPrintf("  IP:     %s\r\n", WiFi.localIP().toString().c_str());
        logPrintf("  RSSI:   %d dBm\r\n", WiFi.RSSI());
    }
}

void cmd_wifi_ap(int argc, char **argv) {
  if (argc < 3) {
    logPrintln("\n[WIFI] === AP Mode Management ===");
    logPrintln("Usage:");
    logPrintln("  wifi ap on            - Enable AP mode");
    logPrintln("  wifi ap off           - Disable AP mode");
    logPrintln("  wifi ap set <s|p> <v> - Set SSID(s) or Password(p)");
    logPrintln("  wifi ap status        - Show current AP configuration");
    return;
  }

  if (strcasecmp(argv[2], "on") == 0) {
    configSetInt(KEY_WIFI_AP_EN, 1);
    configUnifiedSave();
    logInfo("[WIFI] [OK] AP Mode enabled. Reboot required.");
  } else if (strcasecmp(argv[2], "off") == 0) {
    configSetInt(KEY_WIFI_AP_EN, 0);
    configUnifiedSave();
    logInfo("[WIFI] [OK] AP Mode disabled. Reboot required.");
  } else if (strcasecmp(argv[2], "status") == 0) {
    int en = configGetInt(KEY_WIFI_AP_EN, 1);
    const char *ssid = configGetString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
    logPrintf("[WIFI] AP Mode: %s\n", en ? "ENABLED" : "DISABLED");
    logPrintf("[WIFI] AP SSID: %s\n", ssid);
  } else if (strcasecmp(argv[2], "set") == 0) {
    if (argc < 5) {
      logError("[WIFI] Usage: wifi ap set <s|p> <value>");
      return;
    }
    if (strcasecmp(argv[3], "s") == 0) {
      configSetString(KEY_WIFI_AP_SSID, argv[4]);
      logInfo("[WIFI] [OK] AP SSID set to '%s'", argv[4]);
    } else if (strcasecmp(argv[3], "p") == 0) {
      if (strlen(argv[4]) < 8) {
        logError("[WIFI] AP Password must be at least 8 chars");
        return;
      }
      configSetString(KEY_WIFI_AP_PASS, argv[4]);
      logInfo("[WIFI] [OK] AP Password updated");
    }
    configUnifiedSave();
    logWarning("[WIFI] Reboot required for changes to take effect");
  }
}

void cmd_wifi_main(int argc, char **argv) {
  if (argc < 2) {
    logPrintln("\n[WIFI] === Network Management ===");
    logPrintln("Usage: wifi [scan | connect | status | ap]");
    return;
  }
  if (strcasecmp(argv[1], "scan") == 0)
    cmd_wifi_scan(argc, argv);
  else if (strcasecmp(argv[1], "connect") == 0)
    cmd_wifi_connect(argc, argv);
  else if (strcasecmp(argv[1], "status") == 0)
    cmd_wifi_status(argc, argv);
  else if (strcasecmp(argv[1], "ap") == 0)
    cmd_wifi_ap(argc, argv);
  else
    logWarning("[WIFI] Unknown parameter '%s'.", argv[1]);
}

// SECURITY: OTA password management command
void cmd_ota_setpass(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("\n[OTA] === OTA Password Management ===");
        logPrintln("Usage: ota_setpass <new_password>");
        logPrintln("Note: Password must be at least 8 characters");
        logPrintln("      Requires reboot to take effect");

        // Show current status
        int ota_pw_changed = configGetInt(KEY_OTA_PW_CHANGED, 0);
        if (ota_pw_changed == 0) {
            logPrintln("\nCurrent: DEFAULT PASSWORD (insecure!)");
        } else {
            logPrintln("\nCurrent: CUSTOM PASSWORD (secure)");
        }
        return;
    }

    const char* new_password = argv[1];

    // Validate password strength
    if (strlen(new_password) < 8) {
        logError("[OTA] Password must be at least 8 characters");
        return;
    }

    // Save to NVS
    configSetString(KEY_OTA_PASSWORD, new_password);
    configSetInt(KEY_OTA_PW_CHANGED, 1);
    configUnifiedSave();

    logInfo("[OTA] [OK] Password updated successfully");
    logWarning("[OTA] Reboot required for changes to take effect");
    logPrintln("[OTA] Use command: reboot");
}

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi management", cmd_wifi_main);
    cliRegisterCommand("ota_setpass", "Set OTA update password", cmd_ota_setpass);
}
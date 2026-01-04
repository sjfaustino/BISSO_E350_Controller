#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "config_unified.h"  // For OTA password config
#include "config_keys.h"     // For KEY_OTA_PASSWORD, KEY_ETH_*
#include "network_manager.h" // For Ethernet status
#include <WiFi.h>
#include <ETH.h>
#include <Arduino.h>
#include <ESP32Ping.h>

// Ethernet statistics
static uint32_t eth_connect_time = 0;
static uint32_t eth_error_count = 0;
static uint32_t eth_reconnect_count = 0;

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
    // Scan without disconnecting to avoid breaking existing sessions
    int n = WiFi.scanNetworks(false, false, false, 300); // Fast scan
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

// =============================================================================
// ETHERNET CLI COMMANDS
// =============================================================================

void cmd_eth_status(int argc, char** argv) {
    logPrintln("\n[ETH] === Ethernet Status ===");
    
    int enabled = configGetInt(KEY_ETH_ENABLED, 1);
    int dhcp = configGetInt(KEY_ETH_DHCP, 1);
    
    logPrintf("  Enabled:     %s\n", enabled ? "YES" : "NO");
    logPrintf("  Mode:        %s\n", dhcp ? "DHCP" : "Static IP");
    
    if (networkManager.isEthernetConnected()) {
        logPrintf("  Status:      CONNECTED\n");
        logPrintf("  IP:          %s\n", ETH.localIP().toString().c_str());
        logPrintf("  Gateway:     %s\n", ETH.gatewayIP().toString().c_str());
        logPrintf("  Subnet:      %s\n", ETH.subnetMask().toString().c_str());
        logPrintf("  DNS:         %s\n", ETH.dnsIP().toString().c_str());
        logPrintf("  MAC:         %s\n", ETH.macAddress().c_str());
        logPrintf("  Link Speed:  %d Mbps\n", networkManager.getEthernetLinkSpeed());
        logPrintf("  Duplex:      %s\n", ETH.fullDuplex() ? "Full" : "Half");
        
        // Uptime
        if (eth_connect_time > 0) {
            uint32_t uptime_sec = (millis() - eth_connect_time) / 1000;
            uint32_t hours = uptime_sec / 3600;
            uint32_t mins = (uptime_sec % 3600) / 60;
            uint32_t secs = uptime_sec % 60;
            logPrintf("  Uptime:      %02d:%02d:%02d\n", hours, mins, secs);
        }
    } else {
        logPrintf("  Status:      DISCONNECTED\n");
    }
    
    logPrintf("  Reconnects:  %lu\n", (unsigned long)eth_reconnect_count);
    logPrintf("  Errors:      %lu\n", (unsigned long)eth_error_count);
    
    // Static IP config if set
    if (!dhcp) {
        logPrintln("\n  Static Configuration:");
        logPrintf("    IP:      %s\n", configGetString(KEY_ETH_IP, "not set"));
        logPrintf("    Gateway: %s\n", configGetString(KEY_ETH_GW, "not set"));
        logPrintf("    Mask:    %s\n", configGetString(KEY_ETH_MASK, "255.255.255.0"));
        logPrintf("    DNS:     %s\n", configGetString(KEY_ETH_DNS, "8.8.8.8"));
    }
}

void cmd_eth_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("\n[ETH] === Ethernet Management ===");
        logPrintln("Usage:");
        logPrintln("  eth status                  - Show Ethernet status");
        logPrintln("  eth on                      - Enable Ethernet");
        logPrintln("  eth off                     - Disable Ethernet");
        logPrintln("  eth dhcp                    - Use DHCP (default)");
        logPrintln("  eth static <ip> <gw> [mask] - Set static IP");
        logPrintln("  eth dns <dns_ip>            - Set DNS server");
        logPrintln("\nExamples:");
        logPrintln("  eth static 192.168.1.100 192.168.1.1");
        logPrintln("  eth static 192.168.1.100 192.168.1.1 255.255.255.0");
        return;
    }
    
    if (strcasecmp(argv[1], "status") == 0) {
        cmd_eth_status(argc, argv);
    }
    else if (strcasecmp(argv[1], "on") == 0) {
        configSetInt(KEY_ETH_ENABLED, 1);
        configUnifiedSave();
        logInfo("[ETH] [OK] Ethernet enabled. Reboot required.");
    }
    else if (strcasecmp(argv[1], "off") == 0) {
        configSetInt(KEY_ETH_ENABLED, 0);
        configUnifiedSave();
        logInfo("[ETH] [OK] Ethernet disabled. Reboot required.");
    }
    else if (strcasecmp(argv[1], "dhcp") == 0) {
        configSetInt(KEY_ETH_DHCP, 1);
        configUnifiedSave();
        logInfo("[ETH] [OK] DHCP mode enabled. Reboot required.");
    }
    else if (strcasecmp(argv[1], "static") == 0) {
        if (argc < 4) {
            logError("[ETH] Usage: eth static <ip> <gateway> [mask]");
            return;
        }
        configSetString(KEY_ETH_IP, argv[2]);
        configSetString(KEY_ETH_GW, argv[3]);
        if (argc >= 5) {
            configSetString(KEY_ETH_MASK, argv[4]);
        } else {
            configSetString(KEY_ETH_MASK, "255.255.255.0");
        }
        configSetInt(KEY_ETH_DHCP, 0);
        configUnifiedSave();
        logInfo("[ETH] [OK] Static IP configured:");
        logPrintf("  IP:      %s\n", argv[2]);
        logPrintf("  Gateway: %s\n", argv[3]);
        logPrintf("  Mask:    %s\n", argc >= 5 ? argv[4] : "255.255.255.0");
        logWarning("[ETH] Reboot required for changes to take effect.");
    }
    else if (strcasecmp(argv[1], "dns") == 0) {
        if (argc < 3) {
            logError("[ETH] Usage: eth dns <dns_ip>");
            return;
        }
        configSetString(KEY_ETH_DNS, argv[2]);
        configUnifiedSave();
        logInfo("[ETH] [OK] DNS set to %s. Reboot required.", argv[2]);
    }
    else {
        logWarning("[ETH] Unknown command '%s'. Use 'eth' for help.", argv[1]);
    }
}

// Track Ethernet connect/disconnect for uptime
void ethTrackConnect() {
    eth_connect_time = millis();
    if (eth_connect_time > 0) eth_reconnect_count++;
}

void ethTrackError() {
    eth_error_count++;
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

void cmd_ping(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[PING] Usage: ping <host> [count]");
        return;
    }

    const char* host = argv[1];
    int count = (argc >= 3) ? atoi(argv[2]) : 4;
    
    if (count <= 0) count = 4;
    if (count > 20) count = 20;

    logPrintf("[PING] Pinging %s (%d times)...\n", host, count);

    int successful = 0;
    float total_time = 0;
    float min_time = 99999;
    float max_time = 0;

    for (int i = 0; i < count; i++) {
        // Feed watchdog during ping sequence
        watchdogFeed("cli");
        
        bool success = Ping.ping(host, 1);
        if (success) {
            float time = Ping.averageTime();
            logPrintf("  Reply from %s: time=%.1fms\n", host, time);
            successful++;
            total_time += time;
            if (time < min_time) min_time = time;
            if (time > max_time) max_time = time;
        } else {
            logPrintf("  Request timed out.\n");
        }
        delay(100);
    }

    if (successful > 0) {
        logPrintf("[PING] Statistics: Sent=%d, Received=%d, Lost=%d (%.0f%% loss)\n", 
                  count, successful, count - successful, (float)(count - successful) / count * 100);
        logPrintf("[PING] Round trip times: min=%.1fms, max=%.1fms, avg=%.1fms\n", 
                  min_time, max_time, total_time / successful);
    } else {
        logPrintf("[PING] Failed: %s is unreachable.\n", host);
    }
}

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi management", cmd_wifi_main);
    cliRegisterCommand("eth", "Ethernet management (KC868-A16)", cmd_eth_main);
    cliRegisterCommand("ota_setpass", "Set OTA update password", cmd_ota_setpass);
    cliRegisterCommand("ping", "Ping a host", cmd_ping);
}

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
    bool force = (argc >= 3 && strcasecmp(argv[2], "force") == 0);
    
    if (force) {
        logPrintln("Forcing scan by disconnecting first...");
        WiFi.disconnect();
        delay(500);
    }

    logPrintln("Scanning...");
    // Scan without disconnecting to avoid breaking existing sessions
    int n = WiFi.scanNetworks(false, false, false, 300); // Fast scan
    
    if (n < 0) {
        if (n == -1) { // WIFI_SCAN_RUNNING
            logPrintln("Scan already in progress.");
        } else {
            logPrintf("Scan failed (Error code: %d).\n", n);
            logPrintln("TIP: If you have invalid credentials saved, they might be blocking the scan.");
            logPrintln("TIP: Try 'wifi scan force' or 'wifi disconnect' first.");
        }
    } else if (n == 0) {
        logPrintln("No networks found.");
    } else {
        logPrintf("Found %d networks:\r\n", n);
        for (int i = 0; i < n; ++i) {
            logPrintf("  %2d: %-32.32s | %d dBm %s\r\n", 
                      i+1, 
                      WiFi.SSID(i).c_str(), 
                      WiFi.RSSI(i),
                      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "(Open)" : "(Encrypted)");
            delay(10);
        }
    }
    WiFi.scanDelete();
}

void cmd_wifi_disconnect(int argc, char** argv) {
    logPrintln("Disconnecting and stopping auto-reconnect...");
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, true); // eraseap = false, stopSTA = true? wait, signature is (eraseap, set_at_startup) or similar
    // Actually in ESP32 Arduino: WiFi.disconnect(bool wifioff = false, bool eraseap = false)
    WiFi.disconnect(false, false);
    logPrintln("Background connection loop stopped.");
    logPrintln("Use 'wifi connect' or 'wifi scan' now.");
}

void cmd_wifi_connect(int argc, char** argv) {
    if (argc < 4) {
        logPrintln("Usage: wifi connect <ssid> <password>");
        return;
    }
    logPrintf("Connecting to '%s'...\n", argv[2]);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // Re-enable auto-reconnect
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // Set maximum power for best range
    WiFi.begin(argv[2], argv[3]);


    // CRITICAL FIX: Non-blocking connection to prevent freezing motion control
    // WiFi connects in background - don't block CLI task with delay() loops
    logPrintln("Connection initiated (non-blocking)");
    logPrintln("Note: WiFi connects in background during normal operation");
    logPrintln("Use 'wifi status' to check connection progress");
    logPrintln("");
    logPrintln("SAFETY: This command does NOT block motion control");
    logPrintln("Connection will complete within 10-20 seconds");

    // Show immediate status
    logPrintf("Current status: %s\r\n", wifiGetStatusString(WiFi.status()));
}

void cmd_wifi_status(int argc, char** argv) {
    logPrintln("\n=== Status ===");
    logPrintf("  Status: %s\r\n", wifiGetStatusString(WiFi.status()));
    logPrintf("  MAC:    %s\r\n", WiFi.macAddress().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        logPrintf("  SSID:   %s\r\n", WiFi.SSID().c_str());
        logPrintf("  Channel:%d\r\n", WiFi.channel());
        logPrintf("  IP:     %s\r\n", WiFi.localIP().toString().c_str());
        logPrintf("  RSSI:   %d dBm\r\n", WiFi.RSSI());
    }
}

void cmd_wifi_ap(int argc, char **argv) {
  if (argc < 3) {
    logPrintln("\n=== AP Mode Management ===");
    CLI_USAGE("wifi", "ap [on|off|set|status]");
    CLI_HELP_LINE("on", "Enable AP mode");
    CLI_HELP_LINE("off", "Disable AP mode");
    CLI_HELP_LINE("set <s|p> <v>", "Set SSID(s) or Password(p)");
    CLI_HELP_LINE("status", "Show current AP configuration");
    return;
  }

  if (strcasecmp(argv[2], "on") == 0) {
    configSetInt(KEY_WIFI_AP_EN, 1);
    configUnifiedSave();
    logPrintf("AP Mode enabled. Reboot required.\n");
  } else if (strcasecmp(argv[2], "off") == 0) {
    configSetInt(KEY_WIFI_AP_EN, 0);
    configUnifiedSave();
    logPrintf("AP Mode disabled. Reboot required.\n");
  } else if (strcasecmp(argv[2], "status") == 0) {
    int en = configGetInt(KEY_WIFI_AP_EN, 1);
    const char *ssid = configGetString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
    logPrintf("AP Mode: %s\n", en ? "ENABLED" : "DISABLED");
    logPrintf("AP SSID: %s\n", ssid);
  } else if (strcasecmp(argv[2], "set") == 0) {
    if (argc < 5) {
      logError("Usage: wifi ap set <s|p> <value>");
      return;
    }
    if (strcasecmp(argv[3], "s") == 0) {
      configSetString(KEY_WIFI_AP_SSID, argv[4]);
      logPrintf("AP SSID set to '%s'\n", argv[4]);
    } else if (strcasecmp(argv[3], "p") == 0) {
      if (strlen(argv[4]) < 8) {
        logError("AP Password must be at least 8 chars");
        return;
      }
      configSetString(KEY_WIFI_AP_PASS, argv[4]);
      logPrintf("AP Password updated\n");
    }
    configUnifiedSave();
    logPrintf("Reboot required for changes to take effect\n");
  }
}

void cmd_wifi_main(int argc, char **argv) {
  // Table-driven subcommand dispatch (P1: DRY improvement)
  static const cli_subcommand_t subcmds[] = {
      {"scan",       cmd_wifi_scan,       "Scan for networks"},
      {"connect",    cmd_wifi_connect,    "Connect to network"},
      {"disconnect", cmd_wifi_disconnect, "Disconnect/Stop auto-reconnect"},
      {"status",     cmd_wifi_status,     "Show connection status"},
      {"ap",      cmd_wifi_ap,      "Configure Access Point"}
  };

  cliDispatchSubcommand("", argc, argv, subcmds, 
                        sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

// =============================================================================
// ETHERNET CLI COMMANDS
// =============================================================================

void cmd_eth_status(int argc, char** argv) {
    logPrintln("\n=== Ethernet Status ===");
    
    int enabled = configGetInt(KEY_ETH_ENABLED, 0);
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

static void cmd_eth_on(int argc, char** argv) {
    (void)argc; (void)argv;
    configSetInt(KEY_ETH_ENABLED, 1);
    configUnifiedSave();
    logPrintf("Ethernet enabled. Reboot required.\n");
}

static void cmd_eth_off(int argc, char** argv) {
    (void)argc; (void)argv;
    configSetInt(KEY_ETH_ENABLED, 0);
    configUnifiedSave();
    logPrintf("Ethernet disabled. Reboot required.\n");
}

static void cmd_eth_dhcp(int argc, char** argv) {
    (void)argc; (void)argv;
    configSetInt(KEY_ETH_DHCP, 1);
    configUnifiedSave();
    logPrintf("DHCP mode enabled. Reboot required.\n");
}

static void cmd_eth_static(int argc, char** argv) {
    if (argc < 4) {
        logError("Usage: eth static <ip> <gateway> [mask]");
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
    logPrintf("Static IP configured:\n");
    logPrintf("  IP:      %s\n", argv[2]);
    logPrintf("  Gateway: %s\n", argv[3]);
    logPrintf("  Mask:    %s\n", argc >= 5 ? argv[4] : "255.255.255.0");
    logPrintf("Reboot required for changes to take effect.\n");
}

static void cmd_eth_dns(int argc, char** argv) {
    if (argc < 3) {
        logError("Usage: eth dns <dns_ip>");
        return;
    }
    configSetString(KEY_ETH_DNS, argv[2]);
    configUnifiedSave();
    logPrintf("DNS set to %s. Reboot required.\n", argv[2]);
}

void cmd_eth_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"status", cmd_eth_status, "Show Ethernet status"},
        {"on",     cmd_eth_on,     "Enable Ethernet"},
        {"off",    cmd_eth_off,    "Disable Ethernet"},
        {"dhcp",   cmd_eth_dhcp,   "Use DHCP"},
        {"static", cmd_eth_static, "Set static IP"},
        {"dns",    cmd_eth_dns,    "Set DNS server"}
    };
    
    cliDispatchSubcommand("", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

// Track Ethernet connect/disconnect for uptime
void ethTrackConnect() {
    eth_connect_time = millis();
    if (eth_connect_time > 0) eth_reconnect_count++;
}

void ethTrackError() {
    eth_error_count++;
}



void cmd_ping(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("Usage: ping <host> [count]");
        return;
    }

    const char* host = argv[1];
    int count = (argc >= 3) ? atoi(argv[2]) : 4;
    
    if (count <= 0) count = 4;
    if (count > 20) count = 20;

    logPrintf("Pinging %s (%d times)...\n", host, count);

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
        logPrintf("Statistics: Sent=%d, Received=%d, Lost=%d (%.0f%% loss)\n", 
                  count, successful, count - successful, (float)(count - successful) / count * 100);
        logPrintf("Round trip times: min=%.1fms, max=%.1fms, avg=%.1fms\n", 
                  min_time, max_time, total_time / successful);
    } else {
        logPrintf("Failed: %s is unreachable.\n", host);
    }
}

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi management", cmd_wifi_main);
    cliRegisterCommand("eth", "Ethernet management (KC868-A16)", cmd_eth_main);
    cliRegisterCommand("ping", "Ping a host", cmd_ping);
}

#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "config_unified.h"
#include "config_keys.h"
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

void cmd_wifi_ip(int argc, char** argv) {
    if (argc < 2) {
        // Show current IP configuration
        char ip_mode[16];
        char static_ip[16];
        char static_gw[16];
        char static_sn[16];
        char static_dns1[16];
        char static_dns2[16];

        configGetString(KEY_IP_MODE, ip_mode, sizeof(ip_mode), "dhcp");
        configGetString(KEY_STATIC_IP, static_ip, sizeof(static_ip), "");
        configGetString(KEY_STATIC_GATEWAY, static_gw, sizeof(static_gw), "");
        configGetString(KEY_STATIC_SUBNET, static_sn, sizeof(static_sn), "255.255.255.0");
        configGetString(KEY_STATIC_DNS1, static_dns1, sizeof(static_dns1), "8.8.8.8");
        configGetString(KEY_STATIC_DNS2, static_dns2, sizeof(static_dns2), "8.8.4.4");

        Serial.println("\n[WIFI] === IP Configuration ===");
        Serial.printf("  IP Mode: %s\n", ip_mode);

        if (strcmp(ip_mode, "static") == 0) {
            Serial.println("\nStatic IP Settings:");
            Serial.printf("  IP Address: %s\n", strlen(static_ip) > 0 ? static_ip : "(not set)");
            Serial.printf("  Gateway:    %s\n", strlen(static_gw) > 0 ? static_gw : "(not set)");
            Serial.printf("  Subnet:     %s\n", static_sn);
            Serial.printf("  DNS 1:      %s\n", static_dns1);
            Serial.printf("  DNS 2:      %s\n", static_dns2);
        }

        Serial.println("\nCurrent Connection:");
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("  Gateway:    %s\n", WiFi.gatewayIP().toString().c_str());
            Serial.printf("  Subnet:     %s\n", WiFi.subnetMask().toString().c_str());
            Serial.printf("  DNS:        %s\n", WiFi.dnsIP().toString().c_str());
        } else {
            Serial.println("  Not connected to WiFi");
        }

        Serial.println("\nUsage:");
        Serial.println("  wifi ip mode <dhcp|static>                        - Set IP mode");
        Serial.println("  wifi ip static <ip> <gateway> <subnet> [dns1] [dns2] - Configure static IP");
        Serial.println("  wifi ip reset                                     - Reset to DHCP");
        return;
    }

    // wifi ip mode <dhcp|static>
    if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            char ip_mode[16];
            configGetString(KEY_IP_MODE, ip_mode, sizeof(ip_mode), "dhcp");
            Serial.println("\n[WIFI] === IP Mode ===");
            Serial.printf("Current: %s\n", ip_mode);
            Serial.println("\nAvailable modes:");
            Serial.println("  dhcp   - Automatic IP (default)");
            Serial.println("  static - Fixed IP address");
            Serial.println("\nUsage: wifi ip mode <dhcp|static>");
            return;
        }

        // Validate mode
        if (strcmp(argv[2], "dhcp") != 0 && strcmp(argv[2], "static") != 0) {
            Serial.println("[WIFI] Error: Invalid mode");
            Serial.println("Valid modes: dhcp, static");
            return;
        }

        configSetString(KEY_IP_MODE, argv[2]);
        Serial.printf("[WIFI] IP mode set to: %s\n", argv[2]);

        if (strcmp(argv[2], "static") == 0) {
            Serial.println("[WIFI] Remember to configure static IP with: wifi ip static <ip> <gateway> <subnet>");
        }

        Serial.println("[WIFI] Changes take effect after reconnecting to WiFi");
        return;
    }

    // wifi ip static <ip> <gateway> <subnet> [dns1] [dns2]
    if (strcmp(argv[1], "static") == 0) {
        if (argc < 5) {
            Serial.println("[WIFI] Error: IP, gateway, and subnet required");
            Serial.println("Usage: wifi ip static <ip> <gateway> <subnet> [dns1] [dns2]");
            Serial.println("Example: wifi ip static 192.168.1.100 192.168.1.1 255.255.255.0");
            Serial.println("         wifi ip static 192.168.1.100 192.168.1.1 255.255.255.0 8.8.8.8 8.8.4.4");
            return;
        }

        // Validate IP addresses
        IPAddress test_ip, test_gw, test_sn;
        if (!test_ip.fromString(argv[2])) {
            Serial.printf("[WIFI] Error: Invalid IP address: %s\n", argv[2]);
            return;
        }
        if (!test_gw.fromString(argv[3])) {
            Serial.printf("[WIFI] Error: Invalid gateway address: %s\n", argv[3]);
            return;
        }
        if (!test_sn.fromString(argv[4])) {
            Serial.printf("[WIFI] Error: Invalid subnet mask: %s\n", argv[4]);
            return;
        }

        // Save static IP configuration
        configSetString(KEY_STATIC_IP, argv[2]);
        configSetString(KEY_STATIC_GATEWAY, argv[3]);
        configSetString(KEY_STATIC_SUBNET, argv[4]);

        // Optional DNS servers
        if (argc >= 6) {
            IPAddress test_dns1;
            if (test_dns1.fromString(argv[5])) {
                configSetString(KEY_STATIC_DNS1, argv[5]);
            } else {
                Serial.printf("[WIFI] Warning: Invalid DNS1 address: %s (using default 8.8.8.8)\n", argv[5]);
            }
        }
        if (argc >= 7) {
            IPAddress test_dns2;
            if (test_dns2.fromString(argv[6])) {
                configSetString(KEY_STATIC_DNS2, argv[6]);
            } else {
                Serial.printf("[WIFI] Warning: Invalid DNS2 address: %s (using default 8.8.4.4)\n", argv[6]);
            }
        }

        // Automatically set mode to static
        configSetString(KEY_IP_MODE, "static");

        Serial.println("[WIFI] Static IP configuration saved:");
        Serial.printf("  IP Address: %s\n", argv[2]);
        Serial.printf("  Gateway:    %s\n", argv[3]);
        Serial.printf("  Subnet:     %s\n", argv[4]);
        if (argc >= 6) {
            Serial.printf("  DNS 1:      %s\n", argv[5]);
        }
        if (argc >= 7) {
            Serial.printf("  DNS 2:      %s\n", argv[6]);
        }
        Serial.println("[WIFI] IP mode set to: static");
        Serial.println("[WIFI] Changes take effect after reconnecting to WiFi");
        return;
    }

    // wifi ip reset
    if (strcmp(argv[1], "reset") == 0) {
        configSetString(KEY_IP_MODE, "dhcp");
        configSetString(KEY_STATIC_IP, "");
        configSetString(KEY_STATIC_GATEWAY, "");
        configSetString(KEY_STATIC_SUBNET, "255.255.255.0");
        configSetString(KEY_STATIC_DNS1, "8.8.8.8");
        configSetString(KEY_STATIC_DNS2, "8.8.4.4");
        Serial.println("[WIFI] IP configuration reset to DHCP (automatic)");
        Serial.println("[WIFI] Changes take effect after reconnecting to WiFi");
        return;
    }

    Serial.printf("[WIFI] Error: Unknown parameter '%s'\n", argv[1]);
    Serial.println("Usage: wifi ip [mode|static|reset]");
}

void cmd_wifi_ap(int argc, char** argv) {
    if (argc < 2) {
        // Show current AP configuration
        char ap_ssid[32];
        char ap_password[64];
        char hostname[32];
        char ap_mode[16];
        configGetString(KEY_AP_SSID, ap_ssid, sizeof(ap_ssid), "BISSO_E350");
        configGetString(KEY_AP_PASSWORD, ap_password, sizeof(ap_password), "1234");
        configGetString(KEY_HOSTNAME, hostname, sizeof(hostname), "bisso-e350");
        configGetString(KEY_AP_MODE, ap_mode, sizeof(ap_mode), "always");

        Serial.println("\n[WIFI] === Access Point Configuration ===");
        Serial.printf("  AP Mode:     %s\n", ap_mode);
        Serial.printf("  AP SSID:     %s\n", ap_ssid);
        Serial.printf("  AP Password: ");
        for (size_t i = 0; i < strlen(ap_password); i++) Serial.print("*");
        Serial.printf(" (%d chars)\n", strlen(ap_password));
        Serial.printf("  Hostname:    %s\n", hostname);
        Serial.println("\nAP Modes:");
        Serial.println("  always - AP always on (even when WiFi connected)");
        Serial.println("  auto   - AP only when WiFi fails");
        Serial.println("  off    - AP disabled");
        Serial.println("\nCurrent Status:");
        if (WiFi.getMode() == WIFI_AP_STA) {
            Serial.println("  WiFi:  Connected");
            Serial.printf("  WiFi IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.println("  AP:    Running");
            Serial.printf("  AP IP:   %s\n", WiFi.softAPIP().toString().c_str());
        } else if (WiFi.getMode() == WIFI_AP) {
            Serial.println("  WiFi:  Disconnected");
            Serial.println("  AP:    Running");
            Serial.printf("  AP IP:   %s\n", WiFi.softAPIP().toString().c_str());
        } else if (WiFi.getMode() == WIFI_STA) {
            Serial.println("  WiFi:  Connected");
            Serial.printf("  WiFi IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.println("  AP:    Off");
        } else {
            Serial.println("  WiFi:  Disconnected");
            Serial.println("  AP:    Off");
        }
        Serial.println("\nUsage: wifi ap [mode|ssid|password|hostname|reset] <value>");
        return;
    }

    // wifi ap mode <always|auto|off>
    if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            char ap_mode[16];
            configGetString(KEY_AP_MODE, ap_mode, sizeof(ap_mode), "always");
            Serial.println("\n[WIFI] === AP Mode ===");
            Serial.printf("Current: %s\n", ap_mode);
            Serial.println("\nAvailable modes:");
            Serial.println("  always - AP always on (recommended for standalone use)");
            Serial.println("  auto   - AP only when WiFi connection fails");
            Serial.println("  off    - AP completely disabled");
            Serial.println("\nUsage: wifi ap mode <always|auto|off>");
            return;
        }

        // Validate mode
        if (strcmp(argv[2], "always") != 0 && strcmp(argv[2], "auto") != 0 && strcmp(argv[2], "off") != 0) {
            Serial.println("[WIFI] Error: Invalid mode");
            Serial.println("Valid modes: always, auto, off");
            return;
        }

        configSetString(KEY_AP_MODE, argv[2]);
        Serial.printf("[WIFI] AP mode set to: %s\n", argv[2]);

        if (strcmp(argv[2], "always") == 0) {
            Serial.println("[WIFI] AP will always run (even when WiFi connected)");
            Serial.println("[WIFI] Access web UI at http://192.168.4.1 from AP");
        } else if (strcmp(argv[2], "auto") == 0) {
            Serial.println("[WIFI] AP will only run when WiFi connection fails");
        } else if (strcmp(argv[2], "off") == 0) {
            Serial.println("[WIFI] AP disabled completely");
        }

        Serial.println("[WIFI] Changes take effect after reboot");
        return;
    }

    // wifi ap ssid <name>
    if (strcmp(argv[1], "ssid") == 0) {
        if (argc < 3) {
            Serial.println("[WIFI] Error: SSID required");
            Serial.println("Usage: wifi ap ssid <name>");
            return;
        }

        // Validate SSID length (1-32 chars)
        if (strlen(argv[2]) < 1 || strlen(argv[2]) > 32) {
            Serial.println("[WIFI] Error: SSID must be 1-32 characters");
            return;
        }

        configSetString(KEY_AP_SSID, argv[2]);
        Serial.printf("[WIFI] AP SSID set to: %s\n", argv[2]);
        Serial.println("[WIFI] Changes take effect on next WiFi failure or reboot");
        return;
    }

    // wifi ap password <password>
    if (strcmp(argv[1], "password") == 0) {
        if (argc < 3) {
            Serial.println("[WIFI] Error: Password required");
            Serial.println("Usage: wifi ap password <password>");
            return;
        }

        // Validate password length (WPA2 requires 8-63 chars, but allow shorter for open AP)
        if (strlen(argv[2]) < 4 || strlen(argv[2]) > 63) {
            Serial.println("[WIFI] Error: Password must be 4-63 characters");
            Serial.println("Note: WPA2 security requires 8+ characters (recommended)");
            return;
        }

        configSetString(KEY_AP_PASSWORD, argv[2]);
        Serial.printf("[WIFI] AP password updated (%d characters)\n", strlen(argv[2]));
        if (strlen(argv[2]) < 8) {
            Serial.println("[WIFI] Warning: Password shorter than 8 chars (less secure)");
        }
        Serial.println("[WIFI] Changes take effect on next WiFi failure or reboot");
        return;
    }

    // wifi ap hostname <name>
    if (strcmp(argv[1], "hostname") == 0) {
        if (argc < 3) {
            Serial.println("[WIFI] Error: Hostname required");
            Serial.println("Usage: wifi ap hostname <name>");
            return;
        }

        // Validate hostname (no spaces, reasonable length)
        if (strlen(argv[2]) < 1 || strlen(argv[2]) > 32) {
            Serial.println("[WIFI] Error: Hostname must be 1-32 characters");
            return;
        }

        // Check for invalid characters
        for (size_t i = 0; i < strlen(argv[2]); i++) {
            char c = argv[2][i];
            if (!isalnum(c) && c != '-' && c != '_') {
                Serial.println("[WIFI] Error: Hostname can only contain letters, numbers, - and _");
                return;
            }
        }

        configSetString(KEY_HOSTNAME, argv[2]);
        Serial.printf("[WIFI] Hostname set to: %s\n", argv[2]);
        Serial.println("[WIFI] Changes take effect after reboot");
        return;
    }

    // wifi ap reset
    if (strcmp(argv[1], "reset") == 0) {
        configSetString(KEY_AP_MODE, "always");
        configSetString(KEY_AP_SSID, "BISSO_E350");
        configSetString(KEY_AP_PASSWORD, "1234");
        configSetString(KEY_HOSTNAME, "bisso-e350");
        Serial.println("[WIFI] AP configuration reset to factory defaults:");
        Serial.println("  AP Mode:     always");
        Serial.println("  AP SSID:     BISSO_E350");
        Serial.println("  AP Password: 1234");
        Serial.println("  Hostname:    bisso-e350");
        Serial.println("[WIFI] Changes take effect after reboot");
        return;
    }

    Serial.printf("[WIFI] Error: Unknown parameter '%s'\n", argv[1]);
    Serial.println("Usage: wifi ap [mode|ssid|password|hostname|reset] <value>");
}

void cmd_wifi_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[WIFI] === Network Management ===");
        Serial.println("Usage: wifi [scan | connect | status | ip | ap]");
        Serial.println("");
        Serial.println("Commands:");
        Serial.println("  wifi scan                           - Scan for available networks");
        Serial.println("  wifi connect <ssid> <password>      - Connect to WiFi network");
        Serial.println("  wifi status                         - Show connection status");
        Serial.println("  wifi ip                             - Show/configure IP settings");
        Serial.println("  wifi ip mode <dhcp|static>          - Set IP mode (DHCP or static)");
        Serial.println("  wifi ip static <ip> <gw> <sn> [...] - Configure static IP");
        Serial.println("  wifi ip reset                       - Reset to DHCP");
        Serial.println("  wifi ap                             - Show/configure Access Point");
        Serial.println("  wifi ap mode <always|auto|off>      - Set AP mode");
        Serial.println("  wifi ap ssid <name>                 - Set AP SSID");
        Serial.println("  wifi ap password <password>         - Set AP password");
        Serial.println("  wifi ap hostname <name>             - Set device hostname");
        Serial.println("  wifi ap reset                       - Reset AP to factory defaults");
        return;
    }
    if (strcmp(argv[1], "scan") == 0) cmd_wifi_scan(argc, argv);
    else if (strcmp(argv[1], "connect") == 0) cmd_wifi_connect(argc, argv);
    else if (strcmp(argv[1], "status") == 0) cmd_wifi_status(argc, argv);
    else if (strcmp(argv[1], "ip") == 0) cmd_wifi_ip(argc, argv);
    else if (strcmp(argv[1], "ap") == 0) cmd_wifi_ap(argc, argv);
    else Serial.printf("[WIFI] Error: Unknown parameter '%s'.\n", argv[1]);
}

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi management", cmd_wifi_main);
}
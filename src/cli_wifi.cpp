#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h" // <-- NEW: Required for watchdogFeed
#include <WiFi.h>
#include <Arduino.h>

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static const char* wifiGetAuthModeString(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        default: return "UNKNOWN";
    }
}

static const char* wifiGetStatusString(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

void cmd_wifi_scan(int argc, char** argv) {
    Serial.println("[WIFI] Switching to Station mode for scanning...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("[WIFI] Starting network scan (this may take a few seconds)...");
    
    // Synchronous scan
    int n = WiFi.scanNetworks();
    
    if (n == 0) {
        Serial.println("[WIFI] No networks found.");
    } else {
        Serial.printf("[WIFI] Found %d networks:\n", n);
        Serial.println("\n  Idx | SSID                             | RSSI  | Ch | Auth         | BSSID");
        Serial.println("  ----+----------------------------------+-------+----+--------------+-------------------");
        
        for (int i = 0; i < n; ++i) {
            Serial.printf("  %3d | %-32.32s | %-5d | %-2d | %-12s | %s\n", 
                i + 1,
                WiFi.SSID(i).c_str(),
                WiFi.RSSI(i),
                WiFi.channel(i),
                wifiGetAuthModeString(WiFi.encryptionType(i)),
                WiFi.BSSIDstr(i).c_str()
            );
            delay(10); // Slight delay to keep serial buffer happy
        }
        Serial.println();
    }
    
    // Clean up RAM used by scan
    WiFi.scanDelete();
}

void cmd_wifi_connect(int argc, char** argv) {
    if (argc < 4) {
        Serial.println("[WIFI] Usage: wifi connect <ssid> <password>");
        return;
    }

    const char* ssid = argv[2];
    const char* password = argv[3];

    Serial.printf("[WIFI] Connecting to '%s'...\n", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Wait for connection with visual feedback
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 second timeout
        delay(500);
        
        // FIX: Feed watchdog to prevent reset during blocking delay
        watchdogFeed("CLI"); 
        
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] ✅ Connected successfully!");
        Serial.print("[WIFI] IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("[WIFI] Subnet Mask: ");
        Serial.println(WiFi.subnetMask());
        Serial.print("[WIFI] Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("[WIFI] RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.printf("[WIFI] ❌ Connection failed. Reason: %s\n", wifiGetStatusString(WiFi.status()));
    }
}

void cmd_wifi_status(int argc, char** argv) {
    Serial.println("\n[WIFI] === Network Interface Status ===");
    
    wifi_mode_t mode = WiFi.getMode();
    const char* modeStr = (mode == WIFI_OFF ? "OFF" : 
                          (mode == WIFI_STA ? "STATION" : 
                          (mode == WIFI_AP ? "ACCESS POINT" : "AP+STA")));
                          
    Serial.printf("  Mode:        %d (%s)\n", mode, modeStr);
    Serial.printf("  Status:      %s\n", wifiGetStatusString(WiFi.status()));
    Serial.printf("  MAC Address: %s\n", WiFi.macAddress().c_str());

    // --- Station Details (Client Mode) ---
    if (mode == WIFI_STA || mode == WIFI_AP_STA) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n  [Connection Details]");
            Serial.printf("  SSID:        %s\n", WiFi.SSID().c_str());
            Serial.printf("  BSSID:       %s\n", WiFi.BSSIDstr().c_str());
            Serial.printf("  Channel:     %d\n", WiFi.channel());
            Serial.printf("  RSSI:        %d dBm\n", WiFi.RSSI());
            
            Serial.println("\n  [IP Configuration]");
            Serial.printf("  Local IP:    %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("  Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
            Serial.printf("  Gateway IP:  %s\n", WiFi.gatewayIP().toString().c_str());
            Serial.printf("  DNS 1:       %s\n", WiFi.dnsIP(0).toString().c_str());
            Serial.printf("  DNS 2:       %s\n", WiFi.dnsIP(1).toString().c_str());
        } else {
            Serial.println("\n  [Connection Details]");
            Serial.println("  (Not connected to an AP)");
        }
    }
    
    // --- Access Point Details (Host Mode) ---
    if (mode == WIFI_AP || mode == WIFI_AP_STA) {
         Serial.println("\n  [Access Point Details]");
         Serial.printf("  AP SSID:     %s\n", WiFi.softAPSSID().c_str());
         Serial.printf("  AP IP:       %s\n", WiFi.softAPIP().toString().c_str());
         Serial.printf("  Clients:     %d\n", WiFi.softAPgetStationNum());
    }
    Serial.println();
}

// ============================================================================
// MAIN DISPATCHER
// ============================================================================

void cmd_wifi_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[WIFI] === WiFi Network Management ===");
        Serial.println("[WIFI] Usage: wifi [command] <params>");
        Serial.println("[WIFI] Commands:");
        Serial.println("  scan      - Scan for available WiFi networks and display details.");
        Serial.println("  connect   - Connect to a network (wifi connect <ssid> <pass>).");
        Serial.println("  status    - Show detailed connection info (IP, RSSI, MAC, DNS, Gateway).");
        return;
    }

    if (strcmp(argv[1], "scan") == 0) {
        cmd_wifi_scan(argc, argv);
    } else if (strcmp(argv[1], "connect") == 0) {
        cmd_wifi_connect(argc, argv);
    } else if (strcmp(argv[1], "status") == 0) {
        cmd_wifi_status(argc, argv);
    } else {
        Serial.printf("[WIFI] Error: Unknown parameter '%s'. Use 'wifi' for help.\n", argv[1]);
    }
}

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterWifiCommands() {
    cliRegisterCommand("wifi", "WiFi scanning and connection manager.", cmd_wifi_main);
}
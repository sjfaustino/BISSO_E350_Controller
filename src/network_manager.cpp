#include "network_manager.h"
#include "serial_logger.h"
#include "cli.h"
#include "web_server.h"
#include "config_unified.h"
#include "config_keys.h"
#include <ESPAsyncWiFiManager.h> // Includes AsyncWiFiManager class
#include <ArduinoOTA.h>
#include <ESPmDNS.h>  // For hostname.local support

NetworkManager networkManager;

NetworkManager::NetworkManager() : telnetServer(nullptr), clientConnected(false), dnsServer(nullptr), captivePortalActive(false) {}

void NetworkManager::init() {
    Serial.println("[NET] Initializing Network Stack...");

    // Read AP configuration from NVS
    char ap_ssid[32];
    char ap_password[64];
    char ap_mode[16];
    configGetString(KEY_AP_SSID, ap_ssid, sizeof(ap_ssid), "BISSO_E350");
    configGetString(KEY_AP_PASSWORD, ap_password, sizeof(ap_password), "1234");
    configGetString(KEY_AP_MODE, ap_mode, sizeof(ap_mode), "always");  // Default: always-on

    Serial.printf("[NET] AP Mode: %s\n", ap_mode);
    Serial.printf("[NET] AP SSID: %s\n", ap_ssid);

    // 1. Configure IP mode (DHCP or Static)
    char ip_mode[16];
    configGetString(KEY_IP_MODE, ip_mode, sizeof(ip_mode), "dhcp");

    WiFi.mode(WIFI_STA);

    if (strcmp(ip_mode, "static") == 0) {
        // Configure static IP
        char static_ip_str[16];
        char static_gw_str[16];
        char static_sn_str[16];
        char static_dns1_str[16];
        char static_dns2_str[16];

        configGetString(KEY_STATIC_IP, static_ip_str, sizeof(static_ip_str), "");
        configGetString(KEY_STATIC_GATEWAY, static_gw_str, sizeof(static_gw_str), "");
        configGetString(KEY_STATIC_SUBNET, static_sn_str, sizeof(static_sn_str), "255.255.255.0");
        configGetString(KEY_STATIC_DNS1, static_dns1_str, sizeof(static_dns1_str), "8.8.8.8");
        configGetString(KEY_STATIC_DNS2, static_dns2_str, sizeof(static_dns2_str), "8.8.4.4");

        // Only configure static IP if IP address is set
        if (strlen(static_ip_str) > 0) {
            IPAddress static_ip, static_gw, static_sn, static_dns1, static_dns2;

            if (static_ip.fromString(static_ip_str) &&
                static_gw.fromString(static_gw_str) &&
                static_sn.fromString(static_sn_str)) {

                static_dns1.fromString(static_dns1_str);
                static_dns2.fromString(static_dns2_str);

                if (WiFi.config(static_ip, static_gw, static_sn, static_dns1, static_dns2)) {
                    Serial.println("[NET] Static IP configuration applied");
                    Serial.printf("[NET] IP: %s\n", static_ip_str);
                    Serial.printf("[NET] Gateway: %s\n", static_gw_str);
                    Serial.printf("[NET] Subnet: %s\n", static_sn_str);
                } else {
                    Serial.println("[NET] [WARN] Failed to apply static IP config, using DHCP");
                }
            } else {
                Serial.println("[NET] [WARN] Invalid static IP configuration, using DHCP");
            }
        } else {
            Serial.println("[NET] [WARN] Static IP not set, using DHCP");
        }
    } else {
        Serial.println("[NET] IP Mode: DHCP (automatic)");
    }

    // 2. Try to connect to WiFi using saved credentials
    WiFi.begin();  // Use saved credentials from WiFiManager

    Serial.print("[NET] Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    bool wifi_connected = (WiFi.status() == WL_CONNECTED);

    if (wifi_connected) {
        Serial.print("[NET] [OK] WiFi Connected. IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("[NET] [OK] Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("[NET] [OK] DNS: ");
        Serial.println(WiFi.dnsIP());
    } else {
        Serial.println("[NET] [WARN] WiFi connection failed");
    }

    // 3. Start Access Point based on mode
    bool start_ap = false;

    if (strcmp(ap_mode, "always") == 0) {
        // Always-on AP mode
        start_ap = true;
        Serial.println("[NET] AP Mode: ALWAYS ON");
    } else if (strcmp(ap_mode, "auto") == 0) {
        // Auto mode - AP only when WiFi fails
        start_ap = !wifi_connected;
        if (start_ap) {
            Serial.println("[NET] AP Mode: AUTO (WiFi failed, starting AP)");
        } else {
            Serial.println("[NET] AP Mode: AUTO (WiFi connected, AP disabled)");
        }
    } else if (strcmp(ap_mode, "off") == 0) {
        // AP disabled
        start_ap = false;
        Serial.println("[NET] AP Mode: OFF (AP disabled)");
    } else {
        // Unknown mode, default to always
        start_ap = true;
        Serial.println("[NET] AP Mode: UNKNOWN, defaulting to ALWAYS ON");
    }

    // 4. Configure WiFi mode and start AP if needed
    if (start_ap) {
        if (wifi_connected) {
            // Both WiFi and AP (AP+STA mode)
            WiFi.mode(WIFI_AP_STA);
            Serial.println("[NET] WiFi Mode: AP+STA (Access Point + Station)");
        } else {
            // AP only
            WiFi.mode(WIFI_AP);
            Serial.println("[NET] WiFi Mode: AP (Access Point only)");
        }

        // Start the Access Point
        bool ap_started = WiFi.softAP(ap_ssid, ap_password);
        if (ap_started) {
            Serial.printf("[NET] [OK] AP Started: %s\n", ap_ssid);
            Serial.print("[NET] [OK] AP IP: ");
            Serial.println(WiFi.softAPIP());
            Serial.println("[NET] [OK] Connect to AP and access http://192.168.4.1");

            // Start Captive Portal DNS
            startCaptivePortal();
        } else {
            Serial.println("[NET] [FAIL] Failed to start AP");
        }
    } else if (wifi_connected) {
        // WiFi only mode
        WiFi.mode(WIFI_STA);
        Serial.println("[NET] WiFi Mode: STA (Station only)");
    } else {
        // No WiFi, No AP - offline mode
        Serial.println("[NET] [WARN] Running in OFFLINE mode (no network)");
    }

    // 5. Get hostname for mDNS and OTA
    char hostname[32];
    configGetString(KEY_HOSTNAME, hostname, sizeof(hostname), "bisso-e350");

    // 6. Start mDNS service for hostname.local access
    if (MDNS.begin(hostname)) {
        Serial.printf("[NET] [OK] mDNS started: %s.local\n", hostname);
        MDNS.addService("http", "tcp", 80);

        if (wifi_connected) {
            Serial.printf("[NET] [OK] Access web UI at: http://%s.local\n", hostname);
        }
    } else {
        Serial.println("[NET] [WARN] mDNS failed to start");
    }

    // 7. OTA Setup
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword("admin123"); // Secure OTA

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start updating " + type);
        // Safety: Stop Motion immediately
        // motionEmergencyStop(); 
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();

    // 8. Telnet Server
    telnetServer = new WiFiServer(TELNET_PORT);
    telnetServer->begin();
    telnetServer->setNoDelay(true);
    Serial.println("[NET] Telnet Server Started on Port 23");
}

void NetworkManager::update() {
    // 1. Handle OTA
    ArduinoOTA.handle();

    // 2. Handle mDNS
    MDNS.update();

    // 3. Handle DNS Server (Captive Portal)
    if (dnsServer) {
        dnsServer->processNextRequest();
    }

    // 4. Handle Telnet
    if (telnetServer->hasClient()) {
        if (!telnetClient || !telnetClient.connected()) {
            if (telnetClient) telnetClient.stop();
            telnetClient = telnetServer->available();
            clientConnected = true;
            telnetClient.flush();
            telnetClient.println("==================================");
            telnetClient.println("   BISSO E350 REMOTE TERMINAL     ");
            telnetClient.println("==================================");
            Serial.println("[NET] Telnet Client Connected");
        } else {
            // Reject multiple clients (Simple 1-client implementation)
            WiFiClient reject = telnetServer->available();
            reject.stop();
        }
    }

    // Process Telnet Input -> CLI
    if (telnetClient && telnetClient.connected() && telnetClient.available()) {
        String cmd = telnetClient.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
            Serial.printf("[NET] Remote Command: %s\n", cmd.c_str());
            // Inject into CLI processor
            cliProcessCommand(cmd.c_str());
            telnetClient.print("> "); // Prompt
        }
    }
}

void NetworkManager::telnetPrint(const char* str) {
    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(str);
    }
}

void NetworkManager::telnetPrintln(const char* str) {
    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(str);
    }
}

// ======================================
// CAPTIVE PORTAL METHODS
// ======================================

void NetworkManager::startCaptivePortal() {
    if (!dnsServer) {
        dnsServer = new DNSServer();

        // Redirect all DNS requests to the AP IP (192.168.4.1)
        // This makes all hostnames resolve to the ESP32
        if (dnsServer->start(DNS_PORT, "*", WiFi.softAPIP())) {
            captivePortalActive = true;
            Serial.println("[NET] [OK] Captive Portal DNS started");
            Serial.println("[NET] [OK] All DNS requests redirect to web UI");
        } else {
            Serial.println("[NET] [WARN] Captive Portal DNS failed to start");
            delete dnsServer;
            dnsServer = nullptr;
        }
    }
}

void NetworkManager::stopCaptivePortal() {
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
        captivePortalActive = false;
        Serial.println("[NET] Captive Portal stopped");
    }
}

bool NetworkManager::isCaptivePortalRequest(const String& host) {
    // Check if request is likely from captive portal detection
    // Captive portals try to access known URLs to detect internet connectivity

    // If captive portal is not active, return false
    if (!captivePortalActive) return false;

    // Check for common captive portal detection hostnames
    if (host.indexOf("captive.apple.com") >= 0) return true;
    if (host.indexOf("connectivitycheck.gstatic.com") >= 0) return true;
    if (host.indexOf("www.msftconnecttest.com") >= 0) return true;
    if (host.indexOf("detectportal.firefox.com") >= 0) return true;
    if (host.indexOf("nmcheck.gnome.org") >= 0) return true;

    // If host is not our IP address and not our mDNS hostname, it's probably captive portal
    char hostname[32];
    configGetString(KEY_HOSTNAME, hostname, sizeof(hostname), "bisso-e350");

    String local_hostname = String(hostname) + ".local";
    String ap_ip = WiFi.softAPIP().toString();

    // If host matches our hostname or IP, it's a direct request
    if (host == local_hostname || host == ap_ip || host == "192.168.4.1") {
        return false;
    }

    // Everything else when connected to AP is likely captive portal
    return true;
}
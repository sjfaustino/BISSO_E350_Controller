#include "network_manager.h"
#include "serial_logger.h"
#include "cli.h"
#include "web_server.h"
#include "config_unified.h"
#include "config_keys.h"
#include <ESPAsyncWiFiManager.h> // Includes AsyncWiFiManager class
#include <ArduinoOTA.h>

NetworkManager networkManager;

NetworkManager::NetworkManager() : telnetServer(nullptr), clientConnected(false) {}

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

    // 1. Try to connect to WiFi using saved credentials
    WiFi.mode(WIFI_STA);
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
    } else {
        Serial.println("[NET] [WARN] WiFi connection failed");
    }

    // 2. Start Access Point based on mode
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

    // 3. Configure WiFi mode and start AP if needed
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

    // 2. OTA Setup
    char hostname[32];
    configGetString(KEY_HOSTNAME, hostname, sizeof(hostname), "bisso-e350");
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

    // 3. Telnet Server
    telnetServer = new WiFiServer(TELNET_PORT);
    telnetServer->begin();
    telnetServer->setNoDelay(true);
    Serial.println("[NET] Telnet Server Started on Port 23");
}

void NetworkManager::update() {
    // 1. Handle OTA
    ArduinoOTA.handle();

    // 2. Handle Telnet
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
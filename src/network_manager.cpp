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

    // 1. WiFi Provisioning (Async Manager)
    AsyncWebServer* provServer = new AsyncWebServer(80);
    DNSServer* dns = new DNSServer();
    
    // FIX: Correct class name is AsyncWiFiManager (not ESPAsyncWiFiManager)
    AsyncWiFiManager wm(provServer, dns);
    
    // Config: Set timeout to 3 minutes (180s)
    // Note: setClass() is not supported in the Async version, so it was removed.
    wm.setConfigPortalTimeout(180);

    // SSID and Password for the Configuration Access Point
    // Read from NVS with defaults: BISSO_E350 / 1234
    char ap_ssid[32];
    char ap_password[64];
    configGetString(KEY_AP_SSID, ap_ssid, sizeof(ap_ssid), "BISSO_E350");
    configGetString(KEY_AP_PASSWORD, ap_password, sizeof(ap_password), "1234");

    Serial.printf("[NET] AP SSID: %s\n", ap_ssid);
    bool res = wm.autoConnect(ap_ssid, ap_password); 

    if(!res) {
        Serial.println("[NET] [FAIL] WiFi connection failed. Starting Offline Mode.");
        // We continue boot even without WiFi to allow local Motion control
    } else {
        Serial.print("[NET] [OK] WiFi Connected. IP: ");
        Serial.println(WiFi.localIP());
    }
    
    // Clean up provisioning resources
    delete provServer;
    delete dns;

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
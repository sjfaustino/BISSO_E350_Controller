#include "network_manager.h"
#include "serial_logger.h"
#include "cli.h"
#include "web_server.h"
#include <ESPAsyncWiFiManager.h> // Includes AsyncWiFiManager class
#include <ArduinoOTA.h>

NetworkManager networkManager;

NetworkManager::NetworkManager() : telnetServer(nullptr), clientConnected(false) {}

NetworkManager::~NetworkManager() {
    // Clean up allocated resources
    if (telnetServer) {
        telnetServer->stop();
        delete telnetServer;
        telnetServer = nullptr;
    }
    if (telnetClient) {
        telnetClient.stop();
    }
}

void NetworkManager::init() {
    Serial.println("[NET] Initializing Network Stack...");

    // 1. WiFi Initialization (Non-blocking to allow boot to continue)
    // Try to connect to saved network without blocking
    WiFi.mode(WIFI_STA);
    WiFi.begin(); // Uses credentials from previous autoConnect()

    // Don't wait for connection - boot continues
    // WiFi will connect in background during networkManager.update() calls
    Serial.println("[NET] [OK] WiFi initialization queued (non-blocking)");

    // NOTE: AsyncWiFiManager with autoConnect() was BLOCKING boot sequence
    // Removed to allow taskManagerStart() to execute immediately

    // 2. OTA Setup
    ArduinoOTA.setHostname("bisso-e350");
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
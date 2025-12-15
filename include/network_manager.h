/**
 * @file network_manager.h
 * @brief Handles WiFi Provisioning, OTA, and Telnet
 * @project Gemini v1.2.0
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>

#define TELNET_PORT 23
#define MAX_TELNET_CLIENTS 1

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();  // Destructor to clean up allocated resources
    void init();
    void update();

    // Send text to connected Telnet client (for log mirroring)
    void telnetPrint(const char* str);
    void telnetPrintln(const char* str);

private:
    WiFiServer* telnetServer;
    WiFiClient telnetClient;
    bool clientConnected;

    void handleOTA();
    void handleTelnet();
};

extern NetworkManager networkManager;

#endif
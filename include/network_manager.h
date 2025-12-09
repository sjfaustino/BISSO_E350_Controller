/**
 * @file network_manager.h
 * @brief Handles WiFi, mDNS, Captive Portal, OTA, and Telnet
 * @project BISSO E350 v1.0.0
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>

#define TELNET_PORT 23
#define MAX_TELNET_CLIENTS 1
#define DNS_PORT 53

class NetworkManager {
public:
    NetworkManager();
    void init();
    void update();

    // Send text to connected Telnet client (for log mirroring)
    void telnetPrint(const char* str);
    void telnetPrintln(const char* str);

    // Check if captive portal redirect is needed
    bool isCaptivePortalRequest(const String& host);

private:
    WiFiServer* telnetServer;
    WiFiClient telnetClient;
    bool clientConnected;

    // Captive Portal DNS Server
    DNSServer* dnsServer;
    bool captivePortalActive;

    void handleOTA();
    void handleTelnet();
    void startCaptivePortal();
    void stopCaptivePortal();
};

extern NetworkManager networkManager;

#endif
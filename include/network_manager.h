/**
 * @file network_manager.h
 * @brief Handles WiFi Provisioning, Ethernet, OTA, and Telnet
 * @project PosiPro
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>

#define TELNET_PORT 23
#define MAX_TELNET_CLIENTS 1
#define NET_DNS_PORT 53

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();  // Destructor to clean up allocated resources
    void init();
    void update();

    // Send text to connected Telnet client (for log mirroring)
    void telnetPrint(const char* str);
    void telnetPrintln(const char* str);

    // Ethernet status (KC868-A16 LAN8720)
    bool isEthernetConnected() const { return ethernetConnected; }
    String getEthernetIP() const;
    uint8_t getEthernetLinkSpeed() const { return ethernetLinkSpeed; }
    
    // Public for event handler access
    bool ethernetConnected;
    uint8_t ethernetLinkSpeed;  // 10 or 100 Mbps

private:
    WiFiServer* telnetServer;
    WiFiClient telnetClient;
    DNSServer* dnsServer;
    bool clientConnected;

    void handleOTA();
    void handleTelnet();
    void initEthernet();
};

extern NetworkManager networkManager;

#endif

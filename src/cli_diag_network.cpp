/**
 * @file cli_diag_network.cpp
 * @brief Network-related diagnostic CLI commands
 */

#include "cli.h"
#include <WiFi.h>
#include <ETH.h>
#include "network_manager.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include <Arduino.h>

void cmd_net_diag(int argc, char** argv) {
    (void)argc; (void)argv;
    watchdogFeed("CLI");
    
    serialLoggerLock();
    logPrintln("\n=== Network Diagnostics ===");
    
    // WiFi Status
    logPrintf("WiFi Status:      %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    if (WiFi.status() == WL_CONNECTED) {
        logPrintf("SSID:             %s\n", WiFi.SSID().c_str());
        logPrintf("IP Address:       %s\n", WiFi.localIP().toString().c_str());
        logPrintf("RSSI:             %d dBm\n", WiFi.RSSI());
    }
    
    // Ethernet Status (if enabled)
    #if defined(ETH_PHY_TYPE)
    logPrintf("Ethernet Status:  %s\n", ETH.linkUp() ? "CONNECTED" : "DISCONNECTED");
    if (ETH.linkUp()) {
        logPrintf("ETH IP Address:   %s\n", ETH.localIP().toString().c_str());
    }
    #endif

    logPrintf("Hostname:         %s.local\n", WiFi.getHostname());
    serialLoggerUnlock();
}

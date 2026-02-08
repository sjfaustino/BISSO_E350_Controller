/**
 * @file oled_dashboard.cpp
 * @brief Local SSD1306 OLED Dashboard Implementation
 */

#include "oled_dashboard.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include "board_variant.h"
#include "system_telemetry.h"
#include "network_manager.h"
#include "motion.h"
#include "serial_logger.h"
#include "system_utils.h" // PHASE 8.1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static bool oled_ready = false;

bool oledDashboardInit() {
#if !BOARD_HAS_OLED_SSD1306
    return false;
#endif

    logModuleInit("OLED");
    
    // Wire instance should already be initialized by board_inputs or main.cpp
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        logModuleInitFail("OLED", "Allocation failed");
        return false;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("BISSO E350 PRO");
    display.println("v3.1 Hardware");
    display.display();
    
    oled_ready = true;
    logModuleInitOK("OLED");
    return true;
}

void oledDashboardUpdate() {
    if (!oled_ready) return;

    system_telemetry_t t = telemetryGetSnapshot();
    
    display.clearDisplay();
    
    // Header
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("STATUS: ");
    display.println(telemetryGetHealthStatusString(t.health_status));
    
    // Network Info
    display.setCursor(0, 10);
    if (networkManager.isEthernetConnected()) {
        display.print("ETH: ");
        display.println(networkManager.getEthernetIP());
    } else if (t.wifi_connected) {
        display.print("WIFI: ");
        display.print(WiFi.localIP());
        display.print(" ");
        display.print(t.wifi_signal_strength);
        display.println("%");
    } else {
        display.println("NET: Disconnected");
    }

    // Coordinates (DRO)
    display.setTextSize(2);
    display.setCursor(0, 22);
    display.print("X: "); display.println(t.axis_x_mm, 2);
    display.print("Y: "); display.println(t.axis_y_mm, 2);
    display.print("Z: "); display.println(t.axis_z_mm, 2);

    // Footer
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("CPU:"); display.print(t.cpu_usage_percent); display.print("% ");
    display.print("H:"); display.print(t.free_heap_bytes / 1024); display.print("KB");

    display.display();
}

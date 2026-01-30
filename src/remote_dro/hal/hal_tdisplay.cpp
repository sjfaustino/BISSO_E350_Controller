#include "hal_tdisplay.h"
#include <WiFi.h>
#include <esp_wifi.h>

#include "../logos.h"

// PIN definitions handled by PIO build flags for TFT_eSPI
// But we need to handle LEDs and Buttons here
#ifndef TDISPLAY_BUTTON_1
#define TDISPLAY_BUTTON_1 0  // Right button
#endif
#ifndef TDISPLAY_BUTTON_2
#define TDISPLAY_BUTTON_2 35 // Left button
#endif

HAL_TDisplay::HAL_TDisplay() : tft(TFT_eSPI()) {}

void HAL_TDisplay::init() {
    pinMode(TDISPLAY_BUTTON_1, INPUT_PULLUP);
    pinMode(TDISPLAY_BUTTON_2, INPUT_PULLUP);

    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(TFT_BLACK);

    // PM Setup (Generic ESP32)
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80, // Slower can flicker TFT
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);

    // Note: ESP32 (Original) temp sensor is different or not available on some modules
    // but handled via driver/temp_sensor.h on newer ones.
}

void HAL_TDisplay::update() {
    // T-Display often has no built-in status LED like the SuperMini
}

void HAL_TDisplay::setScreenOn(bool on) {
    // Control backlight if possible
#ifdef TFT_BL
    digitalWrite(TFT_BL, on ? HIGH : LOW);
#endif
    if (!on) tft.fillScreen(TFT_BLACK);
}

void HAL_TDisplay::showSplash(const char* version, float temp) {
    tft.fillScreen(TFT_BLACK);
    
    // Custom PosiPro Logo (135x92) centered on 240x135
    int x = (240 - 135) / 2;
    int y = (135 - 92) / 2;
    
    tft.drawXBitmap(x, y, logo_posipro_tdisplay_bmp, 135, 92, TFT_WHITE);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x + 40, y + 92 + 5); 
    tft.printf("%s", version);
    
    delay(2000);
}

void HAL_TDisplay::drawSearching(uint8_t channel, float temp, bool fullSweep) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("OFFLINE");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.println("Scanning ESP-NOW Channels...");
    
    tft.setCursor(10, 60);
    tft.printf("Channel: %d", channel);
    
    // Draw a better progress bar for the big screen
    int barWidth = (millis() / 5) % 220;
    tft.drawRect(10, 80, 220, 10, TFT_BLUE);
    tft.fillRect(10, 80, barWidth, 10, TFT_CYAN);
}

void HAL_TDisplay::drawActiveDRO(const TelemetryPacket& data, uint8_t channel) {
    int w = tft.width();
    
    // Track previous state to avoid redundant drawing
    static uint32_t lastDrawStatus = 99;
    static float lx=0, ly=0, lz=0;
    static uint8_t lastChannel = 0;

    int labelX = 20; // Margin from safety zone
    int valueX = 75;

    // 1. Static Elements - Redraw ONLY on major change (Status/Channel/New Entry)
    if (data.status != lastDrawStatus || channel != lastChannel) {
        uint16_t statusColor = TFT_DARKGREY;
        const char* statusText = "READY";
        switch(data.status) {
            case 0: statusColor = TFT_BLUE; statusText = "READY"; break;
            case 1: statusColor = TFT_GREEN; statusText = "MOVING"; break;
            case 2: statusColor = TFT_ORANGE; statusText = "ALARM"; break;
            case 3: statusColor = TFT_RED; statusText = "E-STOP"; break;
        }

        if (lastDrawStatus == 99) tft.fillScreen(TFT_BLACK);

        tft.fillRect(0, 0, w, 24, statusColor);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(statusText, w/2, 12);
        
        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(String("CH" + String(channel)).c_str(), w - 5, 5);
        
        // Draw the static labels ONCE
        tft.setTextSize(3);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);   tft.drawString("X:", labelX, 45);
        tft.setTextColor(TFT_MAGENTA, TFT_BLACK);tft.drawString("Y:", labelX, 80);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("Z:", labelX, 115);

        lastDrawStatus = data.status;
        lastChannel = channel;
    }

    // 2. Dynamic Numbers - Only redraw the numeric value area (Right Justified)
    tft.setTextSize(3);
    tft.setTextDatum(MR_DATUM); // Middle Right
    
    int rightX = w - 10; // Right margin
    int padLength = 160; // Enough to clear labels area if needed
    tft.setTextPadding(padLength); 

    if (data.x != lx) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawFloat(data.x, 2, rightX, 45);
        lx = data.x;
    }
    
    if (data.y != ly) {
        tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
        tft.drawFloat(data.y, 2, rightX, 80);
        ly = data.y;
    }
    
    if (data.z != lz) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawFloat(data.z, 2, rightX, 115);
        lz = data.z;
    }
}

void HAL_TDisplay::drawGiantDRO(char axis, float value, bool positive) {
    int w = tft.width();
    int h = tft.height();

    static char lastAxis = ' ';
    static float lastVal = 999999;

    // Redraw background/static parts only when axis changes
    if (axis != lastAxis) {
        tft.fillScreen(TFT_BLACK);
        
        uint16_t color = TFT_WHITE;
        if (axis == 'X') color = TFT_CYAN;
        else if (axis == 'Y') color = TFT_MAGENTA;
        else if (axis == 'Z') color = TFT_YELLOW;

        tft.setTextColor(color, TFT_BLACK);
        tft.setTextSize(4);
        tft.setTextDatum(TC_DATUM); // Top Center
        tft.drawString(String(axis).c_str(), w/2, 5); // Centered at top
        
        lastAxis = axis;
        lastVal = 999999; // Force value redraw
    }

    // Dynamic numeric value - Large and Right Justified
    if (value != lastVal) {
        uint16_t color = (axis == 'X') ? TFT_CYAN : (axis == 'Y' ? TFT_MAGENTA : TFT_YELLOW);
        tft.setTextColor(color, TFT_BLACK);
        
        // --- Number (Absolute) ---
        tft.setTextSize(6);
        tft.setTextDatum(MR_DATUM); // Middle Right
        int rightX = w - 10;
        tft.setTextPadding(w - 20);
        tft.drawFloat(fabs(value), 1, rightX, h/2 + 20); // Use absolute value
        
        // --- Minus Sign / Indicator Area ---
        // Clear old indicator area
        tft.fillRect(w - 60, 5, 55, 40, TFT_BLACK);
        
        if (value < 0) {
            tft.setTextColor(TFT_RED);
            tft.setTextSize(4);
            tft.setTextDatum(TR_DATUM);
            tft.drawString("-", w - 10, 5);
        }
        
        // --- Direction Arrow - Top Left like SuperMini ---
        tft.fillRect(5, 5, 40, 40, TFT_BLACK); // Clear arrow area
        drawArrow(axis, positive, 5, 5, 30);
        
        lastVal = value;
    }
}

void HAL_TDisplay::drawArrow(char axis, bool positive, int x, int y, int size) {
    uint16_t color = positive ? TFT_GREEN : TFT_RED;
    if (positive) {
        tft.fillTriangle(x, y + size, x + size/2, y, x + size, y + size, color);
    } else {
        tft.fillTriangle(x, y, x + size/2, y + size, x + size, y, color);
    }
}

void HAL_TDisplay::enterDeepSleep(uint32_t wakeAfterMs) {
    Serial.println("T-Display Entering Deep Sleep...");
    setScreenOn(false);
    
    esp_sleep_enable_timer_wakeup(wakeAfterMs * 1000); 
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TDISPLAY_BUTTON_1, 0); // Wake on button press (LOW)
    
    esp_deep_sleep_start();
}

void HAL_TDisplay::setupModemSleep() {
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

void HAL_TDisplay::enterLightSleep(uint32_t durationMs) {
    esp_sleep_enable_timer_wakeup(durationMs * 1000);
    esp_light_sleep_start();
}

float HAL_TDisplay::getSystemTemp() {
    // Simplified for ESP32 original which doesn't always have a calibrated internal sensor
    return 0.0f; 
}

bool HAL_TDisplay::isWakeRequested() {
    return digitalRead(TDISPLAY_BUTTON_1) == LOW || digitalRead(TDISPLAY_BUTTON_2) == LOW;
}

bool HAL_TDisplay::isStealthWake() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

#include "hal_tdisplay.h"
#include <WiFi.h>
#include <esp_wifi.h>

#include "logos.h"
#include "logo_posipro_rgb565.h"

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
    
    // High-Resolution Static Color Logo (160x112)
    int x = (240 - LOGO_WIDTH) / 2;
    int y = (135 - LOGO_HEIGHT) / 2 - 5; 
    
    tft.setSwapBytes(true); 
    tft.pushImage(x, y, LOGO_WIDTH, LOGO_HEIGHT, logo_posipro_rgb565);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(4);
    tft.setTextDatum(BC_DATUM);
    // Draw version slightly below logo
    tft.setTextFont(2);
    tft.drawString(version, 120, 133);
    
    delay(3000); 
}

void HAL_TDisplay::drawSearching(uint8_t channel, float temp, bool fullSweep, int8_t rssi) {
    // Force clear on state transition
    if (_lastState != UI_STATE_SEARCHING) {
        tft.fillScreen(TFT_BLACK);
        
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setTextFont(2); 
        tft.setTextDatum(TL_DATUM);
        tft.drawString("DISCONNECTED", 10, 10);
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextFont(4);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Searching Controller...", 10, 40);
        
        _lastState = UI_STATE_SEARCHING;
        _lastChannel = 0; // Force channel redraw
        _lastRssi = -101; // Force signal redraw
    }

    // Dynamic Signal bars while searching (if any signal detected)
    int bucket = (rssi <= -95) ? 0 : (rssi > -60 ? 4 : (rssi > -75 ? 3 : (rssi > -85 ? 2 : 1)));
    if (bucket != _lastRssi) {
        drawSignalIcon(tft.width() - 22, 5, rssi);
        _lastRssi = bucket;
    }

    // 2. Dynamic Channel Number
    if (channel != _lastChannel) {
        tft.setTextFont(4);
        tft.setTextColor(0xFFE0, TFT_BLACK); // Yellow
        tft.setTextDatum(TL_DATUM);
        tft.setTextPadding(220); 
        tft.drawString("Channel: " + String(channel), 10, 75);
        _lastChannel = channel;
    }
    
    // 3. Dynamic Progress Bar
    int maxWidth = 220;
    int progressWidth = (channel * maxWidth) / 13;
    if (progressWidth > maxWidth) progressWidth = maxWidth;

    tft.drawRect(10, 105, maxWidth + 2, 14, 0x001F); // Blue
    tft.fillRect(11, 106, progressWidth, 12, 0x07FF); // Cyan
    tft.fillRect(11 + progressWidth, 106, maxWidth - progressWidth, 12, TFT_BLACK); 
}

void HAL_TDisplay::drawActiveDRO(const TelemetryPacket& data, uint8_t channel, int8_t rssi) {
    int w = tft.width();
    
    // Status colors
    uint16_t statusColor = TFT_DARKGREY;
    const char* statusText = "READY";
    switch(data.status) {
        case 0: statusColor = 0x2124; statusText = "READY"; break; // Metallic 
        case 1: statusColor = 0x03E0; statusText = "MOVING"; break; // Green
        case 2: statusColor = 0xFBE0; statusText = "ALARM!"; break; // Orange
        case 3: statusColor = 0xF800; statusText = "E-STOP!"; break; // Red
    }

    // Force clear and label redraw on state transition or major change
    if (_lastState != UI_STATE_ACTIVE || data.status != _lastStatus || channel != _lastChannel) {
        if (_lastState != UI_STATE_ACTIVE) {
            tft.fillScreen(TFT_BLACK);
            _lastState = UI_STATE_ACTIVE;
        }

        tft.fillRect(0, 0, w, 22, statusColor);
        tft.setTextColor(TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(statusText, 5, 11);
        
        tft.setTextFont(2);
        tft.setTextColor(TFT_YELLOW);
        tft.setTextDatum(MR_DATUM);
        tft.drawString("CH" + String(channel), w - 28, 11);
        
        // Signal Icon (Initial draw)
        drawSignalIcon(w - 22, 3, rssi);

        // Draw the static labels
        int labelX = 20;
        tft.setTextFont(4);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(0x07FF, TFT_BLACK); tft.drawString("X:", labelX, 45); 
        tft.setTextColor(0xF81F, TFT_BLACK); tft.drawString("Y:", labelX, 80); 
        tft.setTextColor(0xFFE0, TFT_BLACK); tft.drawString("Z:", labelX, 115); 

        _lastStatus = data.status;
        _lastChannel = channel;
        _lastRssi = (rssi <= -95) ? 0 : (rssi > -60 ? 4 : (rssi > -75 ? 3 : (rssi > -85 ? 2 : 1)));
        _lx = 99999; _ly = 99999; _lz = 99999;
    }

    // Signal Icon Dynamic Update (only if bucket changes)
    int bucket = (rssi <= -95) ? 0 : (rssi > -60 ? 4 : (rssi > -75 ? 3 : (rssi > -85 ? 2 : 1)));
    if (bucket != _lastRssi) {
        tft.fillRect(w - 22, 0, 22, 22, statusColor);
        drawSignalIcon(w - 22, 3, rssi);
        _lastRssi = bucket;
    }

    // 2. Dynamic Numbers
    int rightX = w - 10;
    tft.setTextPadding(160); 
    tft.setTextFont(4); 
    tft.setTextDatum(MR_DATUM);

    if (data.x != _lx) {
        tft.setTextColor(0x07FF, TFT_BLACK);
        tft.drawFloat(data.x, 2, rightX, 45);
        _lx = data.x;
    }
    
    if (data.y != _ly) {
        tft.setTextColor(0xF81F, TFT_BLACK);
        tft.drawFloat(data.y, 2, rightX, 80); 
        _ly = data.y;
    }
    
    if (data.z != _lz) {
        tft.setTextColor(0xFFE0, TFT_BLACK);
        tft.drawFloat(data.z, 2, rightX, 115); 
        _lz = data.z;
    }
}

void HAL_TDisplay::drawGiantDRO(char axis, float value, bool positive) {
    int w = tft.width();
    int h = tft.height();

    // 1. Redraw background/static parts ONLY when axis changes or state transition
    if (_lastState != UI_STATE_GIANT || axis != _lastAxis) {
        tft.fillScreen(TFT_BLACK);
        _lastState = UI_STATE_GIANT;
        
        // --- Axis Name (Top Center) - Font 4 Scaled (Bold) ---
        uint16_t color = (axis == 'X') ? 0x07FF : (axis == 'Y' ? 0xF81F : 0xFFE0);
        tft.setTextColor(color, TFT_BLACK);
        tft.setTextFont(4); 
        tft.setTextSize(2); // ~52px
        tft.setTextDatum(TC_DATUM);
        tft.drawString(String(axis).c_str(), w/2, 2); 

        _lastAxis = axis;
        _lx = 999999; // Using _lx as proxy for huge value comparison
    }

    // 2. Dynamic Update - Huge and Right Justified
    if (value != _lx) {
        uint16_t color = (axis == 'X') ? 0x07FF : (axis == 'Y' ? 0xF81F : 0xFFE0);
        
        // --- Number (Absolute Integer) - Font 8 ---
        tft.setTextFont(8); // Huge 75px
        tft.setTextSize(1);
        tft.setTextDatum(MR_DATUM);
        int rightX = w - 10;
        tft.setTextPadding(150); // Reduced padding to avoid clearing central axis label
        tft.setTextColor(color, TFT_BLACK);
        tft.drawNumber((long)fabs(value), rightX, h/2 + 20); // Moved UP to avoid clipping
        
        // --- Minus Sign / Indicator Area (Mega Bold Red Bar) ---
        // Clear area first
        tft.fillRect(w - 60, 2, 55, 45, TFT_BLACK);
        
        if (value < 0) {
            // Draw a massive red bar for the minus sign
            int barWidth = 40;
            int barHeight = 12; // Extra bold
            tft.fillRect(w - 5 - barWidth, 15, barWidth, barHeight, TFT_RED);
        }
        
        // --- Direction Arrow ---
        tft.fillRect(5, 5, 40, 40, TFT_BLACK);
        drawArrow(axis, positive, 5, 5, 30);
        
        _lx = value;
    }
}

void HAL_TDisplay::drawSignalIcon(int x, int y, int8_t rssi) {
    // Determine number of bars
    int bars = 0;
    uint16_t color = 0xF800; // Red
    if (rssi > -60) { bars = 4; color = 0x07E0; } // Green
    else if (rssi > -75) { bars = 3; color = 0xAFE5; } // Lime
    else if (rssi > -85) { bars = 2; color = 0xFFE0; } // Yellow
    else if (rssi > -95) { bars = 1; color = 0xF800; } // Red
    
    // Draw 4 bar placeholders (shadowed dark bars)
    for (int i=0; i<4; i++) {
        int bh = 4 + (i * 3);
        int by = y + 14 - bh;
        // Bar background
        tft.fillRect(x + (i*5), by, 3, bh, (i < bars) ? color : 0x4208); // 0x4208 = Dark Grey
    }
    
    // If absolutely no signal, draw a tiny red x next to bars
    if (rssi <= -100) {
        tft.drawLine(x, y + 10, x + 4, y + 14, TFT_RED);
        tft.drawLine(x + 4, y + 10, x, y + 14, TFT_RED);
    }
}

void HAL_TDisplay::drawArrow(char axis, bool positive, int x, int y, int size) {
    uint16_t color = positive ? TFT_GREEN : TFT_RED;
    
    if (axis == 'X') {
        // Horizontal arrows for X
        if (positive) {
            // Right Arrow
            tft.fillTriangle(x, y, x + size, y + size/2, x, y + size, color);
        } else {
            // Left Arrow
            tft.fillTriangle(x + size, y, x, y + size/2, x + size, y + size, color);
        }
    } else {
        // Vertical arrows for Y and Z
        if (positive) {
            // Up Arrow
            tft.fillTriangle(x, y + size, x + size/2, y, x + size, y + size, color);
        } else {
            // Down Arrow
            tft.fillTriangle(x, y, x + size/2, y + size, x + size, y, color);
        }
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

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

void HAL_TDisplay::drawSearching(uint8_t channel, float temp, bool fullSweep) {
    static uint8_t lastChan = 0;
    static bool firstDraw = true;

    // 1. Static Backdrop (Only once)
    if (firstDraw) {
        tft.fillScreen(TFT_BLACK);
        
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextFont(4); // Professional high-res
        tft.setTextSize(1);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("OFFLINE", 10, 10);
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextFont(2);
        tft.drawString("Scanning ESP-NOW Channels...", 10, 40);
        
        firstDraw = false;
        lastChan = 0; // Force update
    }

    // 2. Dynamic Channel Number
    if (channel != lastChan) {
        tft.setTextFont(4);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.setTextPadding(180); // Increased padding to ensure full string clearing
        tft.drawString("Channel: " + String(channel) + "  ", 10, 65);
        lastChan = channel;
    }
    
    // 3. Dynamic Progress Bar (Proportional to Channel 1-13)
    int maxWidth = 220;
    int progressWidth = (channel * maxWidth) / 13;
    if (progressWidth > maxWidth) progressWidth = maxWidth;

    tft.drawRect(10, 95, maxWidth + 2, 14, TFT_BLUE);
    tft.fillRect(11, 96, progressWidth, 12, TFT_CYAN);
    tft.fillRect(11 + progressWidth, 96, maxWidth - progressWidth, 12, TFT_BLACK); 
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
        tft.setTextFont(2); // 16px sharp font
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(statusText, w/2, 12);
        
        tft.setTextFont(2);
        tft.setTextColor(TFT_YELLOW);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(String("CH" + String(channel)).c_str(), w - 5, 5);
        
        // Draw the static labels ONCE
        tft.setTextFont(4); // 26px font
        tft.setTextSize(1); // No scaling needed, it's natively smooth
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);   tft.drawString("X:", labelX, 45);
        tft.setTextColor(TFT_MAGENTA, TFT_BLACK);tft.drawString("Y:", labelX, 80);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("Z:", labelX, 115);

        lastDrawStatus = data.status;
        lastChannel = channel;
    }

    // 2. Dynamic Numbers - Standard High-Res Dashboard (v1.6.4)
    int rightX = w - 10;
    tft.setTextPadding(160); 
    tft.setTextFont(4); 
    tft.setTextSize(1);
    tft.setTextDatum(MR_DATUM);

    if (data.x != lx) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawFloat(data.x, 2, rightX, 45);
        lx = data.x;
    }
    
    if (data.y != ly) {
        tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
        tft.drawFloat(data.y, 2, rightX, 85); 
        ly = data.y;
    }
    
    if (data.z != lz) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawFloat(data.z, 2, rightX, 120); 
        lz = data.z;
    }
}

void HAL_TDisplay::drawGiantDRO(char axis, float value, bool positive) {
    int w = tft.width();
    int h = tft.height();

    static char lastAxis = ' ';
    static float lastVal = 999999;

    // 1. Redraw background/static parts ONLY when axis changes
    if (axis != lastAxis) {
        tft.fillScreen(TFT_BLACK);
        
        // --- Axis Name (Top Center) - Font 4 Scaled (Bold) ---
        uint16_t color = (axis == 'X') ? TFT_CYAN : (axis == 'Y' ? TFT_MAGENTA : TFT_YELLOW);
        tft.setTextColor(color, TFT_BLACK);
        tft.setTextFont(4); 
        tft.setTextSize(2); // ~52px
        tft.setTextDatum(TC_DATUM);
        tft.drawString(String(axis).c_str(), w/2, 2); 

        lastAxis = axis;
        lastVal = 999999; 
    }

    // 2. Dynamic Update - Huge and Right Justified
    if (value != lastVal) {
        uint16_t color = (axis == 'X') ? TFT_CYAN : (axis == 'Y' ? TFT_MAGENTA : TFT_YELLOW);
        
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
        
        lastVal = value;
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

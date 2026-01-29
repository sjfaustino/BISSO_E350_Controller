#include "hal_tdisplay.h"
#include <WiFi.h>
#include <esp_wifi.h>

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
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 40);
    tft.println("BISSO E350");
    tft.setCursor(20, 70);
    tft.println("REMOTE DRO");
    
    tft.setTextSize(1);
    tft.setCursor(20, 100);
    tft.printf("Version: %s", version);
    tft.setCursor(20, 115);
    tft.printf("System Temp: %.1fC", temp);
    
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
    tft.fillScreen(TFT_BLACK);
    
    // Status Bar
    tft.fillRect(0, 0, 240, 20, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    switch(data.status) {
        case 0: tft.print("READY"); break;
        case 1: tft.setTextColor(TFT_GREEN); tft.print("MOVING"); break;
        case 2: tft.setTextColor(TFT_ORANGE); tft.print("ALARM"); break;
        case 3: tft.setTextColor(TFT_RED); tft.print("E-STOP"); break;
    }
    
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(180, 5);
    tft.printf("CH %d", channel);

    // DRO Values
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    tft.setCursor(10, 30);
    tft.printf("X: %8.1f", data.x);
    tft.setCursor(10, 65);
    tft.printf("Y: %8.1f", data.y);
    tft.setCursor(10, 100);
    tft.printf("Z: %8.1f", data.z);
}

void HAL_TDisplay::drawGiantDRO(char axis, float value, bool positive) {
    tft.fillScreen(TFT_BLACK);
    
    uint16_t color = TFT_WHITE;
    if (axis == 'X') color = TFT_CYAN;
    else if (axis == 'Y') color = TFT_MAGENTA;
    else if (axis == 'Z') color = TFT_YELLOW;

    drawArrow(axis, positive, 180, 40, 40);
    
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(4);
    tft.setCursor(20, 20);
    tft.print(axis);
    
    tft.setTextSize(6);
    tft.setCursor(20, 70);
    tft.printf("%.1f", value);
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

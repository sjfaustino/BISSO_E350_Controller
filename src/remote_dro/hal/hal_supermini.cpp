#include "hal_supermini.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "../logos.h"

// --- Pin Definitions for SuperMini ---
#define STATUS_LED    8  
#define BOOT_BUTTON   9  
#define WAKE_BUTTON   0  

// --- Display Config ---
#define SCREEN_WIDTH  128 
#define SCREEN_HEIGHT 64 
#define OLED_X_OFFSET 28 
#define OLED_Y_OFFSET 12 
#define LOGO_Y_OFFSET 26 
#define OLED_RESET    -1

HAL_SuperMini::HAL_SuperMini() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

void HAL_SuperMini::init() {
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH); 
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    pinMode(WAKE_BUTTON, INPUT_PULLUP);

    // PM Setup
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);

    // Temp Setup
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();

    // I2C Setup
#if defined(OLED_SDA) && defined(OLED_SCL)
    Wire.begin(OLED_SDA, OLED_SCL);
#else
    Wire.begin(); 
#endif
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
    }
}

void HAL_SuperMini::update() {
    // LED heartbeat is handled in main loop for now to avoid timer overhead here,
    // but could be moved here if we want absolute abstraction.
}

void HAL_SuperMini::setScreenOn(bool on) {
    display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
}

void HAL_SuperMini::showSplash(const char* version, float temp) {
    display.clearDisplay();
    display.drawBitmap(OLED_X_OFFSET, LOGO_Y_OFFSET, logo_saw_bmp, 72, 40, SSD1306_WHITE, SSD1306_BLACK);
    display.display();
    delay(1000);
    
    display.clearDisplay();
    display.drawBitmap(OLED_X_OFFSET, LOGO_Y_OFFSET, logo_posipro_bmp, 72, 40, SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(46, 55); 
    display.print(version);
    
    display.setCursor(28+OLED_X_OFFSET, 0+OLED_Y_OFFSET);
    display.printf("%.1fC", temp);
    
    display.display();
    delay(2000);
}

void HAL_SuperMini::drawSearching(uint8_t channel, float temp, bool fullSweep) {
    display.clearDisplay();
    display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println("OFFLINE");
    display.setCursor(0 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
    display.printf("Searching...");
    display.setCursor(0 + OLED_X_OFFSET, 22 + OLED_Y_OFFSET);
    display.printf("Channel %d", channel);
    
    int barWidth = (millis() / 100) % 72;
    display.drawFastHLine(0 + OLED_X_OFFSET, 35+OLED_Y_OFFSET, barWidth, WHITE);
    display.display();
}

void HAL_SuperMini::drawActiveDRO(const TelemetryPacket& data, uint8_t channel) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
    
    switch(data.status) {
        case 0: display.print("READY"); break;
        case 1: display.print("MOVING"); break;
        case 2: display.print("ALARM"); break;
        case 3: display.print("E-STOP"); break;
        default: display.print("BUSY"); break;
    }

    display.setCursor(45 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
    display.printf("CH%d", channel);

    display.setCursor(0 + OLED_X_OFFSET, 10 + OLED_Y_OFFSET);
    display.printf("X:%7.1f", data.x);
    display.setCursor(0 + OLED_X_OFFSET, 20 + OLED_Y_OFFSET);
    display.printf("Y:%7.1f", data.y);
    display.setCursor(0 + OLED_X_OFFSET, 30 + OLED_Y_OFFSET);
    display.printf("Z:%7.1f", data.z);
    display.display();
}

void HAL_SuperMini::drawGiantDRO(char axis, float value, bool positive) {
    display.clearDisplay();
    drawArrow(axis, positive);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(30 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
    display.print(axis);
    
    bool isNeg = (value < 0);
    float absVal = abs(value);

    if (isNeg) {
        display.setCursor(60 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
        display.print("-");
    }
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", absVal);
    int textWidth = strlen(buf) * 12; 
    int x_pos = 72 - textWidth;
    if (x_pos < 0) x_pos = 0;
    display.setCursor(x_pos + OLED_X_OFFSET, 36 + OLED_Y_OFFSET); 
    display.print(buf);
    display.display();
}

void HAL_SuperMini::drawArrow(char axis, bool positive) {
    int bx = 0 + OLED_X_OFFSET;
    int by = 12 + OLED_Y_OFFSET;
    int size = 14;
    
    if (axis == 'X') {
        if (positive) { 
            display.drawLine(bx, by + 7, bx + size, by + 7, WHITE);
            display.drawLine(bx + size, by + 7, bx + size - 4, by + 3, WHITE);
            display.drawLine(bx + size, by + 7, bx + size - 4, by + 11, WHITE);
        } else { 
            display.drawLine(bx + size, by + 7, bx, by + 7, WHITE);
            display.drawLine(bx, by + 7, bx + 4, by + 3, WHITE);
            display.drawLine(bx, by + 7, bx + 4, by + 11, WHITE);
        }
    } else if (axis == 'Y') {
        if (positive) { 
            display.drawLine(bx, by + size, bx + size, by, WHITE);
            display.drawLine(bx + size, by, bx + size - 6, by, WHITE);
            display.drawLine(bx + size, by, bx + size, by + 6, WHITE);
        } else { 
            display.drawLine(bx + size, by, bx, by + size, WHITE);
            display.drawLine(bx, by + size, bx + 6, by + size, WHITE);
            display.drawLine(bx, by + size, bx, by + size - 6, WHITE);
        }
    } else if (axis == 'Z') {
        if (positive) { 
            display.drawLine(bx + 7, by + size, bx + 7, by, WHITE);
            display.drawLine(bx + 7, by, bx + 3, by + 4, WHITE);
            display.drawLine(bx + 7, by, bx + 11, by + 4, WHITE);
        } else { 
            display.drawLine(bx + 7, by, bx + 7, by + size, WHITE);
            display.drawLine(bx + 7, by + size, bx + 3, by + size - 4, WHITE);
            display.drawLine(bx + 7, by + size, bx + 11, by + size - 4, WHITE);
        }
    }
}

void HAL_SuperMini::enterDeepSleep(uint32_t wakeAfterMs) {
    Serial.println("Entering Deep Sleep...");
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    esp_sleep_enable_timer_wakeup(wakeAfterMs * 1000); 
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    
    digitalWrite(STATUS_LED, LOW); // ON
    delay(500);
    digitalWrite(STATUS_LED, HIGH); // OFF
    
    esp_deep_sleep_start();
}

void HAL_SuperMini::setupModemSleep() {
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

void HAL_SuperMini::enterLightSleep(uint32_t durationMs) {
    esp_sleep_enable_timer_wakeup(durationMs * 1000);
    esp_light_sleep_start();
}

float HAL_SuperMini::getSystemTemp() {
    float tsens_out;
    temp_sensor_read_celsius(&tsens_out);
    return tsens_out;
}

bool HAL_SuperMini::isWakeRequested() {
    return digitalRead(WAKE_BUTTON) == LOW; // Button is active LOW
}

bool HAL_SuperMini::isStealthWake() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

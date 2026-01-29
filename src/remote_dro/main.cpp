#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>         
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>      
#include <esp_pm.h>           // For Power Management
#include <esp_sleep.h>        // For Light Sleep
#include "../telemetry_packet.h" 
#include "logos.h"

// --- Configuration ---
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_X_OFFSET 28 
#define OLED_Y_OFFSET 12 
#define LOGO_Y_OFFSET 26 
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Power & Timing Configuration ---
#define HOP_INTERVAL_MS 150   
#define DATA_TIMEOUT_MS 3000  
#define HEARTBEAT_MS    100   // Main controller broadcasts at 10Hz
#define SLEEP_GUARD_MS  15    // Wake up 15ms before expected packet
#define MAX_CHANNELS    13

// Data state
TelemetryPacket data;
TelemetryPacket prevData;
bool dataReceived = false;
uint32_t lastPacketTime = 0;
uint8_t currentChannel = 1;
bool isHopping = true;
uint32_t lastHopTime = 0;

Preferences prefs;

// Movement detection state
char activeAxis = ' ';
uint32_t lastMoveTime = 0;
const float MOVEMENT_THRESHOLD = 0.5f; 

// Helper for drawing arrows
void drawArrow(char axis, bool positive) {
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

// Callback when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(TelemetryPacket)) {
        memcpy(&data, incomingData, sizeof(data));
        dataReceived = true;
        lastPacketTime = millis();
        
        if (isHopping) {
            isHopping = false;
            prefs.putUChar("last_chan", currentChannel);
            Serial.printf("Data found on Channel %d - SAVED\n", currentChannel);
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize Preferences
    prefs.begin("dro_cfg", false);
    currentChannel = prefs.getUChar("last_chan", 1);
    if (currentChannel < 1 || currentChannel > 13) currentChannel = 1;

    // 1. Configure Power Management BEFORE WiFi
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);

    // Initialize I2C
#if defined(OLED_SDA) && defined(OLED_SCL)
    Wire.begin(OLED_SDA, OLED_SCL);
#else
    Wire.begin(); 
#endif
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        for(;;);
    }
    
    // Boot Logos
    display.clearDisplay();
    display.drawBitmap(OLED_X_OFFSET, LOGO_Y_OFFSET, logo_saw_bmp, 72, 40, SSD1306_WHITE, SSD1306_BLACK);
    display.display();
    delay(1000);
    
    display.clearDisplay();
    display.drawBitmap(OLED_X_OFFSET, LOGO_Y_OFFSET, logo_posipro_bmp, 72, 40, SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(46, 55); 
    display.print("v1.2.0");
    display.display();
    delay(1000);
    
    // ESP-NOW Initial Setting
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // Enable modem sleep 
    
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("Starting search on channel %d\n", currentChannel);

    if (esp_now_init() != ESP_OK) {
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
    uint32_t now = millis();

    // --- Channel Hopping State Machine ---
    if (now - lastPacketTime > DATA_TIMEOUT_MS) {
        if (!isHopping) {
            isHopping = true;
            lastHopTime = now;
            Serial.println("Connection lost. Resuming channel hop...");
        }

        if (now - lastHopTime > HOP_INTERVAL_MS) {
            currentChannel++;
            if (currentChannel > MAX_CHANNELS) currentChannel = 1;
            
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            lastHopTime = now;
        }
    }

    display.clearDisplay();
    
    if (isHopping) {
        // --- Searching UI ---
        display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
        display.setTextSize(1);
        display.println("OFFLINE");
        display.setCursor(0 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
        display.printf("Searching...");
        display.setCursor(0 + OLED_X_OFFSET, 22 + OLED_Y_OFFSET);
        display.printf("Channel %d", currentChannel);
        
        int barWidth = (now / 100) % 72;
        display.drawFastHLine(0 + OLED_X_OFFSET, 35+OLED_Y_OFFSET, barWidth, WHITE);
    } else {
        // --- Active DRO UI ---
        bool moved = false;
        if (abs(data.x - prevData.x) > MOVEMENT_THRESHOLD) { activeAxis = 'X'; moved = true; }
        else if (abs(data.y - prevData.y) > MOVEMENT_THRESHOLD) { activeAxis = 'Y'; moved = true; }
        else if (abs(data.z - prevData.z) > MOVEMENT_THRESHOLD) { activeAxis = 'Z'; moved = true; }
        
        if (moved) {
            lastMoveTime = now;
        }
        prevData = data;

        bool showGiant = (activeAxis != ' ' && (now - lastMoveTime < 1000));

        if (showGiant) {
            float val = 0;
            if (activeAxis == 'X') val = data.x;
            else if (activeAxis == 'Y') val = data.y;
            else if (activeAxis == 'Z') val = data.z;

            float absVal = abs(val);
            bool isNeg = (val < 0);

            drawArrow(activeAxis, !isNeg);
            display.setTextSize(2);
            display.setCursor(30 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
            display.print(activeAxis);
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
        } else {
            display.setTextSize(1);
            display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
            
            switch(data.status) {
                case 0: display.print("READY"); break;
                case 1: display.print("MOVING"); break;
                case 2: display.print("ALARM"); break;
                case 3: display.print("E-STOP"); break;
                default: display.print("BUSY"); break;
            }

            display.setCursor(45 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
            display.printf("CH%d", currentChannel);

            display.setCursor(0 + OLED_X_OFFSET, 10 + OLED_Y_OFFSET);
            display.printf("X:%7.1f", data.x);
            display.setCursor(0 + OLED_X_OFFSET, 20 + OLED_Y_OFFSET);
            display.printf("Y:%7.1f", data.y);
            display.setCursor(0 + OLED_X_OFFSET, 30 + OLED_Y_OFFSET);
            display.printf("Z:%7.1f", data.z);
        }
    }
    
    display.display();

    // --- Synchronized Light Sleep ---
    // Calculate if we can "nap" between heartbeats
    if (!isHopping) {
        uint32_t timeSinceLastPacket = millis() - lastPacketTime;
        if (timeSinceLastPacket < (HEARTBEAT_MS - SLEEP_GUARD_MS)) {
            uint32_t napDuration = (HEARTBEAT_MS - SLEEP_GUARD_MS) - timeSinceLastPacket;
            if (napDuration > 10) { // Only sleep if it's worth it
                // Enable timer wake up for the calculated nap duration
                esp_sleep_enable_timer_wakeup(napDuration * 1000); // ms to us
                esp_light_sleep_start();
            }
        }
    } else {
        delay(50); // Standard delay while hopping to keep UI responsive
    }
}

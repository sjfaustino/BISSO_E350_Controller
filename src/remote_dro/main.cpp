/**
 * BISSO E350 - Remote DRO Receiver (ESP-NOW)
 * 
 * Target: ESP32 / ESP32-S3 / ESP32-C3
 * Display: 0.96" OLED (SSD1306 I2C) on SDA/SCL pins
 * 
 * This is part of the BISSO E350 project.
 * Build with: pio run -e remote_dro
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>         // Required for channel hopping
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>      // For channel persistence
#include "../telemetry_packet.h" 
#include "logos.h"            // Boot bitmaps (Saw, POSIPRO)

// --- Configuration ---
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_X_OFFSET 28 
#define OLED_Y_OFFSET 12 
#define LOGO_Y_OFFSET 26 
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Fast Hopping Configuration ---
#define HOP_INTERVAL_MS 150   // Fast scan: 150ms per channel
#define DATA_TIMEOUT_MS 3000  // 3s without data triggers a re-scan
#define MAX_CHANNELS 13

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
        
        // If we were hopping and received a valid packet, LOCK this channel
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
    display.print("v1.1.0");
    display.display();
    delay(1000);
    
    // ESP-NOW Initial Setting
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    // Set to the last known channel before initializing ESP-NOW
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
        // We are officially "OFFLINE"
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
            // Note: Serial printing here might slow down hopping, keeping it for debug for now
            // Serial.printf("Hopping to CH:%d\n", currentChannel);
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
        
        // Animated scanning bar
        int barWidth = (now / 100) % 72;
        display.drawFastHLine(0 + OLED_X_OFFSET, 35+OLED_Y_OFFSET, barWidth, WHITE);
    } else {
        // --- Active DRO UI ---
        // Movement Detection
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
    delay(50); // Increased update rate for smoother hopping UI
}

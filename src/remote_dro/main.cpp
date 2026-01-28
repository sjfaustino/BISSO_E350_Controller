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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../telemetry_packet.h" // Shared structure

// --- Configuration ---
#define SCREEN_WIDTH 128 // Use full controller width (128) to handle hardware offsets
#define SCREEN_HEIGHT 64 // Use full controller height to clear all residuals
#define OLED_X_OFFSET 28 // 0.42" OLEDs are often centered at column 28
#define OLED_Y_OFFSET 15 // Increased from 12 to 15 to fix top clipping
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Debug Configuration ---
#define SIMULATE_MOVEMENT 1  // Set to 1 to test Giant-Text UI without machine connection
#define MAX_MACHINE_DIM   3500.0f

// Data state
TelemetryPacket data;
TelemetryPacket prevData;
bool dataReceived = false;
uint32_t lastPacketTime = 0;

// Movement detection state
char activeAxis = ' ';
uint32_t lastMoveTime = 0;
const float MOVEMENT_THRESHOLD = 0.5f; // mm change to trigger giant text

// Callback when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(TelemetryPacket)) {
        memcpy(&data, incomingData, sizeof(data));
        dataReceived = true;
        lastPacketTime = millis();
    }
}

void setup() {
    Serial.begin(115200);
    
    // 0. Initialize I2C with hardware-specific pins (from platformio.ini)
#if defined(OLED_SDA) && defined(OLED_SCL)
    Wire.begin(OLED_SDA, OLED_SCL);
    Serial.printf("I2C initialized on SDA:%d, SCL:%d\n", OLED_SDA, OLED_SCL);
#else
    Wire.begin(); // Default pins
#endif
    
    // 1. Initialize Display
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
    display.println("BISSO E350 DRO");
    display.setCursor(0 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
    display.println("Connecting...");
    display.display();

    // 2. Initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    
    // Antena/WiFi Stability Fix (from AliExpress reviews)
    // These tiny boards have poor antenna isolation. Reducing TX power improves stability.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
    esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
    display.clearDisplay();
    
#if SIMULATE_MOVEMENT
    // Fake data for UI testing
    static uint32_t lastSimUpdate = 0;
    static uint32_t lastPauseTime = 0;
    static bool isPaused = false;

    if (!isPaused) {
        if (millis() - lastSimUpdate > 500) {
            // Randomly pick an axis to "move" every 0.5s
            int axis = random(0, 3);
            if (axis == 0) data.x = (float)random(0, MAX_MACHINE_DIM * 10) / 10.0f;
            else if (axis == 1) data.y = (float)random(0, MAX_MACHINE_DIM * 10) / 10.0f;
            else data.z = (float)random(0, 500 * 10) / 10.0f;
            
            data.status = 1; // MOVING
            data.uptime = millis() / 1000;
            lastPacketTime = millis();
            dataReceived = true;
            lastSimUpdate = millis();
        }

        // Pause every 5 seconds of "movement"
        if (millis() - lastPauseTime > 5000) {
            isPaused = true;
            lastPauseTime = millis();
        }
    } else {
        // Stay paused for 2 seconds to show normal display
        if (millis() - lastPauseTime > 2000) {
            isPaused = false;
            lastPauseTime = millis();
        }
    }
#endif

    // Check for timeout (1 second)
    if (millis() - lastPacketTime > 1000) {
        display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
        display.setTextSize(1);
        display.println("OFFLINE");
        display.setCursor(0 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET);
        display.println("Waiting for");
        display.setCursor(0 + OLED_X_OFFSET, 22 + OLED_Y_OFFSET);
        display.println("controller...");
    } else {
        // --- Movement Detection ---
        bool moved = false;
        if (abs(data.x - prevData.x) > MOVEMENT_THRESHOLD) { activeAxis = 'X'; moved = true; }
        else if (abs(data.y - prevData.y) > MOVEMENT_THRESHOLD) { activeAxis = 'Y'; moved = true; }
        else if (abs(data.z - prevData.z) > MOVEMENT_THRESHOLD) { activeAxis = 'Z'; moved = true; }
        
        if (moved) {
            lastMoveTime = millis();
        }
        prevData = data;

        // --- Render Logic ---
        bool showGiant = (activeAxis != ' ' && (millis() - lastMoveTime < 1000));

        if (showGiant) {
            // Giant Text Mode: Centered Axis on top, Position on bottom
            float val = 0;
            if (activeAxis == 'X') val = data.x;
            else if (activeAxis == 'Y') val = data.y;
            else if (activeAxis == 'Z') val = data.z;

            // 1. Top Line: Axis Label (Centered)
            // Shifted way down (+12 total) to fix severe top clipping on tiny OLED
            display.setTextSize(2);
            display.setCursor(28 + OLED_X_OFFSET, 12 + OLED_Y_OFFSET); 
            display.print(activeAxis);
            
            // 2. Bottom Line: Value (Right-justified with 1 decimal)
            display.setTextSize(2);
            display.setCursor(0 + OLED_X_OFFSET, 30 + OLED_Y_OFFSET);
            // %6.1f ensures 6 characters total (e.g. "3500.0"), right-justified
            display.printf("%6.1f", val);
            
            // 3. Small Uptime (corner) - Moved to bottom edge to stay out of the way
            display.setTextSize(1);
            display.setCursor(45 + OLED_X_OFFSET, 50 + OLED_Y_OFFSET);
            display.printf("%lus", data.uptime);
        } else {
            // Normal 3-Axis View
            display.setTextSize(1);
            display.setCursor(0 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
            
            switch(data.status) {
                case 0: display.print("READY"); break;
                case 1: display.print("MOVING"); break;
                case 2: display.print("ALARM"); break;
                case 3: display.print("E-STOP"); break;
                default: display.print("UNKNOWN"); break;
            }

            display.setCursor(45 + OLED_X_OFFSET, 0 + OLED_Y_OFFSET);
            display.printf("%lus", data.uptime);

            // X Axis
            display.setTextSize(1);
            display.setCursor(0 + OLED_X_OFFSET, 10 + OLED_Y_OFFSET);
            display.printf("X:%7.1f", data.x);
            
            // Y Axis
            display.setCursor(0 + OLED_X_OFFSET, 20 + OLED_Y_OFFSET);
            display.printf("Y:%7.1f", data.y);
            
            // Z Axis
            display.setCursor(0 + OLED_X_OFFSET, 30 + OLED_Y_OFFSET);
            display.printf("Z:%7.1f", data.z);
        }
    }
    
    display.display();
    delay(100); // 10Hz refresh
}

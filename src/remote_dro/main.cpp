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
#define SCREEN_WIDTH 72  // 0.42" OLED resolution
#define SCREEN_HEIGHT 40
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Data state
TelemetryPacket data;
bool dataReceived = false;
uint32_t lastPacketTime = 0;

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
    display.setCursor(0,0);
    display.println("BISSO E350 DRO");
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
    
    // Check for timeout (1 second)
    if (millis() - lastPacketTime > 1000) {
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.println("OFFLINE");
        display.setCursor(0, 20);
        display.println("Waiting for");
        display.println("controller...");
    } else {
        // Display Coordinates
        display.setTextSize(1);
        display.setCursor(0, 0);
        
        switch(data.status) {
            case 0: display.print("READY"); break;
            case 1: display.print("MOVING"); break;
            case 2: display.print("ALARM"); break;
            case 3: display.print("E-STOP"); break;
            default: display.print("UNKNOWN"); break;
        }

        display.setCursor(45, 0);
        display.printf("%lus", data.uptime);

        // X Axis
        display.setTextSize(1); // Smaller text for 72x40 screen
        display.setCursor(0, 10);
        display.printf("X:%7.1f", data.x);
        
        // Y Axis
        display.setCursor(0, 20);
        display.printf("Y:%7.1f", data.y);
        
        // Z Axis
        display.setCursor(0, 30);
        display.printf("Z:%7.1f", data.z);
    }
    
    display.display();
    delay(100); // 10Hz refresh
}

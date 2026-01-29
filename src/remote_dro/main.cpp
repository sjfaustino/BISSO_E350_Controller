#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>         
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>      
#include <esp_pm.h>           // For Power Management
#include <esp_sleep.h>        // For Light Sleep
#include "driver/temp_sensor.h" // For internal temp sensor
#include "../telemetry_packet.h" 
#include "logos.h"

// --- Configuration ---
#define SCREEN_WIDTH  128 
#define SCREEN_HEIGHT 64 
#define OLED_X_OFFSET 28 
#define OLED_Y_OFFSET 12 
#define LOGO_Y_OFFSET 26 
#define OLED_RESET    -1

#define STATUS_LED    8  // Blue LED on SuperMini C3
#define BOOT_BUTTON   9  // BOOT button on SuperMini C3 (Active LOW)
#define WAKE_BUTTON   0  // RTC-capable GPIO for Deep Sleep Wakeup (Pin D0)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Power & Timing Configuration ---
#define HOP_INTERVAL_MS      150   
#define DATA_TIMEOUT_MS      3000  
#define HEARTBEAT_MS         100   // Main controller broadcasts at 10Hz
#define SLEEP_GUARD_MS       15    // Wake up 15ms before expected packet
#define MAX_CHANNELS         13

#define SCREEN_TIMEOUT_MS    120000 // 2 minutes
#define DEEP_SLEEP_TIMEOUT_MS 300000 // 5 minutes (OFFLINE)
#define DEEP_SLEEP_WAKE_MS   300000 // 5 minutes periodic wake

// Movement sensitivity
#define IDLE_MOVE_THRESHOLD  0.05f  // Stricter for power timeout

// Data state
TelemetryPacket data;
TelemetryPacket prevData;
bool dataReceived = false;
uint32_t lastPacketTime = 0;
uint8_t currentChannel = 1;
bool isHopping = true;
uint32_t lastHopTime = 0;

// Power state
bool screenOn = true;
uint32_t lastMoveTimeStrict = 0;
float lastPositionX = 0, lastPositionY = 0, lastPositionZ = 0;
bool stealthMode = false;
uint32_t sessionStartTime = 0;

Preferences prefs;

// Movement detection state (UI)
char activeAxis = ' ';
uint32_t lastMoveTimeUI = 0;
const float UI_MOVE_THRESHOLD = 0.5f; 

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
        
        if (stealthMode) {
            stealthMode = false;
            display.ssd1306_command(SSD1306_DISPLAYON);
            Serial.println("Machine detected! Exiting stealth mode...");
        }

        if (isHopping) {
            isHopping = false;
            prefs.putUChar("last_chan", currentChannel);
            Serial.printf("Data found on Channel %d\n", currentChannel);
        }
    }
}

float getSystemTemp() {
    float tsens_out;
    temp_sensor_read_celsius(&tsens_out);
    return tsens_out;
}

void enterDeepSleep() {
    Serial.println("Entering Deep Sleep...");
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    
    // Configure Wake Sources for Deep Sleep
    // GPIO 0-5 are valid RTC wakeups on ESP32-C3. Pin 9 is NOT.
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_WAKE_MS * 1000); 
    
    // Final LED indicator (long blink)
    digitalWrite(STATUS_LED, LOW); // ON
    delay(500);
    digitalWrite(STATUS_LED, HIGH); // OFF
    
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        stealthMode = true;
    }
    sessionStartTime = millis();

    // Safety wait for USB CDC
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 2000);
    
    Serial.println("\n--- BISSO E350 Remote DRO starting ---");
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH); // Start OFF (Active LOW)
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    pinMode(WAKE_BUTTON, INPUT_PULLUP);

    // Initialize Preferences
    prefs.begin("dro_cfg", false);
    currentChannel = prefs.getUChar("last_chan", 1);
    if (currentChannel < 1 || currentChannel > 13) currentChannel = 1;

    // 1. Configure Power Management 
    // NOTE: This prioritizes battery. USB Serial may be disrupted during sleep.
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);

    // Initialize Internal Temp Sensor
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();

    // Initialize I2C
#if defined(OLED_SDA) && defined(OLED_SCL)
    Wire.begin(OLED_SDA, OLED_SCL);
#else
    Wire.begin(); 
#endif
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        for(;;);
    }
    if (stealthMode) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
    }
    
    if (!stealthMode) {
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
        display.print("v1.3.7");
        
        // Show Temp on boot
        display.setCursor(28+OLED_X_OFFSET, 0+OLED_Y_OFFSET);
        display.printf("%.1fC", getSystemTemp());
        
        display.display();
        delay(2000); // 2 second silent wait after showing version and temp.
    }
    
    // ESP-NOW Initial Setting
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // Enable modem sleep 
    
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[v1.3.7] Starting search on channel %d (System: %.1fC)\n", currentChannel, getSystemTemp());

    if (esp_now_init() != ESP_OK) {
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);
    
    lastPacketTime = millis();
    lastMoveTimeStrict = millis();
}

void loop() {
    uint32_t now = millis();

    // --- Status LED Heartbeat ---
    static uint32_t lastLedToggle = 0;
    uint32_t ledInterval = screenOn ? 5000 : 10000;
    if (now - lastLedToggle > ledInterval) {
        digitalWrite(STATUS_LED, LOW); // ON
        delay(50);
        digitalWrite(STATUS_LED, HIGH); // OFF
        if (screenOn) { // Double blink if active
            delay(150);
            digitalWrite(STATUS_LED, LOW);
            delay(50);
            digitalWrite(STATUS_LED, HIGH);
        }
        lastLedToggle = now;
    }

    // --- Deep Sleep Logic ---
    if (now - lastPacketTime > DEEP_SLEEP_TIMEOUT_MS) {
        enterDeepSleep();
    }

    // --- Movement Detection (Strict for Power Management) ---
    bool movedStrict = false;
    if (abs(data.x - lastPositionX) > IDLE_MOVE_THRESHOLD ||
        abs(data.y - lastPositionY) > IDLE_MOVE_THRESHOLD ||
        abs(data.z - lastPositionZ) > IDLE_MOVE_THRESHOLD) {
        movedStrict = true;
        lastPositionX = data.x;
        lastPositionY = data.y;
        lastPositionZ = data.z;
    }

    if (movedStrict) {
        lastMoveTimeStrict = now;
        if (!screenOn) {
            screenOn = true;
            display.ssd1306_command(SSD1306_DISPLAYON);
            Serial.println("Movement detected - Screen ON");
        }
    }

    // --- Screen Timeout Logic ---
    if (screenOn && (now - lastMoveTimeStrict > SCREEN_TIMEOUT_MS)) {
        screenOn = false;
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        Serial.println("Idle timeout - Screen OFF");
    }

    // --- Channel Hopping State Machine ---
    if (now - lastPacketTime > DATA_TIMEOUT_MS) {
        if (!isHopping) {
            isHopping = true;
            lastHopTime = now;
            Serial.println("Connection lost. Resuming channel hop...");
        }

        if (now - lastHopTime > HOP_INTERVAL_MS) {
            currentChannel++;
            if (currentChannel > MAX_CHANNELS) {
                currentChannel = 1;
                Serial.printf("[v1.3.7] Still searching... Full sweep done. System Temp: %.1fC\n", getSystemTemp());
            }
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            lastHopTime = now;
        }

        // Stealth Sniffing Timeout: If we woke up by timer and found nothing in 5s, go back to sleep.
        if (stealthMode && (now - sessionStartTime > 5000)) {
            Serial.println("Stealth check complete - no controller. Sleeping.");
            enterDeepSleep();
        }
    }

    // --- Rendering (Only if screen is on) ---
    if (screenOn) {
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
            bool movedUI = false;
            if (abs(data.x - prevData.x) > UI_MOVE_THRESHOLD) { activeAxis = 'X'; movedUI = true; }
            else if (abs(data.y - prevData.y) > UI_MOVE_THRESHOLD) { activeAxis = 'Y'; movedUI = true; }
            else if (abs(data.z - prevData.z) > UI_MOVE_THRESHOLD) { activeAxis = 'Z'; movedUI = true; }
            
            if (movedUI) {
                lastMoveTimeUI = now;
            }
            prevData = data;

            bool showGiant = (activeAxis != ' ' && (now - lastMoveTimeUI < 1000));

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
    }

    // --- Synchronized Light Sleep ---
    // Calculate if we can "nap" between heartbeats
    if (!isHopping && screenOn) { // Only sleep if not hopping and screen is on
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
        delay(50); // Standard delay while hopping or screen off to keep UI responsive
    }
}

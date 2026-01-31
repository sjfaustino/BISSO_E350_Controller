#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>         
#include <Preferences.h>      
#include "telemetry_packet.h" 
#include "logos.h"
#include "hal_tdisplay.h"

HAL_TDisplay* hal = new HAL_TDisplay();

// --- Configuration ---
#define VERSION_STR          "v1.0.1"
#define HOP_INTERVAL_MS      350   
#define DATA_TIMEOUT_MS      5000  
#define HEARTBEAT_MS         100   
#define SLEEP_GUARD_MS       15    
#define MAX_CHANNELS         13
#define SCREEN_TIMEOUT_MS    120000 
#define DEEP_SLEEP_TIMEOUT_MS 300000 
#define DEEP_SLEEP_WAKE_MS   300000 
#define IDLE_MOVE_THRESHOLD  0.05f  
#define STEALTH_TIMEOUT_MS   5000

// Data state
TelemetryPacket data;
TelemetryPacket prevData;
bool dataReceived = false;
uint32_t lastPacketTime = 0;
uint8_t currentChannel = 1;
bool isHopping = true;
uint32_t lastHopTime = 0;
int consecutivePackets = 0; // Require multiple packets to lock

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

int8_t lastRssi = -100;
uint32_t packetsReceivedThisSecond = 0;
uint32_t lastHealthCheck = 0;

// Callback when data is received (Legacy v2.x format)
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(TelemetryPacket)) {
        TelemetryPacket* pkt = (TelemetryPacket*)incomingData;
        
        // Verify protocol signature to prevent phantom locks
        if (pkt->signature != 0x42495353) return;

        packetsReceivedThisSecond++;

        // Assisted Locking: If the controller is on a different channel than we are currently 
        // tuning to (due to radio bleed/interference), force sync immediately.
        if (pkt->channel > 0 && pkt->channel <= 13 && pkt->channel != currentChannel) {
            Serial.printf(">>> Channel Mismatch! Controller is on %d, we were on %d. Syncing...\n", pkt->channel, currentChannel);
            currentChannel = pkt->channel;
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            prefs.putUChar("last_chan", currentChannel);
        }

        memcpy(&data, incomingData, sizeof(data));
        dataReceived = true;
        lastPacketTime = millis();
        
        if (stealthMode) {
            stealthMode = false;
            hal->setScreenOn(true);
            Serial.println("Machine detected! Exiting stealth mode...");
        }

        if (isHopping) {
            consecutivePackets++;
            if (consecutivePackets >= 3) {
                isHopping = false;
                prefs.putUChar("last_chan", currentChannel);
                Serial.printf(">>> Strong signal verified! Locking onto Channel %d\n", currentChannel);
            }
        } else {
            consecutivePackets = 10; // Keep state high while connected
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // HAL Initialization
    hal->init();
    stealthMode = hal->isStealthWake();

#ifdef SIMULATION_MODE
    stealthMode = false; // Simulation never starts in stealth
    isHopping = false;   // Start showing data immediately
    Serial.println("SIMULATION MODE ACTIVE");
#endif

    sessionStartTime = millis();

    Serial.printf("\n--- BISSO E350 Remote DRO %s starting ---\n", VERSION_STR);

    // Initialize Preferences
    prefs.begin("dro_cfg", false);
    currentChannel = prefs.getUChar("last_chan", 1);
    if (currentChannel < 1 || currentChannel > 13) currentChannel = 1;

    if (!stealthMode) {
        hal->showSplash(VERSION_STR, hal->getSystemTemp());
    } else {
        hal->setScreenOn(false);
    }
    
    // ESP-NOW Initial Setting - MAX POWER for strongest signal
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // Highest stable setting
    esp_wifi_set_ps(WIFI_PS_NONE);       // Disable all power saving
    
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[%s] Starting search on channel %d (System: %.1fC)\n", VERSION_STR, currentChannel, hal->getSystemTemp());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Init Failed");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);
    
    lastPacketTime = millis();
    lastMoveTimeStrict = millis();
}

void loop() {
    uint32_t now = millis();
    hal->update();

    // --- Signal Intensity Calculation (Packets Per Second) ---
    if (now - lastHealthCheck > 1000) {
        // Broadcast is 10Hz. We show raw "truth" as requested.
        // 9-10 pkts = 4 bars (-50dBm) - Solid link
        // 6-8 pkts = 3 bars (-65dBm) - Moderate loss
        // 3-5 pkts = 2 bars (-80dBm) - Heavy loss
        // 1-2 pkts = 1 bar (-90dBm) - Near disconnect
        // 0 pkts = 0 bars (-100dBm) - Offline
        if (packetsReceivedThisSecond >= 9) lastRssi = -50;
        else if (packetsReceivedThisSecond >= 6) lastRssi = -65;
        else if (packetsReceivedThisSecond >= 3) lastRssi = -80;
        else if (packetsReceivedThisSecond >= 1) lastRssi = -90;
        else lastRssi = -100;

        packetsReceivedThisSecond = 0;
        lastHealthCheck = now;
    }

#ifdef SIMULATION_MODE
    // Generate Fake Data
    static uint32_t lastSimTime = 0;
    if (now - lastSimTime > 100) { // 10Hz Update
        float t = now / 1000.0f;
        data.x = 1800.0f + 1750.0f * sin(t * 0.5f); // Cycle between 50 and 3550
        data.y = -25.0f + 10.0f * cos(t * 0.8f);
        data.z = 10.5f + 2.0f * sin(t * 1.2f);
        data.status = ( (now / 5000) % 4 ); // Cycle through READY, MOVING, ALARM, E-STOP
        
        dataReceived = true;
        lastPacketTime = now;
        isHopping = false;
        lastSimTime = now;
    }
#endif

    // --- Deep Sleep Logic ---
    if (now - lastPacketTime > DEEP_SLEEP_TIMEOUT_MS) {
        hal->enterDeepSleep(DEEP_SLEEP_WAKE_MS);
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
            hal->setScreenOn(true);
            Serial.println("Movement detected - Screen ON");
        }
    }

    // --- Screen Timeout Logic ---
    if (screenOn && (now - lastMoveTimeStrict > SCREEN_TIMEOUT_MS)) {
        screenOn = false;
        hal->setScreenOn(false);
        Serial.println("Idle timeout - Screen OFF");
    }

    // --- Channel Hopping State Machine ---
    if (now - lastPacketTime > DATA_TIMEOUT_MS) {
        lastRssi = -100; // Reset signal strength on timeout
        if (!isHopping) {
            isHopping = true;
            lastHopTime = now;
            consecutivePackets = 0;
            Serial.println("Connection lost. Resuming channel hop...");
        }

        if (now - lastHopTime > HOP_INTERVAL_MS) {
            currentChannel++;
            if (currentChannel > MAX_CHANNELS) {
                currentChannel = 1;
                Serial.printf("[%s] Still searching... Full sweep done. System Temp: %.1fC\n", VERSION_STR, hal->getSystemTemp());
            }
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            lastHopTime = now;
        }

        // Stealth Sniffing Timeout
        if (stealthMode && (now - sessionStartTime > STEALTH_TIMEOUT_MS)) {
            Serial.println("Stealth check complete - no controller. Sleeping.");
            hal->enterDeepSleep(DEEP_SLEEP_WAKE_MS);
        }
    }

    // --- Rendering (Only if screen is on) ---
    if (screenOn) {
        static uint32_t lastRenderTime = 0;
        if (now - lastRenderTime > 66) { // ~15 FPS Max for UI
            if (isHopping) {
                hal->drawSearching(currentChannel, hal->getSystemTemp(), false, lastRssi);
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

#ifdef SIMULATION_MODE
                // In simulation, force the active axis to cycle every 3 seconds so we see all Giant views
                int axisCycle = (now / 3000) % 3;
                if (axisCycle == 0) activeAxis = 'X';
                else if (axisCycle == 1) activeAxis = 'Y';
                else activeAxis = 'Z';
                lastMoveTimeUI = now; 
#endif

                bool showGiant = (activeAxis != ' ' && (now - lastMoveTimeUI < 1000));

                if (showGiant) {
                    float val = 0;
                    if (activeAxis == 'X') val = data.x;
                    else if (activeAxis == 'Y') val = data.y;
                    else if (activeAxis == 'Z') val = data.z;
                    hal->drawGiantDRO(activeAxis, val, val >= 0);
                } else {
                    hal->drawActiveDRO(data, currentChannel, lastRssi);
                }
            }
            lastRenderTime = now;
        }
    }

    // --- Nap mode disabled for debugging and better reliability ---
    /*
    if (!isHopping && screenOn) {
        ...
    } else {
        delay(50); 
    }
    */
    delay(10); // Standard loop delay
}

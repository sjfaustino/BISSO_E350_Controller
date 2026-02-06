#ifndef HAL_INTERFACE_H
#define HAL_INTERFACE_H

#include <Arduino.h>
#include "telemetry_packet.h"

class RemoteDRO_HAL {
public:
    virtual ~RemoteDRO_HAL() {}

    // --- Lifecycle ---
    virtual void init() = 0;
    virtual void update() = 0; // Periodic update for LED/etc.

    // --- Display ---
    virtual void setScreenOn(bool on) = 0;
    virtual void showSplash(const char* version, float temp) = 0;
    virtual void drawSearching(uint8_t channel, float temp, bool fullSweep) = 0;
    virtual void drawActiveDRO(const TelemetryPacket& data, uint8_t channel) = 0;
    virtual void drawGiantDRO(char axis, float value, bool positive) = 0;

    // --- Power Management ---
    virtual void enterDeepSleep(uint32_t wakeAfterMs) = 0;
    virtual void setupModemSleep() = 0;
    virtual void enterLightSleep(uint32_t durationMs) = 0;
    
    // --- System Info ---
    virtual float getSystemTemp() = 0;
    virtual bool isWakeRequested() = 0; // Check manual wake buttons
    virtual bool isStealthWake() = 0;   // Check if we woke by timer
};

#endif

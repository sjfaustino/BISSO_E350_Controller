#ifndef HAL_TDISPLAY_H
#define HAL_TDISPLAY_H

#include <TFT_eSPI.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include "telemetry_packet.h"

class HAL_TDisplay {
public:
    HAL_TDisplay();
    void init();
    void update();
    void setScreenOn(bool on);
    void showSplash(const char* version, float temp);
    void drawSearching(uint8_t channel, float temp, bool fullSweep);
    void drawActiveDRO(const TelemetryPacket& data, uint8_t channel);
    void drawGiantDRO(char axis, float value, bool positive);
    void enterDeepSleep(uint32_t wakeAfterMs);
    void setupModemSleep();
    void enterLightSleep(uint32_t durationMs);
    float getSystemTemp();
    bool isWakeRequested();
    bool isStealthWake();

private:
    TFT_eSPI tft;
    void drawArrow(char axis, bool positive, int x, int y, int size);
};

#endif

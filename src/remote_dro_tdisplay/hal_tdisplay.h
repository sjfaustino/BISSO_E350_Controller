#ifndef HAL_TDISPLAY_H
#define HAL_TDISPLAY_H

#include <TFT_eSPI.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include "telemetry_packet.h"

enum UIState {
    UI_STATE_BOOT,
    UI_STATE_SEARCHING,
    UI_STATE_ACTIVE,
    UI_STATE_GIANT
};

class HAL_TDisplay {
public:
    HAL_TDisplay();
    void init();
    void update();
    void setScreenOn(bool on);
    void showSplash(const char* version, float temp);
    void drawSearching(uint8_t channel, float temp, bool fullSweep, int8_t rssi);
    void drawActiveDRO(const TelemetryPacket& data, uint8_t channel, int8_t rssi);
    void drawGiantDRO(char axis, float value, bool positive);
    void drawSignalIcon(int x, int y, int8_t rssi);
    void enterDeepSleep(uint32_t wakeAfterMs);
    void setupModemSleep();
    void enterLightSleep(uint32_t durationMs);
    float getSystemTemp();
    bool isWakeRequested();
    bool isStealthWake();

private:
    TFT_eSPI tft;
    UIState _lastState = UI_STATE_BOOT;
    uint32_t _lastStatus = 99;
    uint8_t _lastChannel = 0;
    int8_t _lastRssi = 0;
    char _lastAxis = ' ';
    float _lx=0, _ly=0, _lz=0;

    void drawArrow(char axis, bool positive, int x, int y, int size);
};

#endif

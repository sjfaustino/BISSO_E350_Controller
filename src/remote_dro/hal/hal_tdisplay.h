#ifndef HAL_TDISPLAY_H
#define HAL_TDISPLAY_H

#include "hal_interface.h"
#include <TFT_eSPI.h>
#include <esp_pm.h>
#include <esp_sleep.h>

class HAL_TDisplay : public RemoteDRO_HAL {
public:
    HAL_TDisplay();
    void init() override;
    void update() override;
    void setScreenOn(bool on) override;
    void showSplash(const char* version, float temp) override;
    void drawSearching(uint8_t channel, float temp, bool fullSweep) override;
    void drawActiveDRO(const TelemetryPacket& data, uint8_t channel) override;
    void drawGiantDRO(char axis, float value, bool positive) override;
    void enterDeepSleep(uint32_t wakeAfterMs) override;
    void setupModemSleep() override;
    void enterLightSleep(uint32_t durationMs) override;
    float getSystemTemp() override;
    bool isWakeRequested() override;
    bool isStealthWake() override;

private:
    TFT_eSPI tft;
    void drawArrow(char axis, bool positive, int x, int y, int size);
};

#endif

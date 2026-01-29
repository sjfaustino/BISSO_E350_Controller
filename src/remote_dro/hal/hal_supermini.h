#ifndef HAL_SUPERMINI_H
#define HAL_SUPERMINI_H

#include "hal_interface.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include "driver/temp_sensor.h"

class HAL_SuperMini : public RemoteDRO_HAL {
public:
    HAL_SuperMini();
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
    Adafruit_SSD1306 display;
    void drawArrow(char axis, bool positive);
};

#endif

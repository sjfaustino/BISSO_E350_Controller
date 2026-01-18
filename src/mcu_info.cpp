/**
 * @file mcu_info.cpp
 * @brief Dynamic MCU Information Implementation
 */

#include "mcu_info.h"
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

const char* mcuGetModelName() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    static char model_buf[64];
    const char* base_model = "Unknown";
    const char* sub_model = "";

    switch (chip_info.model) {
        case CHIP_ESP32:
            base_model = "ESP32";
            // Heuristic for specific package/variant
            if (chip_info.cores == 2) {
                // Most common dual core is D0WD
                sub_model = "-D0WDQ6"; 
            } else {
                sub_model = "-S0WD";
            }
            break;
        case CHIP_ESP32S2: base_model = "ESP32-S2"; break;
        case CHIP_ESP32S3: base_model = "ESP32-S3"; break;
        case CHIP_ESP32C3: base_model = "ESP32-C3"; break;
#ifdef CHIP_ESP32H2
        case CHIP_ESP32H2: base_model = "ESP32-H2"; break;
#endif
#ifdef CHIP_ESP32C2
        case CHIP_ESP32C2: base_model = "ESP32-C2"; break;
#endif
        default: break;
    }

    snprintf(model_buf, sizeof(model_buf), "%s%s", base_model, sub_model);
    return model_buf;
}

const char* mcuGetRevisionString(char* buffer, size_t size) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // revision handling based on ESP-IDF version
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    uint32_t major = chip_info.revision / 100;
    uint32_t minor = chip_info.revision % 100;
    snprintf(buffer, size, "v%u.%u", (unsigned int)major, (unsigned int)minor);
#else
    snprintf(buffer, size, "v%u", (unsigned int)chip_info.revision);
#endif
    return buffer;
}

bool mcuHasPsram() {
    return psramFound();
}

uint32_t mcuGetPsramSize() {
#ifdef BOARD_HAS_PSRAM
    return ESP.getPsramSize();
#else
    return psramFound() ? ESP.getPsramSize() : 0;
#endif
}

uint32_t mcuGetFlashSize() {
    return ESP.getFlashChipSize();
}

uint8_t mcuGetCoreCount() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return chip_info.cores;
}

uint32_t mcuGetCpuFreqMHz() {
    return ESP.getCpuFreqMHz();
}

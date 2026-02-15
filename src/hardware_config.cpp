/**
 * @file hardware_config.cpp
 * @brief Hardware Abstraction Layer - Virtual Pin Mapping for KC868-A16
 * @project PosiPro
 * @author Sergio Faustino
 */

#include "hardware_config.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "string_safety.h"
#include <string.h>
#include <stdio.h>

// Global Board Identity
const BoardType BOARD = detectBoard();

BoardType detectBoard() {
#if defined(BOARD_KC868_A16_V31)
    return BoardType::A16_V31;
#elif defined(BOARD_KC868_A16_V16)
    return BoardType::A16_V16;
#else
    return BoardType::UNKNOWN;
#endif
}

const char* getBoardName() {
#if defined(BOARD_KC868_A16_V31)
    return "KC868-A16 v3.1 (S3-WROOM-1U)";
#elif defined(BOARD_KC868_A16_V16)
    return "KC868-A16 v1.6 (WROOM-32E)";
#else
    return "Generic ESP32";
#endif
}

const PinInfo* getPinInfo(int16_t gpio) {
    for (size_t i = 0; i < PIN_COUNT; i++) {
        if (pinDatabase[i].gpio == gpio) {
            return &pinDatabase[i];
        }
    }
    return nullptr;
}

int16_t getPin(const char* key) {
    if (!key) return -1;

    // 1. Check NVS for a user override
    // Stores virtual pin mappings using short key (e.g. "o_axis_x" -> 16)
    const char* nvs_key = nullptr;
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        if (strcmp(signalDefinitions[i].key, key) == 0) {
            nvs_key = signalDefinitions[i].nvs_key;
            break;
        }
    }

    if (nvs_key) {
        int32_t stored_val = configGetInt(nvs_key, -1);
        if (stored_val != -1) {
            return (int16_t)stored_val;
        }
    }

    // 2. Fallback to Defaults from header
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        if (strcmp(signalDefinitions[i].key, key) == 0) {
            return signalDefinitions[i].default_gpio;
        }
    }
    return -1;
}

const char* checkPinConflict(int16_t gpio, const char* currentKey) {
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        const char* checkKey = signalDefinitions[i].key;
        if (currentKey && strcmp(checkKey, currentKey) == 0) continue;

        int16_t assignedPin = getPin(checkKey);
        if (assignedPin == gpio) {
            return checkKey; 
        }
    }
    return nullptr; 
}

bool setPin(const char* key, int16_t gpio, bool skip_save) {
    // 1. Validate Key
    bool keyExists = false;
    const char* signalType = "unknown";
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        if (strcmp(signalDefinitions[i].key, key) == 0) {
            keyExists = true;
            signalType = signalDefinitions[i].type;
            break;
        }
    }
    if (!keyExists) {
        logError("[HAL] Unknown signal: %s", key);
        return false;
    }

    // 2. Validate Virtual Pin
    const PinInfo* info = (gpio == -1) ? nullptr : getPinInfo(gpio);
    if (gpio != -1 && !info) {
        logError("[HAL] Invalid Virtual Pin ID %d", gpio);
        return false;
    }

    // 3. I/O Type Check (Input vs Output)
    if (gpio != -1) {
        // Virtual Pin Scheme for KC868-A16:
        // Pins 100-115 (X1-X16) are PHYSICAL INPUTS (Opto via I2C)
        // Pins 116-131 (Y1-Y16) are PHYSICAL OUTPUTS (Relay via I2C)
        // CH1-CH4 are analog inputs using GPIO 34-39
        // Direct GPIO (13, 14, 16, 32, 33) for RS485/WJ66
        bool isVirtualInputPin = (gpio >= 100 && gpio <= 115);
        bool isVirtualOutputPin = (gpio >= 116 && gpio <= 131);
        
        bool isInputSignal = (strcmp(signalType, "input") == 0);
        bool isOutputSignal = (strcmp(signalType, "output") == 0);

        // Only validate I2C virtual pins for I/O mismatch
        if (isVirtualInputPin && isOutputSignal) {
            logError("[HAL] Hardware Mismatch: Virtual Pin %d (X%d) is PHYSICALLY AN INPUT", gpio, gpio - 99);
            return false;
        }
        if (isVirtualOutputPin && isInputSignal) {
            logError("[HAL] Hardware Mismatch: Virtual Pin %d (Y%d) is PHYSICALLY AN OUTPUT", gpio, gpio - 115);
            return false;
        }

        // 4. Check Logical Conflicts
        const char* conflict = checkPinConflict(gpio, key);
        if (conflict) {
            logError("[HAL] Conflict: Pin %d used by %s", gpio, conflict);
            return false;
        }
    }

    // 5. Save Mapping using short NVS key
    const char* nvs_key = nullptr;
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        if (strcmp(signalDefinitions[i].key, key) == 0) {
            nvs_key = signalDefinitions[i].nvs_key;
            break;
        }
    }

    if (nvs_key) {
        configSetInt(nvs_key, gpio);
        if (!skip_save) {
            configUnifiedSave();
        }
    } else {
        logError("[HAL] Save failed: No NVS key for %s", key);
        return false;
    }

    if (gpio == -1) {
        logInfo("[HAL] [OK] Unmapped %s", key);
    } else {
        logInfo("[HAL] [OK] Mapped %s -> Virtual Pin %d (%s)", key, gpio, info->silk);
    }

    return true;
}

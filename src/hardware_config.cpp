/**
 * @file hardware_config.cpp
 * @brief Hardware Abstraction Layer - Virtual Pin Mapping for KC868-A16
 * @project Gemini v1.0.0
 * @author Sergio Faustino
 */

#include "hardware_config.h"
#include "config_unified.h"
#include "serial_logger.h"
#include "string_safety.h"
#include <string.h>
#include <stdio.h>

// Global Board Identity
const BoardType BOARD = detectBoard();

BoardType detectBoard() {
    // Strictly enforce KC868-A16 for Gemini v1.0.0
    return BoardType::A16;
}

const PinInfo* getPinInfo(int8_t gpio) {
    for (size_t i = 0; i < PIN_COUNT; i++) {
        if (pinDatabase[i].gpio == gpio) {
            return &pinDatabase[i];
        }
    }
    return nullptr;
}

int8_t getPin(const char* key) {
    if (!key) return -1;

    // 1. Check NVS for a user override
    // Stores virtual pin mappings (e.g. "pin_output_axis_x" -> 16)
    char nvs_key[40];
    if (safe_snprintf(nvs_key, sizeof(nvs_key), "pin_%s", key) > 0) {
        int32_t stored_val = configGetInt(nvs_key, -1);
        if (stored_val != -1) {
            return (int8_t)stored_val;
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

const char* checkPinConflict(int8_t gpio, const char* currentKey) {
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        const char* checkKey = signalDefinitions[i].key;
        if (currentKey && strcmp(checkKey, currentKey) == 0) continue;

        int8_t assignedPin = getPin(checkKey);
        if (assignedPin == gpio) {
            return checkKey; 
        }
    }
    return nullptr; 
}

bool setPin(const char* key, int8_t gpio) {
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

    // 2. Validate Virtual Pin (0-31 are Virtual I2C Pins on A16)
    const PinInfo* info = getPinInfo(gpio);
    if (!info) {
        logError("[HAL] Invalid Virtual Pin ID %d", gpio);
        return false;
    }

    // 3. I/O Type Check (Input vs Output)
    // On KC868-A16:
    // Pins 0-15  (X1-X16) are PHYSICAL INPUTS (Opto)
    // Pins 16-31 (Y1-Y16) are PHYSICAL OUTPUTS (Relay/Mosfet)
    bool isInputPin = (gpio >= 0 && gpio <= 15);
    bool isOutputPin = (gpio >= 16 && gpio <= 31);
    
    bool isInputSignal = (strcmp(signalType, "input") == 0);
    bool isOutputSignal = (strcmp(signalType, "output") == 0);

    if (isInputPin && isOutputSignal) {
        logError("[HAL] Hard Hardware Mismatch: Pin %d is PHYSICALLY AN INPUT", gpio);
        return false;
    }
    if (isOutputPin && isInputSignal) {
        logError("[HAL] Hard Hardware Mismatch: Pin %d is PHYSICALLY AN OUTPUT", gpio);
        return false;
    }

    // 4. Check Logical Conflicts
    const char* conflict = checkPinConflict(gpio, key);
    if (conflict) {
        logError("[HAL] Conflict: Pin %d used by %s", gpio, conflict);
        return false;
    }

    // 5. Save Mapping
    char nvs_key[40];
    safe_snprintf(nvs_key, sizeof(nvs_key), "pin_%s", key);
    configSetInt(nvs_key, gpio);
    configUnifiedSave();

    logInfo("[HAL] [OK] Mapped %s -> Virtual Pin %d (%s)", key, gpio, info->silk);
    return true;
}
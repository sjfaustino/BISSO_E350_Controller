/**
 * @file dac_interface.cpp
 * @brief Implementation of PLC Analog Output Card (DAC) driver
 */

#include "dac_interface.h"
#include "config_keys.h"
#include "config_unified.h"
#include "serial_logger.h"
#include "task_manager.h"
#include <Wire.h>

static uint8_t g_dac_address = 0x60;
static bool g_dac_initialized = false;
static float g_max_freq = 50.0f; // Default 50Hz for 10V

bool dacInit(void) {
    if (!dacIsAnalogEnabled()) {
        logInfo("[DAC] Analog control disabled in config");
        return true;
    }

    g_dac_address = (uint8_t)configGetInt(KEY_DAC_ADDR, 0x60);
    g_max_freq = configGetFloat("vfd_max_hz", 50.0f);

    // Initial hardware check
    taskLockMutex(taskGetI2cPlcMutex(), 100);
    Wire.beginTransmission(g_dac_address);
    if (Wire.endTransmission() == 0) {
        logInfo("[DAC] Analog Card detected at 0x%02X (Max Freq: %.0f Hz)", g_dac_address, g_max_freq);
        g_dac_initialized = true;
    } else {
        logError("[DAC] Analog Card not found at 0x%02X", g_dac_address);
        g_dac_initialized = false;
    }
    taskUnlockMutex(taskGetI2cPlcMutex());

    if (g_dac_initialized) {
        dacEmergencyStop(); // Start at 0V
    }

    return g_dac_initialized;
}

bool dacSetFrequency(uint8_t vfd_id, float frequency_hz) {
    if (!g_dac_initialized) return false;

    // Clamp frequency
    if (frequency_hz < 0.0f) frequency_hz = 0.0f;
    if (frequency_hz > g_max_freq) frequency_hz = g_max_freq;

    // Map frequency (0 - MaxHz) to voltage (0 - 10V)
    float voltage = (frequency_hz / g_max_freq) * DAC_MAX_VOLTAGE;
    
    uint8_t channel = (vfd_id == 0) ? DAC_CH_VFD1_X : DAC_CH_VFD2_YZA;
    return dacSetVoltage(channel, voltage);
}

bool dacSetVoltage(uint8_t channel, float voltage) {
    if (!g_dac_initialized || channel > 3) return false;

    // Clamp voltage
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > DAC_MAX_VOLTAGE) voltage = DAC_MAX_VOLTAGE;

    // Map 0-10V to 12-bit DAC value (0-4095)
    // NOTE: Hard wiring usually assumes ESP32's 3.3V DAC or an I2C DAC.
    // If the DAC is 12-bit and 10V is regulated by an Op-Amp from the DAC's full scale:
    uint16_t raw_value = (uint16_t)((voltage / DAC_MAX_VOLTAGE) * DAC_MAX_RAW_VALUE);

    // MCP4728 Fast Write Command (Channel, Value)
    // Sequence: [Address | W] [Channel | Value_High] [Value_Low]
    taskLockMutex(taskGetI2cPlcMutex(), 50);
    Wire.beginTransmission(g_dac_address);
    // Control byte for MCP4728: 0100 0xxx (Write Multi Canal)
    // For simplicity, we use Write Multi-CH starting at current:
    // This is driver-specific logic. 
    uint8_t header = 0x40 | (channel << 1); 
    Wire.write(header);
    Wire.write((raw_value >> 8) & 0x0F);
    Wire.write(raw_value & 0xFF);
    bool ok = (Wire.endTransmission() == 0);
    taskUnlockMutex(taskGetI2cPlcMutex());

    if (!ok) {
        logWarning("[DAC] Write failed to ch %d", channel);
    }
    return ok;
}

void dacEmergencyStop(void) {
    if (!g_dac_initialized) return;
    for (int i = 0; i < 4; i++) {
        dacSetVoltage(i, 0.0f);
    }
    logInfo("[DAC] Emergency stop - All outputs 0V");
}

bool dacIsAnalogEnabled(void) {
    return configGetInt(KEY_VFD_ANALOG_EN, 0) != 0;
}

// calibration.cpp

#include "calibration.h"
#include "motion.h"
#include "hardware_config.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include <Preferences.h>
#include "serial_logger.h"
#include "system_utilities.h" // <-- NEW: Centralized Axis Utilities
#include <string.h>
#include <stdio.h> // For snprintf
#include <ctype.h> // Required for tolower()

// Global calibration storage (Defined here)
MachineCalibration machineCal;

// Helper to get a pointer to the AxisCalibration data for a given index (0=X, 1=Y, 2=Z, 3=A)
static AxisCalibration* getAxisCalPtr(uint8_t axis) {
    if (axis < 4) return &machineCal.axes[axis];
    return NULL;
}

// Helper to generate the NVS key string based on axis and suffix (e.g., "x_ppm", "a_slow")
static void generateNvsKey(char* key_buffer, uint8_t axis, const char* suffix) {
    char axis_char_upper = axisIndexToChar(axis); // <-- USE CENTRALIZED UTILITY
    
    if (axis_char_upper != '?') {
        char axis_char_lower = tolower(axis_char_upper);
        snprintf(key_buffer, 10, "%c_%s", axis_char_lower, suffix);
    }
}

void loadAllCalibration() {
    Preferences p;
    p.begin("calib", true);

    for (uint8_t i = 0; i < 4; ++i) {
        AxisCalibration* cal = getAxisCalPtr(i);
        if (!cal) continue;

        char key[10];

        // General linear parameters (PPM, Backlash, Pitch)
        generateNvsKey(key, i, "ppm"); cal->pulses_per_mm = p.getFloat(key, 0.0f);
        generateNvsKey(key, i, "back"); cal->backlash_mm = p.getFloat(key, 0.0f);
        generateNvsKey(key, i, "pitch"); cal->pitch_error = p.getFloat(key, 1.0f);

        // Angular parameters (PPD - only needed for A-axis, but loaded universally)
        generateNvsKey(key, i, "ppd"); cal->pulses_per_degree = p.getFloat(key, 0.0f); 

        // Speed profiles
        generateNvsKey(key, i, "slow"); cal->speed_slow_mm_min = p.getFloat(key, 300.0f);
        generateNvsKey(key, i, "med"); cal->speed_med_mm_min = p.getFloat(key, 900.0f);
        generateNvsKey(key, i, "fast"); cal->speed_fast_mm_min = p.getFloat(key, 2400.0f);
    }
    p.end();
}

void saveAllCalibration() {
    Preferences p;
    p.begin("calib", false);

    for (uint8_t i = 0; i < 4; ++i) {
        AxisCalibration* cal = getAxisCalPtr(i);
        if (!cal) continue;

        char key[10];

        // General linear parameters (PPM, Backlash, Pitch)
        generateNvsKey(key, i, "ppm"); p.putFloat(key, cal->pulses_per_mm);
        generateNvsKey(key, i, "back"); p.putFloat(key, cal->backlash_mm);
        generateNvsKey(key, i, "pitch"); p.putFloat(key, cal->pitch_error);

        // Angular parameters (PPD)
        generateNvsKey(key, i, "ppd"); p.putFloat(key, cal->pulses_per_degree);

        // Speed profiles
        generateNvsKey(key, i, "slow"); p.putFloat(key, cal->speed_slow_mm_min);
        generateNvsKey(key, i, "med"); p.putFloat(key, cal->speed_med_mm_min);
        generateNvsKey(key, i, "fast"); p.putFloat(key, cal->speed_fast_mm_min);
    }
    p.end();
}

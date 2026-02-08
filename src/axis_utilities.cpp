/**
 * @file axis_utilities.cpp
 * @brief Axis-related utility functions implementation
 * @project BISSO E350 Controller
 */

#include "axis_utilities.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "hardware_config.h"
#include <math.h>

char axisIndexToChar(uint8_t index) {
    switch(index) {
        case 0: return 'X';
        case 1: return 'Y';
        case 2: return 'Z';
        case 3: return 'A';
        default: return '?'; 
    }
}

uint8_t axisCharToIndex(char* arg) {
    if (arg == NULL || strlen(arg) != 1) return 255;
    char axis_letter = toupper(arg[0]);
    if (axis_letter == 'X') return 0;
    if (axis_letter == 'Y') return 1;
    if (axis_letter == 'Z') return 2;
    if (axis_letter == 'A') return 3;
    return 255;
}

float getAxisScale(uint8_t axis) {
    if (axis >= 4) return 0.0f;
    // Axis 3 (A) is angular (degree), others are linear (mm)
    return (axis == 3) ? machineCal.axes[3].pulses_per_degree : machineCal.axes[axis].pulses_per_mm;
}

float countsToMM(uint8_t axis, int32_t counts) {
    float scale = getAxisScale(axis);
    if (scale <= 0.0001f) return 0.0f;
    return (float)counts / scale;
}

int32_t mmToCounts(uint8_t axis, float mm) {
    float scale = getAxisScale(axis);
    return (int32_t)roundf(mm * scale);
}

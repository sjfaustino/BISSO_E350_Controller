/**
 * @file axis_utilities.h
 * @brief Axis-related utility functions (PPM, indexing, conversion)
 * @project BISSO E350 Controller
 */

#ifndef AXIS_UTILITIES_H
#define AXIS_UTILITIES_H

#include <stdint.h>

/**
 * @brief Converts a 0-indexed axis number to its corresponding character label.
 * @param index The 0-indexed axis (0=X, 1=Y, 2=Z, 3=A).
 * @return The character representation of the axis ('X', 'Y', 'Z', 'A'), or '?' if out of range.
 */
char axisIndexToChar(uint8_t index);

/**
 * @brief Converts a single-character axis string ('X', 'Y', 'Z', 'A') to its 
 * corresponding 0-3 index.
 * @param arg Pointer to the axis string (e.g., "X").
 * @return Axis index (0-3) or 255 if invalid.
 */
uint8_t axisCharToIndex(char* arg);

/**
 * @brief Convert encoder counts to millimeters based on axis calibration.
 */
float countsToMM(uint8_t axis, int32_t counts);

/**
 * @brief Convert distance in mm to encoder counts.
 */
int32_t mmToCounts(uint8_t axis, float mm);

/**
 * @brief Get the active scale (PPM/PPD) for an axis.
 */
float getAxisScale(uint8_t axis);

#endif // AXIS_UTILITIES_H

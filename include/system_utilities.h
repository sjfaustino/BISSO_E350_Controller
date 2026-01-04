#ifndef SYSTEM_UTILITIES_H
#define SYSTEM_UTILITIES_H

#include <stdint.h>

// --- Axis Mapping Definitions (Centralized) ---

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
uint8_t axisCharToIndex(char* arg); // Renamed from parse_axis_arg for clarity

// --- Add other system utilities here as needed ---

#endif // SYSTEM_UTILITIES_H

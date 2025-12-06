#ifndef INPUT_VALIDATION_H
#define INPUT_VALIDATION_H

#include <Arduino.h>

bool validateIntRange(int32_t value, int32_t min, int32_t max);
bool validateFloatRange(float value, float min, float max);
bool validateStringLength(const char* str, size_t max_len);
bool validateAxisNumber(uint8_t axis);
bool validateMotionPosition(uint8_t axis, int32_t position);
bool validateMotionVelocity(int32_t velocity);

size_t sanitizeStringInput(const char* input, char* output, size_t max_len);
bool parseAndValidateInt(const char* str, int32_t* value, int32_t min, int32_t max);
bool parseAndValidateFloat(const char* str, float* value, float min, float max);

// --- NEW: Axis Helper ---
uint8_t axisCharToIndex(const char* str);

#endif
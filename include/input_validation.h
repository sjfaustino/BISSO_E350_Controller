#ifndef INPUT_VALIDATION_H
#define INPUT_VALIDATION_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// INPUT VALIDATION FUNCTIONS
// ============================================================================

/**
 * @brief Validate integer is within range
 * @param value Value to validate
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @return true if value is within range, false otherwise
 */
bool validateIntRange(int32_t value, int32_t min, int32_t max);

/**
 * @brief Validate float is within range
 * @param value Value to validate
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @return true if value is within range, false otherwise
 */
bool validateFloatRange(float value, float min, float max);

/**
 * @brief Validate string length
 * @param str String to validate
 * @param max_len Maximum allowed length
 * @return true if string length <= max_len, false otherwise
 */
bool validateStringLength(const char* str, size_t max_len);

/**
 * @brief Validate axis number (0-3 for 4-axis system)
 * @param axis Axis number to validate
 * @return true if axis is valid (0-3), false otherwise
 */
bool validateAxisNumber(uint8_t axis);

/**
 * @brief Validate position is within safe limits
 * @param axis Axis number
 * @param position Position in encoder counts
 * @return true if position is safe, false otherwise
 */
bool validateMotionPosition(uint8_t axis, int32_t position);

/**
 * @brief Validate velocity is within safe limits
 * @param velocity Velocity value
 * @return true if velocity is safe, false otherwise
 */
bool validateMotionVelocity(int32_t velocity);

/**
 * @brief Sanitize string input - remove dangerous characters
 * @param input Input string
 * @param output Output buffer
 * @param max_len Maximum output length
 * @return Number of characters copied
 */
size_t sanitizeStringInput(const char* input, char* output, size_t max_len);

/**
 * @brief Parse and validate integer from string
 * @param str String to parse
 * @param value Pointer to store parsed value
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @return true if parse and validation successful, false otherwise
 */
bool parseAndValidateInt(const char* str, int32_t* value, int32_t min, int32_t max);

/**
 * @brief Parse and validate float from string
 * @param str String to parse
 * @param value Pointer to store parsed value
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @return true if parse and validation successful, false otherwise
 */
bool parseAndValidateFloat(const char* str, float* value, float min, float max);

#endif

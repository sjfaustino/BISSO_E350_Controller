#include "input_validation.h"
#include <string.h>
#include <stdlib.h> 
#include <ctype.h>

bool validateIntRange(int32_t value, int32_t min, int32_t max) {
  return (value >= min && value <= max);
}

bool validateFloatRange(float value, float min, float max) {
  return (value >= min && value <= max);
}

bool validateStringLength(const char* str, size_t max_len) {
  if (str == NULL) return false;
  return (strlen(str) <= max_len);
}

bool validateAxisNumber(uint8_t axis) {
  return (axis < 4);
}

bool validateMotionPosition(uint8_t axis, int32_t position) {
  if (!validateAxisNumber(axis)) return false;
  const int32_t POSITION_MIN = -10000000;
  const int32_t POSITION_MAX = 10000000;
  return (position >= POSITION_MIN && position <= POSITION_MAX);
}

bool validateMotionVelocity(int32_t velocity) {
  const int32_t VELOCITY_MIN = 100;
  const int32_t VELOCITY_MAX = 50000;
  return (velocity >= VELOCITY_MIN && velocity <= VELOCITY_MAX);
}

size_t sanitizeStringInput(const char* input, char* output, size_t max_len) {
  if (input == NULL || output == NULL || max_len == 0) return 0;
  size_t i = 0;
  size_t j = 0;
  while (input[i] != '\0' && j < max_len - 1) {
    char c = input[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == ' ' || c == '-' || c == '_' || c == '.' || c == ':') {
      output[j++] = c;
    }
    i++;
  }
  output[j] = '\0';
  return j;
}

bool parseAndValidateInt(const char* str, int32_t* value, int32_t min, int32_t max) {
  if (str == NULL || value == NULL) return false;
  if (strlen(str) > 20) return false;
  char* endptr = NULL;
  long parsed_value = strtol(str, &endptr, 10);
  if (*endptr != '\0') return false;
  if (parsed_value < min || parsed_value > max) return false;
  *value = (int32_t)parsed_value;
  return true;
}

bool parseAndValidateFloat(const char* str, float* value, float min, float max) {
  if (str == NULL || value == NULL) return false;
  if (strlen(str) > 20) return false;
  char* endptr = NULL;
  float parsed_value = strtof(str, &endptr);
  if (*endptr != '\0') return false;
  if (parsed_value < min || parsed_value > max) return false;
  *value = parsed_value;
  return true;
}

// --- NEW IMPLEMENTATION ---
uint8_t axisCharToIndex(const char* str) {
    if (!str || strlen(str) == 0) return 255;
    char c = toupper(str[0]);
    switch(c) {
        case 'X': return 0;
        case 'Y': return 1;
        case 'Z': return 2;
        case 'A': return 3;
        default: return 255;
    }
}

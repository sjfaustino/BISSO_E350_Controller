#include "input_validation.h"

// ============================================================================
// INPUT VALIDATION IMPLEMENTATION
// ============================================================================

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
  return (axis >= 0 && axis < 4);  // 4-axis system
}

bool validateMotionPosition(uint8_t axis, int32_t position) {
  if (!validateAxisNumber(axis)) return false;
  
  // Safe limits: ±10,000,000 encoder counts
  const int32_t POSITION_MIN = -10000000;
  const int32_t POSITION_MAX = 10000000;
  
  return (position >= POSITION_MIN && position <= POSITION_MAX);
}

bool validateMotionVelocity(int32_t velocity) {
  // Safe velocity limits: 100-50000 counts/sec
  const int32_t VELOCITY_MIN = 100;
  const int32_t VELOCITY_MAX = 50000;
  
  return (velocity >= VELOCITY_MIN && velocity <= VELOCITY_MAX);
}

size_t sanitizeStringInput(const char* input, char* output, size_t max_len) {
  if (input == NULL || output == NULL || max_len == 0) return 0;
  
  size_t i = 0;
  size_t j = 0;
  
  // Copy only safe characters (alphanumeric, space, common symbols)
  while (input[i] != '\0' && j < max_len - 1) {
    char c = input[i];
    
    // Allow: letters, digits, space, hyphen, underscore, dot, colon
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
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
  
  // Check string length
  if (strlen(str) > 20) return false;  // Prevent extremely long input
  
  // Try to parse
  char* endptr = NULL;
  long parsed_value = strtol(str, &endptr, 10);
  
  // Check if parse was successful (entire string consumed)
  if (*endptr != '\0') return false;
  
  // Check range
  if (parsed_value < min || parsed_value > max) return false;
  
  *value = (int32_t)parsed_value;
  return true;
}

bool parseAndValidateFloat(const char* str, float* value, float min, float max) {
  if (str == NULL || value == NULL) return false;
  
  // Check string length
  if (strlen(str) > 20) return false;  // Prevent extremely long input
  
  // Try to parse
  char* endptr = NULL;
  float parsed_value = strtof(str, &endptr);
  
  // Check if parse was successful (entire string consumed)
  if (*endptr != '\0') return false;
  
  // Check range
  if (parsed_value < min || parsed_value > max) return false;
  
  *value = parsed_value;
  return true;
}

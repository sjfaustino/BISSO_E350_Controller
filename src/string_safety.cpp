#include "string_safety.h"
#include "fault_logging.h"
#include "fault_logging.h"

// ============================================================================
// SAFE STRING FORMATTING IMPLEMENTATION
// ============================================================================

size_t safe_vsnprintf(char* buffer, size_t buffer_size, const char* format, va_list args) {
  if (!buffer || buffer_size == 0) {
    Serial.println("[STR_SAFETY] ERROR: Invalid buffer or size");
    return 0;
  }
  
  // Perform formatted print
  size_t result = vsnprintf(buffer, buffer_size, format, args);
  
  // Check for truncation
  if (result >= buffer_size) {
    buffer[buffer_size - 1] = '\0';
    Serial.print("[STR_SAFETY] WARNING: String truncation detected! ");
    Serial.print("Required: ");
    Serial.print(result + 1);
    Serial.print(", Available: ");
    Serial.println(buffer_size);
    
    faultLogWarning(FAULT_BOOT_FAILED, "String truncation in formatting");
  }
  
  return result;
}

size_t safe_snprintf(char* buffer, size_t buffer_size, const char* format, ...) {
  if (!buffer || buffer_size == 0) {
    Serial.println("[STR_SAFETY] ERROR: Invalid buffer or size");
    return 0;
  }
  
  va_list args;
  va_start(args, format);
  size_t result = safe_vsnprintf(buffer, buffer_size, format, args);
  va_end(args);
  
  return result;
}

bool safe_strcpy(char* dest, size_t dest_size, const char* src) {
  if (!dest || !src || dest_size == 0) {
    Serial.println("[STR_SAFETY] ERROR: Invalid parameters for strcpy");
    return false;
  }
  
  // Ensure null terminator space
  if (dest_size < 1) {
    return false;
  }
  
  size_t src_len = strlen(src);
  
  if (src_len >= dest_size) {
    // Truncation would occur
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    
    Serial.print("[STR_SAFETY] WARNING: String copy truncation! ");
    Serial.print("Source len: ");
    Serial.print(src_len);
    Serial.print(", Available: ");
    Serial.println(dest_size - 1);
    
    faultLogWarning(FAULT_BOOT_FAILED, "String copy truncation");
    return false;
  }
  
  // Safe to copy
  strcpy(dest, src);
  return true;
}

bool safe_strcat(char* dest, size_t dest_size, const char* src) {
  if (!dest || !src || dest_size == 0) {
    Serial.println("[STR_SAFETY] ERROR: Invalid parameters for strcat");
    return false;
  }
  
  size_t dest_len = strlen(dest);
  size_t src_len = strlen(src);
  
  // Check if concatenation would exceed buffer
  if (dest_len + src_len >= dest_size) {
    // Truncation would occur
    Serial.print("[STR_SAFETY] WARNING: String concat truncation! ");
    Serial.print("Dest len: ");
    Serial.print(dest_len);
    Serial.print(", Src len: ");
    Serial.print(src_len);
    Serial.print(", Available: ");
    Serial.println(dest_size - dest_len - 1);
    
    faultLogWarning(FAULT_BOOT_FAILED, "String concat truncation");
    return false;
  }
  
  // Safe to concatenate
  strcat(dest, src);
  return true;
}

bool safe_is_valid_string(const char* buffer, size_t max_size) {
  if (!buffer) {
    return false;
  }
  
  // Check for null terminator within max_size
  for (size_t i = 0; i < max_size; i++) {
    if (buffer[i] == '\0') {
      return true;  // Found null terminator
    }
  }
  
  Serial.print("[STR_SAFETY] ERROR: String not null-terminated within ");
  Serial.print(max_size);
  Serial.println(" bytes");
  
  return false;
}

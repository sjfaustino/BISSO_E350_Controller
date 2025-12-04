#include "string_safety.h"
#include "fault_logging.h"
#include <stdio.h>
#include <string.h>

size_t safe_vsnprintf(char* buffer, size_t buffer_size, const char* format, va_list args) {
  if (!buffer || buffer_size == 0) {
    Serial.println("[SAFETY] [ERR] Invalid buffer/size in safe_vsnprintf");
    return 0;
  }
  size_t result = vsnprintf(buffer, buffer_size, format, args);
  if (result >= buffer_size) {
    buffer[buffer_size - 1] = '\0';
    Serial.printf("[SAFETY] [WARN] String truncated (Req: %lu, Avail: %lu)\n", result + 1, buffer_size);
    faultLogWarning(FAULT_BOOT_FAILED, "String truncation");
  }
  return result;
}

size_t safe_snprintf(char* buffer, size_t buffer_size, const char* format, ...) {
  if (!buffer || buffer_size == 0) return 0;
  va_list args;
  va_start(args, format);
  size_t result = safe_vsnprintf(buffer, buffer_size, format, args);
  va_end(args);
  return result;
}

bool safe_strcpy(char* dest, size_t dest_size, const char* src) {
  if (!dest || !src || dest_size == 0) return false;
  if (dest_size < 1) return false;
  
  size_t src_len = strlen(src);
  if (src_len >= dest_size) {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    Serial.printf("[SAFETY] [WARN] Copy truncated (Src: %lu, Dest: %lu)\n", src_len, dest_size - 1);
    faultLogWarning(FAULT_BOOT_FAILED, "String copy truncation");
    return false;
  }
  strcpy(dest, src);
  return true;
}

bool safe_strcat(char* dest, size_t dest_size, const char* src) {
  if (!dest || !src || dest_size == 0) return false;
  size_t dest_len = strlen(dest);
  size_t src_len = strlen(src);
  
  if (dest_len + src_len >= dest_size) {
    Serial.printf("[SAFETY] [WARN] Concat truncated (Needed: %lu, Avail: %lu)\n", dest_len + src_len + 1, dest_size);
    faultLogWarning(FAULT_BOOT_FAILED, "String concat truncation");
    return false;
  }
  strcat(dest, src);
  return true;
}

bool safe_is_valid_string(const char* buffer, size_t max_size) {
  if (!buffer) return false;
  for (size_t i = 0; i < max_size; i++) {
    if (buffer[i] == '\0') return true;
  }
  Serial.printf("[SAFETY] [ERR] String not null-terminated in %lu bytes\n", max_size);
  return false;
}
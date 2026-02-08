#ifndef STRING_SAFETY_H
#define STRING_SAFETY_H

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// ============================================================================
// SAFE STRING FORMATTING
// ============================================================================

/**
 * Safe wrapper around snprintf that detects and reports truncation
 * 
 * @param buffer Destination buffer
 * @param buffer_size Size of destination buffer
 * @param format Printf-style format string
 * @return Number of characters written (excluding null terminator)
 * 
 * If truncation occurs:
 * - Buffer is null-terminated at buffer_size-1
 * - Serial warning is printed
 * - Fault is logged
 */
size_t safe_snprintf(char* buffer, size_t buffer_size, const char* format, ...);

/**
 * Safe wrapper around vsnprintf
 * 
 * @param buffer Destination buffer
 * @param buffer_size Size of destination buffer
 * @param format Printf-style format string
 * @param args Argument list
 * @return Number of characters written (excluding null terminator)
 */
size_t safe_vsnprintf(char* buffer, size_t buffer_size, const char* format, va_list args);

/**
 * Safe string copy with truncation detection
 * 
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 * @return true if copy successful, false if truncated
 */
bool safe_strcpy(char* dest, size_t dest_size, const char* src);

/**
 * Safe string concatenation with truncation detection
 * 
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to append
 * @return true if concatenation successful, false if truncated
 */
bool safe_strcat(char* dest, size_t dest_size, const char* src);

/**
 * Validate buffer doesn't contain uninitialized memory
 * 
 * @param buffer Pointer to check
 * @param size Size to check
 * @return true if buffer appears valid
 */
bool safe_is_valid_string(const char* buffer, size_t max_size);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * SAFE_STRCPY(dest, src, size)
 * Recommended macro for fixed-size buffer copies.
 * Ensures null-termination and logs warnings on truncation.
 */
#define SAFE_STRCPY(dest, src, size) safe_strcpy(dest, size, src)

/**
 * SAFE_SNPRINTF(dest, size, format, ...)
 * Recommended macro for formatted string creation.
 * Ensures null-termination and logs warnings on truncation.
 */
#define SAFE_SNPRINTF(dest, size, format, ...) safe_snprintf(dest, size, format, ##__VA_ARGS__)

#endif // STRING_SAFETY_H

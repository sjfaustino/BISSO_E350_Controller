#ifndef SERIAL_LOGGER_H
#define SERIAL_LOGGER_H

#include <Arduino.h>
#include <stdint.h>
#include <stdarg.h>

// ============================================================================
// LOG LEVEL DEFINITIONS
// ============================================================================

typedef enum {
  LOG_LEVEL_NONE = 0,      // No output
  LOG_LEVEL_ERROR = 1,     // Only errors
  LOG_LEVEL_WARNING = 2,   // Errors and warnings
  LOG_LEVEL_INFO = 3,      // Errors, warnings, and info
  LOG_LEVEL_DEBUG = 4,     // All including debug
  LOG_LEVEL_VERBOSE = 5    // Maximum verbosity
} log_level_t;

// ============================================================================
// OPTIMIZED LOGGING FUNCTIONS
// ============================================================================

/**
 * @brief Initialize serial logger
 * @param log_level Minimum log level to output (see log_level_t)
 */
void serialLoggerInit(log_level_t log_level);

/**
 * @brief Set current log level at runtime
 * @param log_level New log level
 */
void serialLoggerSetLevel(log_level_t log_level);

/**
 * @brief Get current log level
 * @return Current log level
 */
log_level_t serialLoggerGetLevel();

/**
 * @brief Log error message (uses snprintf for efficiency)
 * @param format Format string (printf style)
 * @param ... Arguments
 */
void logError(const char* format, ...);

/**
 * @brief Log warning message
 * @param format Format string
 * @param ... Arguments
 */
void logWarning(const char* format, ...);

/**
 * @brief Log info message
 * @param format Format string
 * @param ... Arguments
 */
void logInfo(const char* format, ...);

/**
 * @brief Log debug message
 * @param format Format string
 * @param ... Arguments
 */
void logDebug(const char* format, ...);

/**
 * @brief Log verbose message
 * @param format Format string
 * @param ... Arguments
 */
void logVerbose(const char* format, ...);

/**
 * @brief Print formatted string directly (no level filtering)
 * @param format Format string
 * @param ... Arguments
 */
void logPrintf(const char* format, ...);

/**
 * @brief Print buffered log with newline
 * @param format Format string
 * @param ... Arguments
 */
void logPrintln(const char* format, ...);

/**
 * @brief Get internal buffer pointer (for direct manipulation if needed)
 * @return Pointer to internal buffer
 */
char* serialLoggerGetBuffer();

/**
 * @brief Get internal buffer size
 * @return Buffer size in bytes
 */
size_t serialLoggerGetBufferSize();

/**
 * @brief Acquire serial mutex for thread-safe direct Serial access
 * @return true if acquired, false on timeout
 * @note Use serialLoggerUnlock() after Serial operations complete
 */
bool serialLoggerLock();

/**
 * @brief Release serial mutex after thread-safe Serial access
 */
void serialLoggerUnlock();

#endif

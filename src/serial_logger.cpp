#include "serial_logger.h"

// ============================================================================
// SERIAL LOGGER STATE
// ============================================================================

#define LOGGER_BUFFER_SIZE 512

static log_level_t current_log_level = LOG_LEVEL_INFO;
static char log_buffer[LOGGER_BUFFER_SIZE];
static bool logger_initialized = false;

// ============================================================================
// HELPER FUNCTION - Buffered print with level checking
// ============================================================================

static void vlogPrint(log_level_t level, const char* prefix, const char* format, va_list args) {
  if (level > current_log_level) return;  // Skip if below threshold
  
  // Build complete message in buffer
  int offset = 0;
  
  // Add prefix if provided
  if (prefix != NULL) {
    offset = snprintf(log_buffer, LOGGER_BUFFER_SIZE, "%s", prefix);
  }
  
  // Add formatted message
  vsnprintf(log_buffer + offset, LOGGER_BUFFER_SIZE - offset, format, args);
  
  // Send single Serial.println() instead of multiple Serial.print() calls
  Serial.println(log_buffer);
}

// ============================================================================
// SERIAL LOGGER IMPLEMENTATION
// ============================================================================

void serialLoggerInit(log_level_t log_level) {
  current_log_level = log_level;
  logger_initialized = true;
  
  // Print initialization banner
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║         BISSO v4.2 Serial Logger Initialized              ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝");
  
  const char* level_names[] = {
    "NONE", "ERROR", "WARNING", "INFO", "DEBUG", "VERBOSE"
  };
  Serial.print("Log Level: ");
  Serial.println(level_names[log_level]);
  Serial.println();
}

void serialLoggerSetLevel(log_level_t log_level) {
  current_log_level = log_level;
  logInfo("Log level changed to: %d", log_level);
}

log_level_t serialLoggerGetLevel() {
  return current_log_level;
}

void logError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vlogPrint(LOG_LEVEL_ERROR, "[ERROR] ", format, args);
  va_end(args);
}

void logWarning(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vlogPrint(LOG_LEVEL_WARNING, "[WARN]  ", format, args);
  va_end(args);
}

void logInfo(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vlogPrint(LOG_LEVEL_INFO, "[INFO]  ", format, args);
  va_end(args);
}

void logDebug(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vlogPrint(LOG_LEVEL_DEBUG, "[DEBUG] ", format, args);
  va_end(args);
}

void logVerbose(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vlogPrint(LOG_LEVEL_VERBOSE, "[VERB]  ", format, args);
  va_end(args);
}

void logPrintf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args);
  va_end(args);
  Serial.print(log_buffer);
}

void logPrintln(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args);
  va_end(args);
  Serial.println(log_buffer);
}

char* serialLoggerGetBuffer() {
  return log_buffer;
}

size_t serialLoggerGetBufferSize() {
  return LOGGER_BUFFER_SIZE;
}

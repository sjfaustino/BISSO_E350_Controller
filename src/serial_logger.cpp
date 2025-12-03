#include "serial_logger.h"
#include "firmware_version.h" // <-- NEW: Required for banner
#include <stdio.h>

#define LOGGER_BUFFER_SIZE 512
static log_level_t current_log_level = LOG_LEVEL_INFO;
static char log_buffer[LOGGER_BUFFER_SIZE];

static void vlogPrint(log_level_t level, const char* prefix, const char* format, va_list args) {
  if (level > current_log_level) return;
  int offset = 0;
  if (prefix != NULL) offset = snprintf(log_buffer, LOGGER_BUFFER_SIZE, "%s", prefix);
  vsnprintf(log_buffer + offset, LOGGER_BUFFER_SIZE - offset, format, args);
  Serial.println(log_buffer);
}

void serialLoggerInit(log_level_t log_level) {
  current_log_level = log_level;
  
  // FIX: Dynamic Banner
  char ver_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(ver_str, sizeof(ver_str));

  Serial.println("\n------------------------------------------");
  Serial.printf("     %s Serial Logger Init     \n", ver_str);
  Serial.println("------------------------------------------\n");
  
  Serial.printf("Log Level: %d\n\n", log_level);
}

void serialLoggerSetLevel(log_level_t log_level) {
  current_log_level = log_level;
  logInfo("Log level changed to: %d", log_level);
}

log_level_t serialLoggerGetLevel() { return current_log_level; }

void logError(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_ERROR, "[ERROR] ", format, args); va_end(args);
}

void logWarning(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_WARNING, "[WARN]  ", format, args); va_end(args);
}

void logInfo(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_INFO, "[INFO]  ", format, args); va_end(args);
}

void logDebug(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_DEBUG, "[DEBUG] ", format, args); va_end(args);
}

void logVerbose(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_VERBOSE, "[VERB]  ", format, args); va_end(args);
}

void logPrintf(const char* format, ...) {
  va_list args; va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args); va_end(args);
  Serial.print(log_buffer);
}

void logPrintln(const char* format, ...) {
  va_list args; va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args); va_end(args);
  Serial.println(log_buffer);
}

char* serialLoggerGetBuffer() { return log_buffer; }
size_t serialLoggerGetBufferSize() { return LOGGER_BUFFER_SIZE; }
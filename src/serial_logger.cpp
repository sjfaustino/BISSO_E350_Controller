#include "serial_logger.h"
#include "firmware_version.h" 
#include "network_manager.h" // <-- NEW: Link to Network Manager
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define LOGGER_BUFFER_SIZE 512
static log_level_t current_log_level = LOG_LEVEL_INFO;
static char log_buffer[LOGGER_BUFFER_SIZE];

// Thread-safety: Mutex for serial output
static SemaphoreHandle_t serial_mutex = NULL;
static bool mutex_initialized = false;

/**
 * @brief Initialize the serial mutex (called lazily)
 */
static void ensureMutexInitialized() {
  if (!mutex_initialized) {
    serial_mutex = xSemaphoreCreateMutex();
    mutex_initialized = true;
  }
}

/**
 * @brief Acquire serial mutex with timeout
 * @return true if acquired, false if timeout
 */
static bool acquireSerialMutex() {
  ensureMutexInitialized();
  if (!serial_mutex) return true;  // No mutex, proceed without lock
  
  // Use 50ms timeout to avoid deadlocks
  return xSemaphoreTake(serial_mutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

/**
 * @brief Release serial mutex
 */
static void releaseSerialMutex() {
  if (serial_mutex) {
    xSemaphoreGive(serial_mutex);
  }
}

static void vlogPrint(log_level_t level, const char* prefix, const char* format, va_list args) {
  if (level > current_log_level) return;
  
  // Acquire mutex for thread-safe output
  bool locked = acquireSerialMutex();
  
  int offset = 0;
  if (prefix != NULL) offset = snprintf(log_buffer, LOGGER_BUFFER_SIZE, "%s", prefix);
  vsnprintf(log_buffer + offset, LOGGER_BUFFER_SIZE - offset, format, args);
  
  // Output to Serial (UART)
  Serial.println(log_buffer);

  // Output to Telnet (Network Mirror)
  // Check if network manager is active (simple check to avoid crash before init)
  // Ideally, use a safer singleton pattern or check a flag. 
  // For now, assuming networkManager is global and robust.
  networkManager.telnetPrintln(log_buffer);
  
  if (locked) releaseSerialMutex();
}

void serialLoggerInit(log_level_t log_level) {
  // Initialize mutex early
  ensureMutexInitialized();
  
  current_log_level = log_level;
  
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
  bool locked = acquireSerialMutex();
  
  va_list args; va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args); va_end(args);
  Serial.print(log_buffer);
  networkManager.telnetPrint(log_buffer); // Mirror raw prints
  
  if (locked) releaseSerialMutex();
}

void logPrintln(const char* format, ...) {
  bool locked = acquireSerialMutex();
  
  va_list args; va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args); va_end(args);
  Serial.println(log_buffer);
  networkManager.telnetPrintln(log_buffer); // Mirror raw prints
  
  if (locked) releaseSerialMutex();
}

char* serialLoggerGetBuffer() { return log_buffer; }
size_t serialLoggerGetBufferSize() { return LOGGER_BUFFER_SIZE; }

bool serialLoggerLock() {
  return acquireSerialMutex();
}

void serialLoggerUnlock() {
  releaseSerialMutex();
}
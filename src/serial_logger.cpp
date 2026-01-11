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

// Forward declaration for boot log writing
static void bootLogWrite(const char* message);

/**
 * @brief Initialize the serial mutex (called lazily)
 */


/**
 * @brief Acquire serial mutex with timeout
 * @return true if acquired, false if timeout
 */
static bool acquireSerialMutex() {
  if (!mutex_initialized || !serial_mutex) return false;
  
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
  // CRITICAL FIX: If we can't get the lock, we MUST NOT touch the static buffer
  if (!acquireSerialMutex()) {
      // Optional: Print a minimal marker using ROM functions to indicate drop, 
      // or just silently drop to prevent corruption.
      // ets_printf("!"); 
      return; 
  }
  
  int offset = 0;
  if (prefix != NULL) offset = snprintf(log_buffer, LOGGER_BUFFER_SIZE, "%s", prefix);
  vsnprintf(log_buffer + offset, LOGGER_BUFFER_SIZE - offset, format, args);
  
  // Output to Serial (UART) only
  Serial.println(log_buffer);
  
  // Also write to boot log file if active
  bootLogWrite(log_buffer);
  
  releaseSerialMutex();
}

void serialLoggerInit(log_level_t log_level) {
  // Initialize mutex explicitly
  if (!mutex_initialized) {
    serial_mutex = xSemaphoreCreateMutex();
    mutex_initialized = true;
  }
  
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
  // CRITICAL FIX: If we can't get the lock, we MUST NOT touch the static buffer
  if (!acquireSerialMutex()) {
      return; 
  }
  
  va_list args; va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args); va_end(args);
  Serial.print(log_buffer);
  
  // Note: Telnet mirroring might be unsafe if networkManager locks too.
  // Ideally, use a queue for telnet. For now, this is kept but protected by serial_mutex.
  networkManager.telnetPrint(log_buffer); 
  
  releaseSerialMutex();
}

void logPrintln(const char* format, ...) {
  // CRITICAL FIX: If we can't get the lock, we MUST NOT touch the static buffer
  if (!acquireSerialMutex()) {
      return; 
  }
  
  va_list args; va_start(args, format);
  vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args); va_end(args);
  Serial.println(log_buffer);
  networkManager.telnetPrintln(log_buffer); // Mirror raw prints
  
  releaseSerialMutex();
}

char* serialLoggerGetBuffer() { return log_buffer; }
size_t serialLoggerGetBufferSize() { return LOGGER_BUFFER_SIZE; }

bool serialLoggerLock() {
  return acquireSerialMutex();
}

void serialLoggerUnlock() {
  releaseSerialMutex();
}

// ============================================================================
// BOOT LOG CAPTURE (LittleFS)
// ============================================================================

#include <LittleFS.h>
#include "config_keys.h"
#include "config_unified.h"

#define BOOT_LOG_PATH "/bootlog.txt"

static File boot_log_file;
static bool boot_logging_active = false;
static size_t boot_log_max_size = 32768;
static size_t boot_log_current_size = 0;

bool bootLogInit(size_t max_size_bytes) {
    // Check if boot logging is enabled in config (default: enabled)
    if (configGetInt(KEY_BOOTLOG_EN, 1) == 0) {
        Serial.println("[BOOTLOG] Disabled by configuration");
        return false;
    }
    
    // Mount LittleFS if not already mounted
    if (!LittleFS.begin(false)) {
        Serial.println("[BOOTLOG] LittleFS mount failed");
        return false;
    }
    
    boot_log_max_size = max_size_bytes;
    
    // Remove old log file (overwrite on each boot)
    if (LittleFS.exists(BOOT_LOG_PATH)) {
        LittleFS.remove(BOOT_LOG_PATH);
    }
    
    // Open new log file for writing
    boot_log_file = LittleFS.open(BOOT_LOG_PATH, "w");
    if (!boot_log_file) {
        Serial.println("[BOOTLOG] Failed to create log file");
        return false;
    }
    
    boot_logging_active = true;
    boot_log_current_size = 0;
    
    // Write header
    const char* header = "=== BOOT LOG START ===\n";
    boot_log_file.print(header);
    boot_log_current_size += strlen(header);
    boot_log_file.flush();
    
    Serial.println("[BOOTLOG] Boot log capture started");
    return true;
}

void bootLogStop() {
    if (!boot_logging_active) return;
    
    boot_logging_active = false;
    
    if (boot_log_file) {
        const char* footer = "\n=== BOOT LOG END ===\n";
        boot_log_file.print(footer);
        boot_log_file.flush();
        boot_log_file.close();
    }
    
    Serial.printf("[BOOTLOG] Boot log capture stopped (%u bytes)\n", boot_log_current_size);
}

bool bootLogIsActive() {
    return boot_logging_active;
}

size_t bootLogGetSize() {
    if (!LittleFS.begin(false)) return 0;
    
    if (!LittleFS.exists(BOOT_LOG_PATH)) return 0;
    
    File f = LittleFS.open(BOOT_LOG_PATH, "r");
    if (!f) return 0;
    
    size_t size = f.size();
    f.close();
    return size;
}

size_t bootLogRead(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return 0;
    
    if (!LittleFS.begin(false)) return 0;
    
    if (!LittleFS.exists(BOOT_LOG_PATH)) {
        strncpy(buffer, "(No boot log available)", max_len - 1);
        buffer[max_len - 1] = '\0';
        return strlen(buffer);
    }
    
    File f = LittleFS.open(BOOT_LOG_PATH, "r");
    if (!f) {
        strncpy(buffer, "(Failed to open boot log)", max_len - 1);
        buffer[max_len - 1] = '\0';
        return strlen(buffer);
    }
    
    size_t bytes_read = f.readBytes(buffer, max_len - 1);
    buffer[bytes_read] = '\0';
    f.close();
    
    return bytes_read;
}

/**
 * @brief Internal helper to write to boot log file (called from vlogPrint)
 */
static void bootLogWrite(const char* message) {
    if (!boot_logging_active || !boot_log_file) return;
    
    size_t msg_len = strlen(message);
    
    // Check size limit (leave room for footer)
    if (boot_log_current_size + msg_len + 50 > boot_log_max_size) {
        // Stop logging to prevent overflow
        boot_log_file.print("\n[BOOTLOG] Size limit reached, stopping capture\n");
        boot_log_file.flush();
        boot_logging_active = false;
        boot_log_file.close();
        return;
    }
    
    boot_log_file.print(message);
    boot_log_file.print("\n");
    boot_log_current_size += msg_len + 1;
    
    // Flush periodically (every 1KB)
    if (boot_log_current_size % 1024 < msg_len) {
        boot_log_file.flush();
    }
}

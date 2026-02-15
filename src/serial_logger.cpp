#include "serial_logger.h"
#include "firmware_version.h" 
#include "network_manager.h"
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "task_manager.h"

// ESP32-S2/S3 USB CDC Serial
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    #define SerialOut Serial
#else
    #define SerialOut Serial
#endif

#include <SD.h>
#include "sd_card_manager.h"

#define LOGGER_BUFFER_SIZE 512
static log_level_t current_log_level = LOG_LEVEL_INFO;

// Thread-safety: Mutex for serial output
static SemaphoreHandle_t serial_mutex = NULL;
static bool mutex_initialized = false;

// Shared static buffer to prevent stack overflows in system tasks
// Protected by serial_mutex
static char shared_formatting_buffer[LOGGER_BUFFER_SIZE];

// Async Queue State
#define LOG_QUEUE_DEPTH 32
struct log_msg_t {
    char text[LOGGER_BUFFER_SIZE];
};
static QueueHandle_t log_queue = NULL;
static bool async_enabled = false;
static TaskHandle_t logger_task_handle = NULL;

// Forward declarations
static void bootLogWrite(const char* message);
static void loggerTask(void* parameter);

// ============================================================================
// MUTEX HELPERS
// ============================================================================

static bool acquireSerialMutex() {
  if (!mutex_initialized || !serial_mutex) return true;  // Allow if not initialized
  
  // Use consistent timeout for all tasks
  // For recursive mutexes, if this thread already has it, this returns immediately
  TickType_t timeout = pdMS_TO_TICKS(100);
  return xSemaphoreTakeRecursive(serial_mutex, timeout) == pdTRUE;
}

static void releaseSerialMutex() {
  if (serial_mutex && mutex_initialized) {
    xSemaphoreGiveRecursive(serial_mutex);
  }
}

// ============================================================================
// CORE LOGGING
// ============================================================================

static void vlogPrint(log_level_t level, const char* prefix, const char* format, va_list args) {
  if (level > current_log_level) return;
  
  // Lock first: The shared_formatting_buffer is NOT thread-safe
  if (!acquireSerialMutex()) return;

  int offset = 0;
  if (prefix != NULL) offset = snprintf(shared_formatting_buffer, LOGGER_BUFFER_SIZE, "%s", prefix);
  vsnprintf(shared_formatting_buffer + offset, LOGGER_BUFFER_SIZE - offset, format, args);
  
  // Terminal log entries should be atomic
  if (async_enabled && log_queue != NULL) {
    log_msg_t msg;
    strncpy(msg.text, shared_formatting_buffer, LOGGER_BUFFER_SIZE - 1);
    msg.text[LOGGER_BUFFER_SIZE - 1] = '\0';
    
    if (xQueueSend(log_queue, &msg, 0) != pdTRUE) {
        SerialOut.println(shared_formatting_buffer);
        SerialOut.println("[DEBUG] Log queue overflowed!");
    }
  } else {
    SerialOut.println(shared_formatting_buffer);
  }
  
  bootLogWrite(shared_formatting_buffer);
  systemLogWrite(shared_formatting_buffer);
  releaseSerialMutex();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void serialLoggerInit(log_level_t log_level) {
  // Initialize hardware buffer for USB CDC (ESP32-S3)
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
  SerialOut.setTxBufferSize(2048);
  #endif

  // Initialize mutex
  if (!mutex_initialized) {
    serial_mutex = xSemaphoreCreateRecursiveMutex();
    mutex_initialized = (serial_mutex != NULL);
  }
  
  current_log_level = log_level;
  
  char ver_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(ver_str, sizeof(ver_str));

  SerialOut.println("\n------------------------------------------");
  SerialOut.printf("     %s Serial Logger Init     \n", ver_str);
  SerialOut.println("------------------------------------------\n");
  SerialOut.flush();
  
  SerialOut.printf("Log Level: %d\n\n", log_level);
}

// Implementation of log queue for background processing
void logQueueInit() {
  if (log_queue != NULL) return;

  // Use PSRAM for the queue storage if available
  log_queue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(log_msg_t));
  if (log_queue == NULL) {
    SerialOut.println("[ERROR] Failed to create log queue!");
    return;
  }

  // Create logger task on Core 0 (Higher Priority for UI Responsiveness)
  // Priority 5 ensures it runs above background tasks but below motion control
  xTaskCreatePinnedToCore(
      loggerTask,
      "LoggerTask",
      4096,
      NULL,
      5, // Priority 5
      &logger_task_handle,
      0 // Core 0
  );

  logInfo("[LOGGER] Async queue initialized (depth: %d)", LOG_QUEUE_DEPTH);
}

void logQueueEnableAsync() {
  if (log_queue != NULL && logger_task_handle != NULL) {
    async_enabled = true;
    logInfo("[LOGGER] Async logging enabled");
  }
}

bool logQueueIsReady() {
  return (log_queue != NULL && logger_task_handle != NULL);
}

static void loggerTask(void* parameter) {
    log_msg_t msg;
    while (true) {
        if (xQueueReceive(log_queue, &msg, portMAX_DELAY) == pdTRUE) {
            // No mutex needed for SerialOut here as it's the only consumer
            // but we use logPrintf-style lock for consistency if others use SerialOut directly
            if (acquireSerialMutex()) {
                SerialOut.println(msg.text);
                releaseSerialMutex();
            }
        }
    }
}

void serialLoggerSetLevel(log_level_t log_level) {
  current_log_level = log_level;
  logInfo("Log level changed to: %d", log_level);
}

log_level_t serialLoggerGetLevel() { return current_log_level; }

// ============================================================================
// LOG FUNCTIONS
// ============================================================================

void logError(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_ERROR, "[ERROR] ", format, args); 
  va_end(args);
}

void logWarning(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_WARNING, "[WARN]  ", format, args); 
  va_end(args);
}

void logInfo(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_INFO, "[INFO]  ", format, args); 
  va_end(args);
}

void logDebug(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_DEBUG, "[DEBUG] ", format, args); 
  va_end(args);
}

void logVerbose(const char* format, ...) {
  va_list args; va_start(args, format);
  vlogPrint(LOG_LEVEL_VERBOSE, "[VERB]  ", format, args); 
  va_end(args);
}

void logPrintf(const char* format, ...) {
  if (!acquireSerialMutex()) return;

  // Stack-local buffer — safe from shared buffer contention
  // Always synchronous — CLI output must not go through async queue
  char buf[256];
  va_list args; va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args); 
  va_end(args);
  
  SerialOut.print(buf);
  networkManager.telnetPrint(buf);
  systemLogWrite(buf);
  releaseSerialMutex();
}

void logPrintln(const char* format, ...) {
  if (!acquireSerialMutex()) return;

  char buf[256];
  va_list args; va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args); 
  va_end(args);
  
  SerialOut.println(buf);
  networkManager.telnetPrintln(buf);
  systemLogWrite(buf);
  releaseSerialMutex();
}

// ============================================================================
// DIRECT PRINT (NO MUTEX - caller must hold serialLoggerLock)
// ============================================================================

void logDirectPrintf(const char* format, ...) {
  char buf[256];
  va_list args; va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  SerialOut.print(buf);
}

void logDirectPrintln(const char* format, ...) {
  char buf[256];
  va_list args; va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  SerialOut.println(buf);
}

// ============================================================================
// BUFFER ACCESS
// ============================================================================

char* serialLoggerGetBuffer() { return nullptr; }
size_t serialLoggerGetBufferSize() { return 0; }

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
    if (configGetInt(KEY_BOOTLOG_EN, 0) == 0) {
        SerialOut.println("[BOOTLOG] Disabled by configuration");
        return false;
    }
    
    if (!LittleFS.begin(false)) {
        SerialOut.println("[BOOTLOG] LittleFS mount failed");
        return false;
    }
    
    boot_log_max_size = max_size_bytes;
    
    if (LittleFS.exists(BOOT_LOG_PATH)) {
        LittleFS.remove(BOOT_LOG_PATH);
    }
    
    boot_log_file = LittleFS.open(BOOT_LOG_PATH, "w");
    if (!boot_log_file) {
        SerialOut.println("[BOOTLOG] Failed to create log file");
        return false;
    }
    
    boot_logging_active = true;
    boot_log_current_size = 0;
    
    const char* header = "=== BOOT LOG START ===\n";
    boot_log_file.print(header);
    boot_log_current_size += strlen(header);
    boot_log_file.flush();
    
    SerialOut.println("[BOOTLOG] Boot log capture started");
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
    
    SerialOut.printf("[BOOTLOG] Boot log capture stopped (%u bytes)\n", boot_log_current_size);
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

static void bootLogWrite(const char* message) {
    if (!boot_logging_active || !boot_log_file) return;
    
    size_t msg_len = strlen(message);
    
    if (boot_log_current_size + msg_len + 50 > boot_log_max_size) {
        boot_log_file.print("\n[BOOTLOG] Size limit reached, stopping capture\n");
        boot_log_file.flush();
        boot_logging_active = false;
        boot_log_file.close();
        return;
    }
    
    boot_log_file.print(message);
    boot_log_file.print("\n");
    boot_log_current_size += msg_len + 1;
    
    if (boot_log_current_size % 1024 < msg_len) {
        boot_log_file.flush();
    }
}

// ============================================================================
// PERSISTENT SYSTEM LOGGING (SD Card)
// ============================================================================

static File system_log_file;
static bool system_logging_active = false;
static std::string system_log_path = "";

bool systemLogInit(const char* path) {
    if (!sdCardIsMounted()) {
        SerialOut.println("[SYSTEMLOG] SD card not mounted, persistent logging disabled");
        return false;
    }

    // Ensure directory exists recursively
    std::string path_str = path;
    size_t last_slash = path_str.find_last_of('/');
    if (last_slash != std::string::npos && last_slash > 0) {
        std::string dir = path_str.substr(0, last_slash);
        sdCardMkdirRecursive(dir.c_str());
    }

    system_log_path = path;
    
    // Open in append mode (Linux style)
    system_log_file = SD.open(path, FILE_APPEND);
    if (!system_log_file) {
        // Try creating if append fails (might not exist)
        system_log_file = SD.open(path, FILE_WRITE);
    }

    if (!system_log_file) {
        SerialOut.printf("[SYSTEMLOG] Failed to open path: %s\n", path);
        return false;
    }

    system_logging_active = true;
    
    // Write session header
    system_log_file.printf("\n--- SESSION START: %lu ---\n", (unsigned long)millis());
    system_log_file.flush();
    
    SerialOut.printf("[SYSTEMLOG] Persistent logging active at %s\n", path);
    return true;
}

void systemLogStop() {
    if (system_logging_active) {
        system_log_file.println("--- SESSION END ---");
        system_log_file.flush();
        system_log_file.close();
        system_logging_active = false;
    }
}

void systemLogWrite(const char* message) {
    if (!system_logging_active || !system_log_file) return;
    
    // Write and flush periodically
    system_log_file.println(message);
    
    // Safety: check if card is still mounted before flush
    if (sdCardIsMounted()) {
        static uint32_t last_flush = 0;
        if (millis() - last_flush > 5000) {
            system_log_file.flush();
            last_flush = millis();
        }
    } else {
        system_logging_active = false;
        system_log_file.close();
    }
}

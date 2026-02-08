/**
 * @file memory_prealloc.cpp
 * @brief Implementation of centralized memory pre-allocation
 */

#include "memory_prealloc.h"
#include "psram_alloc.h"
#include "serial_logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

// Static buffers
static char* api_status_buffer = nullptr;
static void* history_export_buffer = nullptr;

// Mutexes for concurrency protection
static SemaphoreHandle_t status_mutex = nullptr;
static SemaphoreHandle_t history_mutex = nullptr;

bool memoryPreallocInit() {
    logInfo("[PREALLOC] Initializing PSRAM buffers...");

    // 1. Initialize mutexes
    status_mutex = xSemaphoreCreateMutex();
    history_mutex = xSemaphoreCreateMutex();

    if (status_mutex == nullptr || history_mutex == nullptr) {
        logError("[PREALLOC] Failed to create mutexes");
        return false;
    }

    // 2. Allocate Status Buffer (2KB)
    api_status_buffer = (char*)psramMalloc(API_STATUS_BUFFER_SIZE);
    if (api_status_buffer == nullptr) {
        logError("[PREALLOC] Failed to allocate Status Buffer (%d bytes)", API_STATUS_BUFFER_SIZE);
        return false;
    }
    memset(api_status_buffer, 0, API_STATUS_BUFFER_SIZE);

    // 3. Allocate History Export Buffer (~172KB)
    history_export_buffer = psramMalloc(TELEMETRY_HISTORY_EXPORT_SIZE);
    if (history_export_buffer == nullptr) {
        logError("[PREALLOC] Failed to allocate History Export Buffer (%d bytes)", TELEMETRY_HISTORY_EXPORT_SIZE);
        return false;
    }
    memset(history_export_buffer, 0, TELEMETRY_HISTORY_EXPORT_SIZE);

    logInfo("[PREALLOC] Success! Allocated %d KB total in PSRAM", 
            (API_STATUS_BUFFER_SIZE + TELEMETRY_HISTORY_EXPORT_SIZE) / 1024);
    
    return true;
}

char* memoryGetStatusBuffer() {
    return api_status_buffer;
}

void* memoryGetHistoryExportBuffer() {
    return history_export_buffer;
}

bool memoryLockStatusBuffer(uint32_t timeout_ms) {
    if (status_mutex == nullptr) return false;
    return xSemaphoreTake(status_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void memoryUnlockStatusBuffer() {
    if (status_mutex != nullptr) xSemaphoreGive(status_mutex);
}

bool memoryLockHistoryBuffer(uint32_t timeout_ms) {
    if (history_mutex == nullptr) return false;
    return xSemaphoreTake(history_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void memoryUnlockHistoryBuffer() {
    if (history_mutex != nullptr) xSemaphoreGive(history_mutex);
}

#include "telemetry_history.h"
#include "psram_alloc.h"
#include "serial_logger.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static telemetry_packet_t* history_buffer = NULL;
static uint16_t history_head = 0;
static uint16_t history_count = 0;
static SemaphoreHandle_t history_mutex = NULL;

void telemetryHistoryInit() {
    if (history_buffer != NULL) return;

    history_mutex = xSemaphoreCreateMutex();
    
    // Allocate buffer in PSRAM
    // 3600 * ~40 bytes = ~144KB
    history_buffer = (telemetry_packet_t*)psramCalloc(TELEMETRY_HISTORY_DEPTH, sizeof(telemetry_packet_t));
    
    if (history_buffer == NULL) {
        logError("[HISTORY] Failed to allocate telemetry history in PSRAM!");
        return;
    }

    logInfo("[HISTORY] Initialized 1-hour buffer in PSRAM (%u bytes)", 
            (uint32_t)(TELEMETRY_HISTORY_DEPTH * sizeof(telemetry_packet_t)));
}

void telemetryHistoryAdd(const telemetry_packet_t* packet) {
    if (history_buffer == NULL || history_mutex == NULL || packet == NULL) return;

    if (xSemaphoreTake(history_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&history_buffer[history_head], packet, sizeof(telemetry_packet_t));
        
        history_head = (history_head + 1) % TELEMETRY_HISTORY_DEPTH;
        if (history_count < TELEMETRY_HISTORY_DEPTH) {
            history_count++;
        }
        
        xSemaphoreGive(history_mutex);
    }
}

void telemetryHistoryGet(telemetry_packet_t* buffer, uint16_t* count) {
    if (history_buffer == NULL || history_mutex == NULL || buffer == NULL || count == NULL) {
        if (count) *count = 0;
        return;
    }

    if (xSemaphoreTake(history_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *count = history_count;
        
        // Copy in chronological order (oldest first)
        uint16_t start_idx = (history_count < TELEMETRY_HISTORY_DEPTH) ? 0 : history_head;
        
        for (uint16_t i = 0; i < history_count; i++) {
            uint16_t idx = (start_idx + i) % TELEMETRY_HISTORY_DEPTH;
            memcpy(&buffer[i], &history_buffer[idx], sizeof(telemetry_packet_t));
        }
        
        xSemaphoreGive(history_mutex);
    } else {
        *count = 0;
    }
}

uint16_t telemetryHistoryGetCount() {
    return history_count;
}

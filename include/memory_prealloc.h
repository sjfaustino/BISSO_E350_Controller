/**
 * @file memory_prealloc.h
 * @brief Centralized memory pre-allocation for high-frequency buffers
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Buffer sizes
#define API_STATUS_BUFFER_SIZE 2048
#define TELEMETRY_HISTORY_EXPORT_SIZE (200 * 1024) // Safe for 3600 * sizeof(telemetry_packet_t)

/**
 * @brief Initialize all pre-allocated buffers in PSRAM
 * @return true if all allocations succeeded
 */
bool memoryPreallocInit();

/**
 * @brief Get the pre-allocated buffer for API status responses
 * @return Pointer to buffer, or NULL if allocation failed
 */
char* memoryGetStatusBuffer();

/**
 * @brief Get the pre-allocated buffer for telemetry history exports
 * @return Pointer to buffer, or NULL if allocation failed
 */
void* memoryGetHistoryExportBuffer();

/**
 * @brief Lock the status buffer for exclusive access
 */
bool memoryLockStatusBuffer(uint32_t timeout_ms);

/**
 * @brief Unlock the status buffer
 */
void memoryUnlockStatusBuffer();

/**
 * @brief Lock the history export buffer for exclusive access
 */
bool memoryLockHistoryBuffer(uint32_t timeout_ms);

/**
 * @brief Unlock the history export buffer
 */
void memoryUnlockHistoryBuffer();

#ifdef __cplusplus
}
#endif

/**
 * @file psram_alloc.h
 * @brief PSRAM-aware memory allocation helpers for ESP32-S3
 * @project BISSO E350 Controller
 * 
 * Provides allocation functions that prefer PSRAM (external SPI RAM)
 * for large buffers while gracefully falling back to internal heap
 * when PSRAM is not available or allocation fails.
 * 
 * Usage:
 *   void* buf = psramMalloc(4096);  // Allocates in PSRAM if available
 *   psramFree(buf);                 // Works for both PSRAM and internal
 */

#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

/**
 * @brief Check if PSRAM is available on this board
 * @return true if PSRAM is detected and usable
 */
bool psramIsAvailable();

/**
 * @brief Get total PSRAM size in bytes
 * @return PSRAM size, or 0 if not available
 */
size_t psramGetTotalSize();

/**
 * @brief Get free PSRAM size in bytes
 * @return Free PSRAM bytes, or 0 if not available
 */
size_t psramGetFreeSize();

/**
 * @brief Allocate memory with PSRAM preference
 * 
 * Attempts to allocate in PSRAM first. If PSRAM is not available
 * or allocation fails, falls back to internal heap.
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* psramMalloc(size_t size);

/**
 * @brief Allocate zero-initialized memory with PSRAM preference
 * 
 * Like psramMalloc but initializes memory to zero.
 * 
 * @param count Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 */
void* psramCalloc(size_t count, size_t size);

/**
 * @brief Reallocate memory with PSRAM preference
 * 
 * @param ptr Existing pointer (can be NULL for new allocation)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* psramRealloc(void* ptr, size_t size);

/**
 * @brief Free memory allocated by psram* functions
 * 
 * Safe to use with both PSRAM and internal heap allocations.
 * 
 * @param ptr Pointer to free (can be NULL)
 */
void psramFree(void* ptr);

/**
 * @brief Check if a pointer was allocated in PSRAM
 * 
 * @param ptr Pointer to check
 * @return true if pointer is in PSRAM address range
 */
bool psramIsPointerInPsram(void* ptr);

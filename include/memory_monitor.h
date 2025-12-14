#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// MEMORY MONITORING TYPES
// ============================================================================

typedef struct {
  uint32_t current_free;        // Current free heap
  uint32_t minimum_free;        // Minimum free heap observed
  uint32_t maximum_used;        // Maximum heap used
  uint32_t allocations_count;   // Number of allocations
  uint32_t deallocations_count; // Number of deallocations
  uint32_t largest_block;       // Largest contiguous block available
  uint32_t sample_count;        // Number of samples collected
} memory_stats_t;

// ============================================================================
// MEMORY MONITORING FUNCTIONS
// ============================================================================

/**
 * @brief Initialize memory monitoring system
 */
void memoryMonitorInit();

/**
 * @brief Update memory statistics (call periodically)
 */
void memoryMonitorUpdate();

/**
 * @brief Get current memory statistics
 * @return Pointer to memory_stats_t structure
 */
memory_stats_t* memoryMonitorGetStats();

/**
 * @brief Get current free heap size
 * @return Free heap in bytes
 */
uint32_t memoryMonitorGetFreeHeap();

/**
 * @brief Get minimum free heap observed
 * @return Minimum free heap in bytes
 */
uint32_t memoryMonitorGetMinFreeHeap();

/**
 * @brief Check if memory is critically low
 * @param threshold Threshold in bytes (default 32KB)
 * @return true if free heap < threshold, false otherwise
 */
bool memoryMonitorIsCriticallyLow(uint32_t threshold);

/**
 * @brief Print memory statistics to serial
 */
void memoryMonitorPrintStats();

/**
 * @brief Reset minimum free heap counter
 */
void memoryMonitorResetMinimum();

/**
 * @brief Get memory usage percentage (0-100)
 * @return Percentage of heap used
 */
uint8_t memoryMonitorGetUsagePercent();

/**
 * @brief Get total heap size
 * @return Total heap size in bytes
 */
uint32_t memoryMonitorGetTotalHeap();

/**
 * @brief Get largest contiguous free memory block
 * @return Largest free block size in bytes
 */
uint32_t memoryMonitorGetLargestFreeBlock();

#endif

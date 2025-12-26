#include "memory_monitor.h"
#include "serial_logger.h"
#include "system_events.h"
#include <esp_heap_caps.h>

// FIX: Fully initialize struct to suppress warnings
static memory_stats_t mem_stats = {0, 0, 0, 0, 0, 0, 0};
static bool mem_monitor_initialized = false;
static uint32_t total_heap_size = 0;

// PHASE 5.10: Track low memory state for event signaling
static bool low_memory_event_active = false;

void memoryMonitorInit() {
  logInfo("[MEM] Initializing...");
  total_heap_size = ESP.getHeapSize();
  mem_stats.current_free = ESP.getFreeHeap();
  mem_stats.minimum_free = mem_stats.current_free;
  mem_stats.maximum_used = 0;
  mem_stats.allocations_count = 0;
  mem_stats.deallocations_count = 0;
  mem_stats.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  mem_stats.sample_count = 0;
  mem_monitor_initialized = true;
  logInfo("[MEM] [OK] Heap: %lu bytes", (unsigned long)total_heap_size);
}

void memoryMonitorUpdate() {
  if (!mem_monitor_initialized) { memoryMonitorInit(); return; }

  uint32_t current_free = ESP.getFreeHeap();
  uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

  mem_stats.current_free = current_free;
  if (current_free < mem_stats.minimum_free) mem_stats.minimum_free = current_free;

  uint32_t used = total_heap_size - current_free;
  if (used > mem_stats.maximum_used) mem_stats.maximum_used = used;

  mem_stats.largest_block = largest_block;
  mem_stats.sample_count++;

  // PHASE 5.10: Signal low memory event when free heap drops below 25%
  uint32_t low_memory_threshold = total_heap_size / 4;
  bool is_low_memory = (current_free < low_memory_threshold);

  if (is_low_memory && !low_memory_event_active) {
    // Transition to low memory state
    systemEventsSystemSet(EVENT_SYSTEM_LOW_MEMORY);
    low_memory_event_active = true;
  } else if (!is_low_memory && low_memory_event_active) {
    // Memory recovered above threshold
    systemEventsSystemClear(EVENT_SYSTEM_LOW_MEMORY);
    low_memory_event_active = false;
  }
}

memory_stats_t* memoryMonitorGetStats() { return &mem_stats; }
uint32_t memoryMonitorGetFreeHeap() { return ESP.getFreeHeap(); }
uint32_t memoryMonitorGetMinFreeHeap() { return mem_stats.minimum_free; }
bool memoryMonitorIsCriticallyLow(uint32_t threshold) { return (ESP.getFreeHeap() < threshold); }

void memoryMonitorPrintStats() {
  serialLoggerLock();
  Serial.println("\n=== MEMORY DIAGNOSTICS ===");
  uint32_t free = ESP.getFreeHeap();
  uint32_t used = total_heap_size - free;
  uint8_t percent = (used * 100) / total_heap_size;
  
  Serial.printf("Total Heap:   %lu\n", (unsigned long)total_heap_size);
  Serial.printf("Current Free: %lu (%u%% used)\n", (unsigned long)free, percent);
  Serial.printf("Min Free:     %lu\n", (unsigned long)mem_stats.minimum_free);
  Serial.printf("Max Used:     %lu\n", (unsigned long)mem_stats.maximum_used);
  Serial.printf("Largest Blk:  %lu\n", (unsigned long)mem_stats.largest_block);
  Serial.printf("Samples:      %lu\n", (unsigned long)mem_stats.sample_count);
  
  if (free > (total_heap_size / 2)) Serial.println("Status: [GOOD]");
  else if (free > (total_heap_size / 4)) Serial.println("Status: [WARN]");
  else Serial.println("Status: [CRITICAL]");
  Serial.println();
  serialLoggerUnlock();
}

void memoryMonitorResetMinimum() {
  mem_stats.minimum_free = ESP.getFreeHeap();
  logInfo("[MEM] [OK] Stats reset");
}

uint8_t memoryMonitorGetUsagePercent() {
  uint32_t used = total_heap_size - ESP.getFreeHeap();
  return (uint8_t)((used * 100) / total_heap_size);
}

uint32_t memoryMonitorGetTotalHeap() {
  return total_heap_size;
}

uint32_t memoryMonitorGetLargestFreeBlock() {
  return mem_stats.largest_block;
}
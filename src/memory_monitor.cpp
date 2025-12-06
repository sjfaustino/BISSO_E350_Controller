#include "memory_monitor.h"
#include <esp_heap_caps.h>

// FIX: Fully initialize struct to suppress warnings
static memory_stats_t mem_stats = {0, 0, 0, 0, 0, 0, 0};
static bool mem_monitor_initialized = false;
static uint32_t total_heap_size = 0;

void memoryMonitorInit() {
  Serial.println("[MEM] Initializing...");
  total_heap_size = ESP.getHeapSize();
  mem_stats.current_free = ESP.getFreeHeap();
  mem_stats.minimum_free = mem_stats.current_free;
  mem_stats.maximum_used = 0;
  mem_stats.allocations_count = 0;
  mem_stats.deallocations_count = 0;
  mem_stats.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  mem_stats.sample_count = 0;
  mem_monitor_initialized = true;
  // FIX: Cast for printf
  Serial.printf("[MEM] [OK] Heap: %lu bytes\n", (unsigned long)total_heap_size);
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
}

memory_stats_t* memoryMonitorGetStats() { return &mem_stats; }
uint32_t memoryMonitorGetFreeHeap() { return ESP.getFreeHeap(); }
uint32_t memoryMonitorGetMinFreeHeap() { return mem_stats.minimum_free; }
bool memoryMonitorIsCriticallyLow(uint32_t threshold) { return (ESP.getFreeHeap() < threshold); }

void memoryMonitorPrintStats() {
  Serial.println("\n=== MEMORY DIAGNOSTICS ===");
  uint32_t free = ESP.getFreeHeap();
  uint32_t used = total_heap_size - free;
  uint8_t percent = (used * 100) / total_heap_size;
  
  // FIX: Cast all uint32_t to unsigned long for %lu
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
}

void memoryMonitorResetMinimum() {
  mem_stats.minimum_free = ESP.getFreeHeap();
  Serial.println("[MEM] [OK] Stats reset");
}

uint8_t memoryMonitorGetUsagePercent() {
  uint32_t used = total_heap_size - ESP.getFreeHeap();
  return (uint8_t)((used * 100) / total_heap_size);
}
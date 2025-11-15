#include "memory_monitor.h"
#include <esp_heap_caps.h>

// ============================================================================
// MEMORY MONITORING STATE
// ============================================================================

static memory_stats_t mem_stats = {0};
static bool mem_monitor_initialized = false;
static uint32_t total_heap_size = 0;

// ============================================================================
// MEMORY MONITORING IMPLEMENTATION
// ============================================================================

void memoryMonitorInit() {
  Serial.println("[MEM] Initializing memory monitor...");
  
  // Get total heap size
  total_heap_size = ESP.getHeapSize();
  
  // Initialize stats
  mem_stats.current_free = ESP.getFreeHeap();
  mem_stats.minimum_free = mem_stats.current_free;
  mem_stats.maximum_used = 0;
  mem_stats.allocations_count = 0;
  mem_stats.deallocations_count = 0;
  mem_stats.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  mem_stats.sample_count = 0;
  
  mem_monitor_initialized = true;
  
  Serial.print("[MEM] ✅ Total heap: ");
  Serial.print(total_heap_size);
  Serial.print(" bytes, Initial free: ");
  Serial.print(mem_stats.current_free);
  Serial.println(" bytes");
}

void memoryMonitorUpdate() {
  if (!mem_monitor_initialized) {
    memoryMonitorInit();
    return;
  }
  
  uint32_t current_free = ESP.getFreeHeap();
  uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  
  // Update current free
  mem_stats.current_free = current_free;
  
  // Update minimum free (tracks lowest point)
  if (current_free < mem_stats.minimum_free) {
    mem_stats.minimum_free = current_free;
  }
  
  // Update maximum used
  uint32_t used = total_heap_size - current_free;
  if (used > mem_stats.maximum_used) {
    mem_stats.maximum_used = used;
  }
  
  // Update largest block
  mem_stats.largest_block = largest_block;
  
  // Increment sample count
  mem_stats.sample_count++;
}

memory_stats_t* memoryMonitorGetStats() {
  return &mem_stats;
}

uint32_t memoryMonitorGetFreeHeap() {
  return ESP.getFreeHeap();
}

uint32_t memoryMonitorGetMinFreeHeap() {
  return mem_stats.minimum_free;
}

bool memoryMonitorIsCriticallyLow(uint32_t threshold) {
  return (ESP.getFreeHeap() < threshold);
}

void memoryMonitorPrintStats() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║            MEMORY USAGE DIAGNOSTICS                       ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  uint32_t free = ESP.getFreeHeap();
  uint32_t used = total_heap_size - free;
  uint8_t percent = (used * 100) / total_heap_size;
  
  Serial.print("[MEM] Total Heap: ");
  Serial.print(total_heap_size);
  Serial.println(" bytes");
  
  Serial.print("[MEM] Current Free: ");
  Serial.print(free);
  Serial.print(" bytes (");
  Serial.print(percent);
  Serial.println("% used)");
  
  Serial.print("[MEM] Minimum Free: ");
  Serial.print(mem_stats.minimum_free);
  Serial.println(" bytes");
  
  Serial.print("[MEM] Maximum Used: ");
  Serial.print(mem_stats.maximum_used);
  Serial.println(" bytes");
  
  Serial.print("[MEM] Largest Free Block: ");
  Serial.print(mem_stats.largest_block);
  Serial.println(" bytes");
  
  Serial.print("[MEM] Samples Collected: ");
  Serial.println(mem_stats.sample_count);
  
  // Memory health status
  if (free > (total_heap_size / 2)) {
    Serial.println("[MEM] ✅ Memory status: GOOD");
  } else if (free > (total_heap_size / 4)) {
    Serial.println("[MEM] ⚠️  Memory status: WARNING - Memory usage high");
  } else {
    Serial.println("[MEM] ❌ Memory status: CRITICAL - Memory critically low!");
  }
  
  Serial.println();
}

void memoryMonitorResetMinimum() {
  mem_stats.minimum_free = ESP.getFreeHeap();
  Serial.println("[MEM] ✅ Minimum free heap counter reset");
}

uint8_t memoryMonitorGetUsagePercent() {
  uint32_t used = total_heap_size - ESP.getFreeHeap();
  return (uint8_t)((used * 100) / total_heap_size);
}

#include "memory_monitor.h"
#include "serial_logger.h"
#include "system_events.h"
#include "load_manager.h" // PHASE 14
#include <esp_heap_caps.h>
#include "system_utils.h" // PHASE 8.1

// FIX: Fully initialize struct to suppress warnings
static memory_stats_t mem_stats = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static bool mem_monitor_initialized = false;
static uint32_t total_heap_size = 0;

// PHASE 5.10: Track low memory state for event signaling
static bool low_memory_event_active = false;

void memoryMonitorInit() {
  logModuleInit("MEM");
  total_heap_size = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  mem_stats.current_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  mem_stats.minimum_free = mem_stats.current_free;
  mem_stats.maximum_used = 0;
  mem_stats.allocations_count = 0;
  mem_stats.deallocations_count = 0;
  mem_stats.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  mem_stats.sample_count = 0;
  
  // PSRAM detection
  mem_stats.psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (mem_stats.psram_total > 0) {
    mem_stats.psram_current_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mem_stats.psram_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    logInfo("[MEM] [OK] Internal: %lu bytes, PSRAM: %lu bytes", 
            (unsigned long)total_heap_size, (unsigned long)mem_stats.psram_total);
  } else {
    logInfo("[MEM] [OK] Internal: %lu bytes, PSRAM: NOT FOUND", (unsigned long)total_heap_size);
  }
  
  mem_monitor_initialized = true;
}

void memoryMonitorUpdate() {
  if (!mem_monitor_initialized) { memoryMonitorInit(); return; }

  uint32_t current_free = ESP.getFreeHeap();
  
  // PHASE 14: Optimize expensive heap scanning
  // Only check largest block every 1s (Normal) or 5s (Load) to save CPU cycles
  static uint32_t last_block_check = 0;
  uint32_t check_interval = loadManagerIsUnderLoad() ? 5000 : 1000;
  uint32_t now = millis();
  
  if (now - last_block_check >= check_interval) {
    mem_stats.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (mem_stats.psram_total > 0) {
      mem_stats.psram_current_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      mem_stats.psram_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
    last_block_check = now;
  }

  mem_stats.current_free = current_free;
  if (current_free < mem_stats.minimum_free) mem_stats.minimum_free = current_free;

  uint32_t used = total_heap_size - current_free;
  if (used > mem_stats.maximum_used) mem_stats.maximum_used = used;

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
  logPrintln("\n=== MEMORY DIAGNOSTICS ===");
  uint32_t free = ESP.getFreeHeap();
  uint32_t used = total_heap_size - free;
  uint8_t percent = (used * 100) / total_heap_size;
  
  logPrintf("Total Heap:   %lu\n", (unsigned long)total_heap_size);
  logPrintf("Current Free: %lu (%u%% used)\n", (unsigned long)free, percent);
  logPrintf("Min Free:     %lu\n", (unsigned long)mem_stats.minimum_free);
  logPrintf("Max Used:     %lu\n", (unsigned long)mem_stats.maximum_used);
  logPrintf("Largest Blk:  %lu\n", (unsigned long)mem_stats.largest_block);
  
  if (mem_stats.psram_total > 0) {
    logPrintf("PSRAM Total:  %lu\n", (unsigned long)mem_stats.psram_total);
    logPrintf("PSRAM Free:   %lu\n", (unsigned long)mem_stats.psram_current_free);
    logPrintf("PSRAM Large:  %lu\n", (unsigned long)mem_stats.psram_largest_block);
  }
  
  logPrintf("Samples:      %lu\n", (unsigned long)mem_stats.sample_count);
  
  if (free > (total_heap_size / 2)) logPrintln("Status: [GOOD]");
  else if (free > (total_heap_size / 4)) logPrintln("Status: [WARN]");
  else logPrintln("Status: [CRITICAL]");
  logPrintln("");
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

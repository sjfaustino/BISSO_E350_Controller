#include "task_manager.h"
#include "memory_monitor.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "config_unified.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The external task function body
void taskMonitorFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[MONITOR_TASK] Started on core 1");
  watchdogTaskAdd("Monitor");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Monitor");
  
  memoryMonitorInit();
  
  while (1) {
    uint32_t task_start = millis();
    
    // 1. Memory and System Monitoring
    memoryMonitorUpdate();
    
    if (memoryMonitorIsCriticallyLow(MEMORY_CRITICAL_THRESHOLD_BYTES)) {
      faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "Memory critically low!");
    }
    
    // 2. Configuration Flusher
    if (taskLockMutex(taskGetConfigMutex(), 10)) {
        configUnifiedFlush(); 
        taskUnlockMutex(taskGetConfigMutex());
    } else {
        logWarning("[MONITOR_TASK] Mutex timeout, skipped config flush.");
    }
    
    // 3. Task Execution Time Monitoring (FIX: Using Accessors)
    int stats_count = taskGetStatsCount();
    task_stats_t* stats_array = taskGetStatsArray(); // Get the pointer to the shared array

    for (int i = 0; i < stats_count; i++) {
        // Check if the last recorded run time exceeded the warning threshold
        if (stats_array[i].last_run_time_ms > TASK_EXECUTION_WARNING_MS) {
            
            logWarning("[MONITOR] Task '%s' slow: %lu ms (Max: %lu ms)",
                       stats_array[i].name, 
                       stats_array[i].last_run_time_ms, 
                       TASK_EXECUTION_WARNING_MS);
            
            faultLogWarning(FAULT_TASK_HUNG, "Task execution time exceeded warning threshold");
        }
    }
    
    // Feed watchdog
    watchdogFeed("Monitor");
    
    // Update stats (Implicitly handled by task_manager.cpp structure)

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MONITOR));
  }
}
/**
 * @file tasks_monitor.cpp
 * @brief System Health Monitor Task (Gemini v3.5.0)
 * @details Handles background maintenance: Memory checks, Config flushing, and Web Telemetry.
 * @author Sergio Faustino
 */

#include "task_manager.h"
#include "memory_monitor.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "task_stall_detection.h"  // PHASE 2.5: Automatic task stall detection
#include "system_constants.h"
#include "fault_logging.h"
#include "config_unified.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskMonitorFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[MONITOR_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Monitor");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Monitor");

  memoryMonitorInit();
  taskStallDetectionInit();  // PHASE 2.5: Initialize task stall detection
  
  while (1) {
    // 1. Memory Integrity Check
    memoryMonitorUpdate();
    if (memoryMonitorIsCriticallyLow(MEMORY_CRITICAL_THRESHOLD_BYTES)) {
      faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "System Memory Critical");
      logError("[MONITOR] [CRITICAL] Low Heap: %lu bytes", (unsigned long)memoryMonitorGetFreeHeap());
    }
    
    // 2. Lazy Configuration Flush
    // If settings were changed (dirty flag set), write them to NVS now to avoid stalling the main loop.
    if (taskLockMutex(taskGetConfigMutex(), 10)) {
        configUnifiedFlush(); 
        taskUnlockMutex(taskGetConfigMutex());
    }
    
    // 3. Task Stall Detection
    // PHASE 2.5: Check for task stalls and log warnings
    taskStallDetectionUpdate();

    // 4. Task Health Analysis
    // Scans all registered tasks for stack overflows or execution starvation.
    int stats_count = taskGetStatsCount();
    task_stats_t* stats_array = taskGetStatsArray();

    for (int i = 0; i < stats_count; i++) {
        // Starvation Check
        if (stats_array[i].last_run_time_ms > TASK_EXECUTION_WARNING_MS) {
            logWarning("[MONITOR] [WARN] Task '%s' is slow: %lu ms",
                       stats_array[i].name, (unsigned long)stats_array[i].last_run_time_ms);
        }
        
        // Stack Depth Check (Watermark)
        if (stats_array[i].handle != NULL) {
            UBaseType_t high_water = uxTaskGetStackHighWaterMark(stats_array[i].handle);
            if (high_water < 50) { // < 200 bytes safe margin
                 logError("[MONITOR] [CRIT] Stack near overflow: %s (%lu words free)", 
                          stats_array[i].name, (unsigned long)high_water);
            }
        }
    }

    // PHASE 5.4: Telemetry collection moved to dedicated Core 0 task
    // Monitor task on Core 1 now focuses only on critical memory/config checks

    // 8. Watchdog & Sleep
    watchdogFeed("Monitor");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MONITOR));
  }
}
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
#include "plc_iface.h"              // PERFORMANCE FIX: I2C health monitoring
#include "motion.h"                 // PERFORMANCE FIX: For motionEmergencyStop()
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

    // 3.5. I2C Health Check (moved from motion loop for performance)
    // PERFORMANCE FIX: Check PLC I2C communication health at 1Hz instead of 100Hz
    // This was previously in motion_control.cpp running every 10ms, causing real-time delays
    static uint32_t last_i2c_check_ms = 0;
    static uint32_t last_timeout_count = 0;
    if (millis() - last_i2c_check_ms > 1000) {
      if (elboIsShadowRegisterDirty()) {
        logError("[MONITOR] CRITICAL: PLC I2C communication failure - shadow register dirty!");
        faultLogCritical(FAULT_I2C_ERROR, "PLC I2C failure detected - emergency stop");
        motionEmergencyStop();
      }

      uint32_t current_timeout_count = elboGetMutexTimeoutCount();
      if (current_timeout_count > last_timeout_count + 10) {
        logError("[MONITOR] CRITICAL: PLC mutex timeout escalation (%lu timeouts)",
                 (unsigned long)(current_timeout_count - last_timeout_count));
        faultLogCritical(FAULT_I2C_ERROR, "PLC mutex timeout threshold exceeded");
        motionEmergencyStop();
      }
      last_timeout_count = current_timeout_count;
      last_i2c_check_ms = millis();
    }

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

        // SECURITY FIX: Enhanced stack overflow monitoring with critical fault logging
        // Stack sizes are typically 2048 words (8192 bytes)
        // Thresholds: CRITICAL < 128 words (6.25%), WARNING < 256 words (12.5%)
        if (stats_array[i].handle != NULL) {
            UBaseType_t high_water = uxTaskGetStackHighWaterMark(stats_array[i].handle);

            if (high_water < 128) {  // < 512 bytes remaining - CRITICAL
                faultLogEntry(FAULT_CRITICAL, FAULT_CRITICAL_SYSTEM_ERROR, i, high_water,
                             "CRITICAL: Stack near overflow in task '%s' (%lu words free)",
                             stats_array[i].name, (unsigned long)high_water);
                logError("[MONITOR] [CRITICAL] Stack overflow imminent: %s (%lu words / %lu bytes free)",
                         stats_array[i].name, (unsigned long)high_water, (unsigned long)(high_water * 4));
            }
            else if (high_water < 256) {  // < 1024 bytes remaining - WARNING
                faultLogEntry(FAULT_WARNING, FAULT_CRITICAL_SYSTEM_ERROR, i, high_water,
                             "WARNING: Low stack space in task '%s' (%lu words free)",
                             stats_array[i].name, (unsigned long)high_water);
                logWarning("[MONITOR] [WARN] Low stack: %s (%lu words / %lu bytes free)",
                          stats_array[i].name, (unsigned long)high_water, (unsigned long)(high_water * 4));
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
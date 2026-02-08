/**
 * @file tasks_monitor.cpp
 * @brief System Health Monitor Task (PosiPro)
 * @details Handles background maintenance: Memory checks, Config flushing, and Web Telemetry.
 * @author Sergio Faustino
 */

#include "task_manager.h"
#include "memory_monitor.h"
#include "serial_logger.h"
#include "system_tuning.h"          // MAINTAINABILITY FIX: Centralized tuning parameters
#include "watchdog_manager.h"
#include "task_stall_detection.h"  // PHASE 2.5: Automatic task stall detection
#include "system_constants.h"
#include "fault_logging.h"
#include "config_unified.h"
#include "plc_iface.h"              // PERFORMANCE FIX: I2C health monitoring
#include "motion.h"                 // PERFORMANCE FIX: For motionEmergencyStop()
#include "i2c_bus_recovery.h"       // ROBUSTNESS FIX: I2C bus recovery before E-STOP
#include "load_manager.h"           // PHASE 5.3: For loadManagerUpdate()
#include "rs485_device_registry.h"  // RS485 Watchdog
#include "oled_dashboard.h"         // Local OLED Dashboard
#include "sd_telemetry_logger.h"    // Background SD Logging
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskMonitorFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[MONITOR_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Monitor");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Monitor");

  memoryMonitorInit();
  taskStallDetectionInit();  // PHASE 2.5: Initialize task stall detection
  
  // Initialize Local OLED Dashboard for v3.1
  oledDashboardInit();
  
  // Initialize SD Telemetry Logger (Black Box)
  sdTelemetryLoggerInit();
  
  while (1) {
    // 1. Memory Integrity Check
    memoryMonitorUpdate();
    if (memoryMonitorIsCriticallyLow(MEMORY_CRITICAL_THRESHOLD_BYTES)) {
      faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "System Memory Critical");
      logError("[MONITOR] [CRITICAL] Low Heap: %lu bytes", (unsigned long)memoryMonitorGetFreeHeap());
    }
    
    // 2. Lazy Configuration Flush
    // If settings were changed (dirty flag set), write them to NVS now to avoid stalling the main loop.
    if (taskLockMutex((SemaphoreHandle_t)configGetMutex(), 10)) {
        configUnifiedFlush(); 
        taskUnlockMutex((SemaphoreHandle_t)configGetMutex());
    }
    
    // 3. Task Stall Detection
    // PHASE 2.5: Check for task stalls and log warnings
    taskStallDetectionUpdate();

    // 3.7. Load & Resource Management (PHASE 5.3/6.1)
    // Update system load state and calculate task stack usage statistics
    loadManagerUpdate();
    taskUpdateStackUsage();

    // 3.5. I2C Health Check (moved from motion loop for performance)
    // PERFORMANCE FIX: Check PLC I2C communication health at 1Hz instead of 100Hz
    // This was previously in motion_control.cpp running every 10ms, causing real-time delays
    static uint32_t last_i2c_check_ms = 0;
    static uint32_t last_timeout_count = 0;
    static uint8_t persistent_failure_count = 0;  // Track hardware missing
    static bool i2c_hardware_disabled = false;    // Flag to disable I2C checks when no hardware
    static bool checked_hardware_at_boot = false; // One-time check
    
    // Skip I2C monitoring entirely if hardware was not detected at boot
    if (!checked_hardware_at_boot) {
      checked_hardware_at_boot = true;
      if (!plcIsHardwarePresent()) {
        logInfo("[MONITOR] PLC I2C hardware not present at boot - disabling I2C monitoring");
        i2c_hardware_disabled = true;
      }
    }
    
    if (!i2c_hardware_disabled && millis() - last_i2c_check_ms > 1000) {
      // ROBUSTNESS FIX: 3-retry I2C bus recovery before escalating to E-STOP
      // Single I2C glitches should not cause permanent emergency stop
      if (elboIsShadowRegisterDirty()) {
        bool recovery_success = false;

        // Attempt recovery up to 3 times with exponential backoff
        for (int retry = 0; retry < 3; retry++) {
          logWarning("[MONITOR] I2C shadow register dirty - attempting recovery %d/3", retry + 1);

          // Feed watchdog BEFORE potentially blocking I2C operations
          watchdogFeed("Monitor");
          
          // Attempt bus recovery
          i2cRecoverBus();

          // Wait with exponential backoff: 50ms, 100ms, 200ms
          uint32_t backoff_ms = 50 << retry;
          vTaskDelay(pdMS_TO_TICKS(backoff_ms));
          
          // Feed watchdog after delay
          watchdogFeed("Monitor");

          // Check if recovery succeeded
          if (!elboIsShadowRegisterDirty()) {
            recovery_success = true;
            persistent_failure_count = 0;  // Reset counter on success
            logInfo("[MONITOR] [OK] I2C bus recovery successful on attempt %d/3", retry + 1);
            faultLogWarning(FAULT_I2C_ERROR, "I2C bus recovered after retry");
            break;
          }
        }

        // All recovery attempts failed
        if (!recovery_success) {
          persistent_failure_count++;
          
          // After 3 consecutive full-failure cycles, assume hardware is missing
          if (persistent_failure_count >= 3) {
            logWarning("[MONITOR] I2C hardware not present - disabling I2C monitoring");
            i2c_hardware_disabled = true;
            // Don't trigger E-STOP when hardware is simply not connected
          } else {
            logError("[MONITOR] CRITICAL: PLC I2C failure - all 3 recovery attempts exhausted");
            faultLogCritical(FAULT_I2C_ERROR, "PLC I2C failure after 3 recovery attempts - emergency stop");
            motionEmergencyStop();
          }
        }
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
    
    // 3.9. RS485 Bus Health Check
    if (rs485CheckWatchdog()) {
      // Watchdog alert active - logs within rs485CheckWatchdog() only first time
    }

    // Update Local OLED Dashboard (DRO and System Info)
    static uint32_t last_oled_update = 0;
    if (millis() - last_oled_update > 500) { // 2Hz refresh for OLED
        oledDashboardUpdate();
        last_oled_update = millis();
    }

    // Update SD Telemetry Logger (1Hz Black Box recording)
    static uint32_t last_sd_log_ms = 0;
    if (millis() - last_sd_log_ms > 1000) {
        sdTelemetryLoggerUpdate();
        last_sd_log_ms = millis();
    }

    // 4. Task Health Analysis
    // Scans all registered tasks for stack overflows or execution starvation.
    int stats_count = taskGetStatsCount();
    task_stats_t* stats_array = taskGetStatsArray();

    static uint32_t last_health_log_ms = 0;
    bool should_log_health = (millis() - last_health_log_ms > 5000);

    for (int i = 0; i < stats_count; i++) {
        // Starvation Check
        if (stats_array[i].last_run_time_ms > TASK_EXECUTION_WARNING_MS) {
            if (should_log_health) {
                logWarning("[MONITOR] [WARN] Task '%s' is slow: %lu ms",
                           stats_array[i].name, (unsigned long)stats_array[i].last_run_time_ms);
            }
        }

        // SECURITY FIX: Stack monitoring is now handled globally by taskUpdateStackUsage()
        // and reported via telemetry. The Monitor task still logs critical alerts.
        if (stats_array[i].handle != nullptr) {
            uint16_t high_water = stats_array[i].stack_high_water;

            if (high_water < STACK_CRITICAL_THRESHOLD_WORDS * 4) {  // Convert words to bytes
                if (should_log_health) {
                    faultLogEntry(FAULT_CRITICAL, FAULT_CRITICAL_SYSTEM_ERROR, i, high_water,
                                 "CRITICAL: Stack near overflow in task '%s' (%u bytes free)",
                                 stats_array[i].name, high_water);
                    logError("[MONITOR] [CRITICAL] Stack overflow imminent: %s (%u bytes free)",
                             stats_array[i].name, high_water);
                }
            }
            else if (high_water < STACK_WARNING_THRESHOLD_WORDS * 4) {
                if (should_log_health) {
                    faultLogEntry(FAULT_WARNING, FAULT_CRITICAL_SYSTEM_ERROR, i, high_water,
                                 "WARNING: Low stack space in task '%s' (%u bytes free)",
                                 stats_array[i].name, high_water);
                    logWarning("[MONITOR] [WARN] Low stack: %s (%u bytes free)",
                              stats_array[i].name, high_water);
                }
            }
        }
    }
    if (should_log_health) {
        last_health_log_ms = millis();
    }

    // PHASE 5.4: Telemetry collection moved to dedicated Core 0 task
    // Monitor task on Core 1 now focuses only on critical memory/config checks

    // 8. Watchdog & Sleep
    watchdogFeed("Monitor");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MONITOR));
  }
}

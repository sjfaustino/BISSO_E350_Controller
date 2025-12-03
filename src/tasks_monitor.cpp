#include "task_manager.h"
#include "memory_monitor.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "config_unified.h"
#include "web_server.h" // <-- FIX: Added Include
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// External Web Server instance
extern WebServerManager webServer;
// External Motion Getter
extern int32_t motionGetPosition(uint8_t axis);
extern bool motionIsMoving();
extern bool motionIsEmergencyStopped();
extern bool safetyIsAlarmed();

void taskMonitorFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[MONITOR_TASK] Started on core 1");
  watchdogTaskAdd("Monitor");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Monitor");
  
  memoryMonitorInit();
  
  while (1) {
    uint32_t task_start = millis();
    
    // 1. Memory Monitoring
    memoryMonitorUpdate();
    
    if (memoryMonitorIsCriticallyLow(MEMORY_CRITICAL_THRESHOLD_BYTES)) {
      faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "Memory critically low!");
      logError("[MONITOR] [CRITICAL] Low Memory Warning");
    }
    
    // 2. Config Flush
    if (taskLockMutex(taskGetConfigMutex(), 10)) {
        configUnifiedFlush(); 
        taskUnlockMutex(taskGetConfigMutex());
    }
    
    // 3. Task Execution Analysis
    int stats_count = taskGetStatsCount();
    task_stats_t* stats_array = taskGetStatsArray();

    for (int i = 0; i < stats_count; i++) {
        if (stats_array[i].last_run_time_ms > TASK_EXECUTION_WARNING_MS) {
            logWarning("[MONITOR] [WARN] Task '%s' slow: %lu ms",
                       stats_array[i].name, stats_array[i].last_run_time_ms);
        }
    }
    
    // 4. Telemetry Update
    webServer.setAxisPosition('X', motionGetPosition(0) / (float)MOTION_POSITION_SCALE_FACTOR);
    webServer.setAxisPosition('Y', motionGetPosition(1) / (float)MOTION_POSITION_SCALE_FACTOR);
    webServer.setAxisPosition('Z', motionGetPosition(2) / (float)MOTION_POSITION_SCALE_FACTOR);
    webServer.setAxisPosition('A', motionGetPosition(3) / (float)MOTION_POSITION_SCALE_FACTOR);

    webServer.setSystemUptime(taskGetUptime());
    
    const char* status_message = "READY";
    if (motionIsEmergencyStopped()) status_message = "E-STOP";
    else if (safetyIsAlarmed()) status_message = "ALARMED";
    else if (motionIsMoving()) status_message = "MOVING";
    
    webServer.setSystemStatus(status_message);
    
    watchdogFeed("Monitor");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MONITOR));
  }
}
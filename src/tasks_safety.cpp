#include "task_manager.h"
#include "safety.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h" // NEW: Include for contextual logging
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

// The external task function body
void taskSafetyFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[SAFETY_TASK] Started on core 1");
  watchdogTaskAdd("Safety");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Safety");

  while (1) {
    uint32_t task_start = millis();
    
    // Critical safety operations
    safetyUpdate();
    
    // Check for safety messages
    queue_message_t msg;
    while (taskReceiveMessage(taskGetSafetyQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_SAFETY_ESTOP_REQUESTED:
          // Use full fault API to log the critical event
          faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 0, 
                        "E-STOP requested via Task Queue (Source: %lu)", msg.param1);
          break;
        case MSG_SAFETY_ALARM_TRIGGERED:
          logError("[SAFETY_TASK] Alarm triggered!");
          // The safety module already logs this, but we can log the queue event:
          faultLogEntry(FAULT_WARNING, FAULT_SAFETY_INTERLOCK, -1, 0, 
                        "Safety Alarm triggered via Queue (Type: %lu)", msg.param1);
          break;
        default:
          break;
      }
    }
    
    watchdogFeed("Safety");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_SAFETY));
  }
}
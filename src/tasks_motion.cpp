#include "task_manager.h"
#include "motion.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

// The external task function body
void taskMotionFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[MOTION_TASK] Started on core 1");
  watchdogTaskAdd("Motion");
  
  // FIX: Use xTaskGetCurrentTaskHandle() to get and subscribe the handle
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Motion");

  while (1) {
    uint32_t task_start = millis();
    
    // Core motion operations
    motionUpdate();
    
    // Check for motion commands
    queue_message_t msg;
    while (taskReceiveMessage(taskGetMotionQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_MOTION_START:
          logInfo("[MOTION_TASK] Start command received");
          break;
        case MSG_MOTION_STOP:
          logInfo("[MOTION_TASK] Stop command received");
          break;
        case MSG_MOTION_EMERGENCY_HALT:
          logError("[MOTION_TASK] Emergency halt!");
          break;
        default:
          break;
      }
    }
    
    // Update stats
    
    watchdogFeed("Motion");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MOTION));
  }
}
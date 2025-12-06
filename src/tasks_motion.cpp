#include "task_manager.h"
#include "motion.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

void taskMotionFunction(void* parameter) {
  // 100Hz Motion Loop
  const TickType_t xPeriod = pdMS_TO_TICKS(TASK_PERIOD_MOTION);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  logInfo("[MOTION_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Motion");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Motion");

  while (1) {
    uint32_t task_start = millis();
    
    // 1. Core Motion Logic
    motionUpdate();
    
    // 2. Queue Processing (Legacy / Info Messages)
    queue_message_t msg;
    while (taskReceiveMessage(taskGetMotionQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_MOTION_START: logInfo("[MOTION_TASK] Start received"); break;
        case MSG_MOTION_STOP:  logInfo("[MOTION_TASK] Stop received"); break;
        case MSG_MOTION_EMERGENCY_HALT: logError("[MOTION_TASK] Emergency Halt"); break;
        default: break;
      }
    }
    
    watchdogFeed("Motion");
    
    // 3. Hybrid Wait: Periodic + Event Driven
    // Calculate remaining time in the 10ms slot
    TickType_t xNow = xTaskGetTickCount();
    xLastWakeTime += xPeriod; // Target next wake
    
    // Use Notification Wait instead of Delay to allow "Kick" from other tasks
    // If notified (e.g. E-Stop/Move), we wake immediately.
    // If not, we wake at the 10ms mark.
    if (xLastWakeTime > xNow) {
        ulTaskNotifyTake(pdTRUE, xLastWakeTime - xNow);
    } else {
        // Overrun, yield briefly
        xLastWakeTime = xNow; 
        vTaskDelay(1);
    }
  }
}
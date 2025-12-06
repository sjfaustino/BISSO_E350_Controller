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
    // Core motion operations
    motionUpdate();
    
    // Check for motion commands
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
    
    // Hybrid Wait: Periodic + Event Driven
    TickType_t xNow = xTaskGetTickCount();
    xLastWakeTime += xPeriod;
    
    if (xLastWakeTime > xNow) {
        ulTaskNotifyTake(pdTRUE, xLastWakeTime - xNow);
    } else {
        xLastWakeTime = xNow; 
        vTaskDelay(1);
    }
  }
}
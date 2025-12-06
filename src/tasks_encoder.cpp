#include "task_manager.h"
#include "encoder_wj66.h"
#include "encoder_motion_integration.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskEncoderFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[ENCODER_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Encoder");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Encoder"); 

  while (1) {
    // Encoder operations (20ms = 50 Hz)
    wj66Update();
    encoderMotionUpdate(); 

    watchdogFeed("Encoder");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_ENCODER));
  }
}
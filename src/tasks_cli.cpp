#include "task_manager.h"
#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The external task function body
void taskCliFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[CLI_TASK] Started on core 0");
  watchdogTaskAdd("CLI");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "CLI");

  while (1) {
    uint32_t task_start = millis();
    
    // CLI processing (100ms = 10 Hz)
    cliUpdate();
    
    // Update stats
    
    watchdogFeed("CLI");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_CLI));
  }
}
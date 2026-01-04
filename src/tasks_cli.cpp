#include "task_manager.h"
#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskCliFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[CLI_TASK] [OK] Started on core 0");
  watchdogTaskAdd("CLI");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "CLI");

  while (1) {
    cliUpdate();
    watchdogFeed("CLI");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_CLI));
  }
}

/**
 * @file tasks_plc.cpp
 * @brief PLC Interface Task
 * @details In v3.3+, the ELBO driver is synchronous (direct I2C).
 * This task is kept for monitoring or polling if needed, but is largely idle.
 */

#include "task_manager.h"
#include "plc_iface.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include <Arduino.h>

void taskPlcCommFunction(void* parameter) {
  logInfo("[PLC_TASK] Started on Core 1");
  watchdogTaskAdd("PLC");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "PLC");

  TickType_t last_wake = xTaskGetTickCount();

  while (1) {
    // In v3.3, motion logic drives the PLC directly via elboSet... functions.
    // We can use this task to monitor input states periodically if needed for telemetry,
    // or just feed the watchdog.
    
    // Optional: Periodically read inputs to keep cache fresh if not moving?
    // For now, it just keeps the task alive.
    
    watchdogFeed("PLC");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_PLC_COMM));
  }
}
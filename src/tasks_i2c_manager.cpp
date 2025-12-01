#include "task_manager.h"
#include "i2c_bus_recovery.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h" // NEW: Include for contextual logging
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The external task function body
void taskI2cManagerFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[I2C_TASK] Started on core 1");
  watchdogTaskAdd("I2C_Manager");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "I2C_Manager");

  while (1) {
    uint32_t task_start = millis();
    
    // I2C management (50ms = 20 Hz)
    if (taskLockMutex(taskGetI2cMutex(), 10)) {
      i2cMonitorBusHealth(); // Monitor bus health for errors (uses faultLogEntry internally)
      taskUnlockMutex(taskGetI2cMutex());
    } else {
      logWarning("[I2C_TASK] Mutex timeout, skipped bus health check.");
      // Use full fault API to log the contention/timeout
      faultLogEntry(FAULT_WARNING, FAULT_TASK_HUNG, -1, 10, 
                    "I2C Mutex contention timeout (%dms)", 10);
    }
    
    watchdogFeed("I2C_Manager");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_I2C_MANAGER));
  }
}
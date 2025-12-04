#include "task_manager.h"
#include "plc_iface.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h" 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskPlcCommFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[PLC_TASK] [OK] Started on core 1");
  watchdogTaskAdd("PLC_Comm");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "PLC_Comm");

  while (1) {
    // PLC communication (50ms)
    if (taskLockMutex(taskGetI2cMutex(), 10)) {
      plcIfaceUpdate(); 
      taskUnlockMutex(taskGetI2cMutex());
    } else {
      logWarning("[PLC_TASK] [WARN] Mutex timeout, skipped read.");
      faultLogEntry(FAULT_WARNING, FAULT_TASK_HUNG, -1, 10, "PLC Mutex Timeout");
    }
    
    watchdogFeed("PLC_Comm");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_PLC_COMM));
  }
}
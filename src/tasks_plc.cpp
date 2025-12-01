#include "task_manager.h"
#include "plc_iface.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h" // NEW: Include for contextual logging
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The external task function body
void taskPlcCommFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[PLC_TASK] Started on core 1");
  watchdogTaskAdd("PLC_Comm");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "PLC_Comm");

  while (1) {
    uint32_t task_start = millis();
    
    // PLC communication (50ms = 20 Hz)
    if (taskLockMutex(taskGetI2cMutex(), 10)) {
      plcIfaceUpdate(); // Read consensus from PLC (uses faultLogEntry internally)
      taskUnlockMutex(taskGetI2cMutex());
    } else {
      logWarning("[PLC_TASK] Mutex timeout, skipped PLC I/O read.");
      // Use full fault API to log the contention/timeout
      faultLogEntry(FAULT_WARNING, FAULT_TASK_HUNG, -1, 10, 
                    "PLC Comm Mutex contention timeout (%dms)", 10);
    }
    
    watchdogFeed("PLC_Comm");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_PLC_COMM));
  }
}
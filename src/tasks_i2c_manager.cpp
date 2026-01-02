#include "fault_logging.h"
#include "i2c_bus_recovery.h"
#include "plc_iface.h"  // For plcIsHardwarePresent()
#include "serial_logger.h"
#include "system_constants.h"
#include "task_manager.h"
#include "watchdog_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


void taskI2cManagerFunction(void *parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[I2C_TASK] [OK] Started on core 1");
  watchdogTaskAdd("I2C_Manager");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "I2C_Manager");

  // Check once at startup if hardware is present
  bool hardware_present = plcIsHardwarePresent();
  if (!hardware_present) {
    logInfo("[I2C_TASK] PLC hardware not present - I2C monitoring disabled");
  }

  while (1) {
    // Skip I2C operations if hardware was not detected at boot
    if (hardware_present) {
      // PHASE 2.5: Monitor bus health with adaptive timeout
      // Scaling prevents spurious timeouts under high system load
      uint32_t bus_timeout = taskGetAdaptiveI2cTimeout();
      if (taskLockMutex(taskGetI2cMutex(), bus_timeout)) {
        i2cMonitorBusHealth();
        taskUnlockMutex(taskGetI2cMutex());
      } else {
        static uint32_t last_log = 0;
        if (millis() - last_log > 5000) {
          logWarning("[I2C_TASK] [WARN] Mutex timeout.");
          faultLogEntry(FAULT_WARNING, FAULT_TASK_HUNG, -1, bus_timeout,
                        "I2C Mutex Timeout");
          last_log = millis();
        }
      }
    }

    watchdogFeed("I2C_Manager");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_I2C_MANAGER));
  }
}
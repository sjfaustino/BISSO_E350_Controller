#include "task_manager.h"
#include "encoder_wj66.h"
#include "encoder_motion_integration.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "rs485_device_registry.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskEncoderFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[ENCODER_TASK] [OK] Started on core 1");

  // Initialize RS-485 Registry with default baud
  rs485RegistryInit(WJ66_BAUD);

  // Diagnostic: Measure stack high water mark
  UBaseType_t stack_hwm_initial = uxTaskGetStackHighWaterMark(NULL);
  logInfo("[ENCODER_TASK] Initial stack HWM: %u bytes", (unsigned int)stack_hwm_initial * 4);

  watchdogTaskAdd("Encoder");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Encoder");

  uint32_t loop_count = 0;
  while (1) {
    // RUN THE CENTRAL RS-485 BUS HANDLER
    // This handles both WJ66 and Modbus devices (JXK-10, Altivar31, YH-TC05)
    rs485HandleBus();
    
    // Position/Status updates for motion engine
    encoderMotionUpdate();

    // Periodic stack monitoring (every 100 cycles = 2 seconds)
    loop_count++;
    if (loop_count % 100 == 0) {
      UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);
      uint32_t stack_hwm_bytes = stack_hwm * sizeof(StackType_t);
      uint32_t stack_used = TASK_STACK_ENCODER - stack_hwm_bytes;
      // Only warn if less than 512 bytes free
      if (stack_hwm_bytes < 512) {
        logWarning("[ENCODER_TASK] HIGH stack usage: %lu / %d bytes (Free: %u)",
                   (unsigned long)stack_used, TASK_STACK_ENCODER, (unsigned int)stack_hwm_bytes);
      }
    }

    watchdogFeed("Encoder");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_ENCODER));
  }
}
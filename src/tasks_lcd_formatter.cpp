/**
 * @file tasks_lcd_formatter.cpp
 * @brief LCD String Formatter Task (PHASE 5.4)
 * @details Background formatting task on Core 0 to prepare LCD strings
 * @author Sergio Faustino
 */

#include "task_manager.h"
#include "lcd_formatter.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskLcdFormatterFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[LCD_FORMATTER_TASK] [OK] Started on core 0 - Background formatting");
  watchdogTaskAdd("LCD_Formatter");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "LCD_Formatter");

  // Initialize the formatter
  lcdFormatterInit();

  while (1) {
    // Format all LCD strings with current motion state
    // This heavy snprintf work happens on Core 0, freeing Core 1 for motion control
    lcdFormatterUpdate();

    watchdogFeed("LCD_Formatter");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_LCD_FORMAT));
  }
}

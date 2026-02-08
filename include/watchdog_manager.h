#ifndef WATCHDOG_MANAGER_H
#define WATCHDOG_MANAGER_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// WATCHDOG CONFIGURATION
// ============================================================================

// TWDT (Task Watchdog Timer) timeout in seconds
#define WATCHDOG_TIMEOUT_SEC        10   // 10 second timeout (reasonable for motion control)

// IWDT (Interrupt Watchdog Timer) timeout
#define WATCHDOG_IWDT_TIMEOUT_MS    5000 // 5 second interrupt watchdog

// Enable/disable watchdog components
#define ENABLE_TASK_WDT             1    // Task watchdog (FreeRTOS tasks)
#define ENABLE_INTERRUPT_WDT        1    // Interrupt watchdog (CPU core)
#define PANIC_ON_TIMEOUT            1    // Force restart on timeout (safer)

#define WDT_MAX_TASKS               15   // Max number of monitored tasks

// ============================================================================
// WATCHDOG STATUS CODES
// ============================================================================

typedef enum {
  WDT_STATUS_OK = 0,
  WDT_STATUS_TIMEOUT = 1,
  WDT_STATUS_PANIC = 2,
  WDT_STATUS_RECOVERY_ATTEMPTED = 3,
  WDT_STATUS_DISABLED = 4,
} wdt_status_t;

// ============================================================================
// WATCHDOG RESET REASON
// ============================================================================

typedef enum {
  RESET_REASON_POWER_ON = 0,
  RESET_REASON_EXTERNAL = 1,
  RESET_REASON_SOFTWARE = 2,
  RESET_REASON_WATCHDOG_TASK = 3,      // Task WDT timeout
  RESET_REASON_WATCHDOG_INTERRUPT = 4, // Interrupt WDT timeout
  RESET_REASON_BROWN_OUT = 5,
  RESET_REASON_UNKNOWN = 6,
} reset_reason_t;

// ============================================================================
// WATCHDOG TICK TRACKING
// ============================================================================

typedef struct {
  const char* task_name;
  uint32_t last_tick;
  uint32_t tick_count;
  uint32_t missed_ticks;
  bool fed_this_cycle;
  uint8_t consecutive_misses;
} watchdog_tick_t;

// ============================================================================
// WATCHDOG STATISTICS
// ============================================================================

typedef struct {
  uint32_t timeouts_detected;
  uint32_t automatic_recoveries;
  uint32_t task_resets;
  uint32_t system_resets;
  uint32_t total_ticks;
  uint32_t missed_ticks;
  uint8_t current_status;
  reset_reason_t last_reset_reason;
  uint32_t reset_count;
  uint32_t uptime_sec;
} watchdog_stats_t;

// ============================================================================
// WATCHDOG INITIALIZATION & CONTROL
// ============================================================================

// Initialize watchdog system (call from setup)
void watchdogInit();

// Enable task watchdog feeding (call from each task)
void watchdogTaskAdd(const char* task_name);

// Feed the watchdog (task stays alive)
void watchdogFeed(const char* task_name);

// Subscribe task to watchdog monitoring
void watchdogSubscribeTask(TaskHandle_t task_handle, const char* task_name);

// Get watchdog status
wdt_status_t watchdogGetStatus();

// Check if system recovered from watchdog
bool watchdogRecoveredFromTimeout();

// ============================================================================
// WATCHDOG MONITORING & DIAGNOSTICS
// ============================================================================

// Show watchdog status
void watchdogShowStatus();

// Show all registered tasks
void watchdogShowTasks();

// Show watchdog statistics
void watchdogShowStats();

// Get last reset reason
reset_reason_t watchdogGetLastResetReason();

// Get reset count since factory reset
uint32_t watchdogGetResetCount();

// Check if specific task is alive
bool watchdogIsTaskAlive(const char* task_name);

// Get missed ticks for task
uint32_t watchdogGetMissedTicks(const char* task_name);

// ============================================================================
// WATCHDOG RECOVERY & DEBUGGING
// ============================================================================

// Manual watchdog recovery (for debugging)
void watchdogRecovery();

// Reset watchdog statistics (for testing)
void watchdogResetStats();

// Enable/disable watchdog temporarily (for debugging)
void watchdogEnable();
void watchdogDisable();

// Force a watchdog timeout (testing - DO NOT USE IN PRODUCTION)
// void watchdogForceTimeout();

// ============================================================================
// AUTOMATIC TASK FEEDING
// ============================================================================

// Called from each task to feed watchdog
// Usage: At end of task loop before delay
#define WATCHDOG_FEED(task_name) watchdogFeed(#task_name)

// ============================================================================
// WATCHDOG CALLBACK (for custom handling)
// ============================================================================

// Called when watchdog timeout detected
typedef void (*watchdog_callback_t)(const char* task_name, uint32_t timeout);
void watchdogSetCallback(watchdog_callback_t callback);

// ============================================================================
// SAFE OPERATIONS
// ============================================================================

// Critical section where watchdog is paused
void watchdogPause();
void watchdogResume();

// Watchdog-safe delay (feeds watchdog while waiting)
void watchdogDelay(uint32_t ms);

// ============================================================================
// DIAGNOSTICS UTILITIES
// ============================================================================

// Log watchdog statistics to NVS
void watchdogLogStats();

// Get watchdog statistics structure
watchdog_stats_t* watchdogGetStats();

// Print detailed watchdog report
void watchdogPrintDetailedReport();

#endif

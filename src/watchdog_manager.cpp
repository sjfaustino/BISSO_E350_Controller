#include "watchdog_manager.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <string.h>
#include "system_utils.h" // PHASE 8.1

// ============================================================================
// WATCHDOG STATE
// ============================================================================

static bool wdt_initialized = false;
static bool wdt_enabled = false;
static wdt_status_t wdt_status = WDT_STATUS_OK;
static reset_reason_t last_reset_reason = RESET_REASON_UNKNOWN;

static Preferences wdt_prefs;

// FIX: Fully initialized struct to suppress -Wmissing-field-initializers
static watchdog_stats_t wdt_stats = {0, 0, 0, 0, 0, 0, 0, RESET_REASON_UNKNOWN, 0, 0};

// FIX: Increased from 10 to 15 to accommodate all system tasks
// Current task count: Safety, Motion, Encoder, CLI, Fault_Log, PLC,
// I2C_Manager, Monitor, Telemetry, LCD_Formatter, LCD = 11 tasks
static watchdog_tick_t wdt_tasks[15] = {
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}
};

static int wdt_task_count = 0;
static watchdog_callback_t wdt_callback = NULL;
static uint32_t wdt_pause_count = 0;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static reset_reason_t getResetReason() {
  esp_reset_reason_t reset_reason = esp_reset_reason();
  
  switch (reset_reason) {
    case ESP_RST_POWERON: return RESET_REASON_POWER_ON;
    case ESP_RST_EXT: return RESET_REASON_EXTERNAL;
    case ESP_RST_SW: return RESET_REASON_SOFTWARE;
    case ESP_RST_TASK_WDT: return RESET_REASON_WATCHDOG_TASK;
    case ESP_RST_INT_WDT: return RESET_REASON_WATCHDOG_INTERRUPT;
    case ESP_RST_BROWNOUT: return RESET_REASON_BROWN_OUT;
    default: return RESET_REASON_UNKNOWN;
  }
}

static const char* resetReasonString(reset_reason_t reason) {
  switch (reason) {
    case RESET_REASON_POWER_ON: return "Power-On Reset";
    case RESET_REASON_EXTERNAL: return "External Reset";
    case RESET_REASON_SOFTWARE: return "Software Reset";
    case RESET_REASON_WATCHDOG_TASK: return "Task WDT Timeout";
    case RESET_REASON_WATCHDOG_INTERRUPT: return "Interrupt WDT Timeout";
    case RESET_REASON_BROWN_OUT: return "Brown-Out Reset";
    default: return "Unknown Reset";
  }
}

// ============================================================================
// WATCHDOG INITIALIZATION
// ============================================================================

void watchdogInit() {
  if (wdt_initialized) return;
  
  // Use logInfo for system startup messages
  logModuleInit("WDT");
  
  last_reset_reason = getResetReason();
  logInfo("[WDT] Last reset reason: %s", resetReasonString(last_reset_reason));
  
  if (!wdt_prefs.begin("bisso_wdt", false)) {
    logWarning("[WDT] Could not open WDT NVS namespace");
  } else {
    wdt_stats.reset_count = wdt_prefs.getUInt("reset_count", 0);
    wdt_stats.timeouts_detected = wdt_prefs.getUInt("timeouts", 0);
    
    if (last_reset_reason == RESET_REASON_WATCHDOG_TASK ||
        last_reset_reason == RESET_REASON_WATCHDOG_INTERRUPT) {
      wdt_stats.reset_count++;
      wdt_stats.timeouts_detected++;
      wdt_prefs.putUInt("reset_count", wdt_stats.reset_count);
      wdt_prefs.putUInt("timeouts", wdt_stats.timeouts_detected);
      
      logError("[WDT] WATCHDOG TIMEOUT RESET DETECTED (Count: %lu)", (unsigned long)wdt_stats.reset_count);
      faultLogError(FAULT_WATCHDOG_TIMEOUT, "Watchdog timeout reset detected");
    }
  }
  
  if (ENABLE_TASK_WDT) {
    logInfo("[WDT] Configuring Task WDT (timeout: %d s)...", WATCHDOG_TIMEOUT_SEC);
    
    esp_err_t ret = esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
    if (ret == ESP_OK) {
      logInfo("[WDT] Task WDT initialized");
    } else if (ret == ESP_ERR_INVALID_STATE) {
      logInfo("[WDT] Task WDT already initialized");
    } else {
      logError("[WDT] Failed to initialize Task WDT: %d", ret);
    }
  }
  
  wdt_enabled = true;
  wdt_initialized = true;
  logModuleInitOK("WDT");
}

// ============================================================================
// TASK REGISTRATION
// ============================================================================

void watchdogTaskAdd(const char* task_name) {
  if (!wdt_initialized) return;
  if (wdt_task_count >= 15) {  // Increased from 10 to 15
    logError("[WDT] Too many tasks registered!");
    return;
  }
  
  for (int i = 0; i < wdt_task_count; i++) {
    if (strcmp(wdt_tasks[i].task_name, task_name) == 0) return;
  }
  
  wdt_tasks[wdt_task_count].task_name = task_name;
  wdt_tasks[wdt_task_count].last_tick = millis();
  wdt_tasks[wdt_task_count].tick_count = 0;
  wdt_tasks[wdt_task_count].missed_ticks = 0;
  wdt_tasks[wdt_task_count].fed_this_cycle = false;
  wdt_tasks[wdt_task_count].consecutive_misses = 0;
  
  wdt_task_count++;
  logInfo("[WDT] Registered task: %s", task_name);
}

void watchdogSubscribeTask(TaskHandle_t task_handle, const char* task_name) {
  if (!wdt_initialized || !wdt_enabled) return;
  
  esp_err_t ret = esp_task_wdt_add(task_handle);
  if (ret == ESP_OK) {
    logInfo("[WDT] Subscribed task: %s", task_name);
  } else {
    logError("[WDT] Failed to subscribe task %s: %d", task_name, ret);
  }
}

// ============================================================================
// WATCHDOG FEEDING
// ============================================================================

void watchdogFeed(const char* task_name) {
  if (!wdt_initialized || !wdt_enabled || wdt_pause_count > 0) return;
  
  for (int i = 0; i < wdt_task_count; i++) {
    if (strcmp(wdt_tasks[i].task_name, task_name) == 0) {
      wdt_tasks[i].last_tick = millis();
      wdt_tasks[i].tick_count++;
      wdt_tasks[i].fed_this_cycle = true;
      wdt_stats.total_ticks++;
      
      if (wdt_tasks[i].consecutive_misses > 0) {
        wdt_stats.automatic_recoveries++;

        // PHASE 5.10: Clear watchdog alert event when task recovers
        systemEventsSystemClear(EVENT_SYSTEM_WATCHDOG_ALERT);
      }
      wdt_tasks[i].consecutive_misses = 0;
      
      esp_task_wdt_reset();
      return;
    }
  }
}

// ============================================================================
// WATCHDOG STATUS & MONITORING
// ============================================================================

wdt_status_t watchdogGetStatus() {
  if (!wdt_initialized || !wdt_enabled) return WDT_STATUS_DISABLED;
  
  uint32_t now = millis();
  for (int i = 0; i < wdt_task_count; i++) {
    uint32_t time_since_feed = now - wdt_tasks[i].last_tick;
    
    if (time_since_feed > (WATCHDOG_TIMEOUT_SEC * 1000 / 2)) {
      if (!wdt_tasks[i].fed_this_cycle) {
        wdt_tasks[i].consecutive_misses++;
        wdt_tasks[i].missed_ticks++;
        
        if (wdt_tasks[i].consecutive_misses > 3) {
          wdt_status = WDT_STATUS_TIMEOUT;

          // PHASE 5.10: Signal watchdog alert event
          systemEventsSystemSet(EVENT_SYSTEM_WATCHDOG_ALERT);

          return wdt_status;
        }
      }
    }
    wdt_tasks[i].fed_this_cycle = false;
  }
  return WDT_STATUS_OK;
}

bool watchdogRecoveredFromTimeout() {
  return (last_reset_reason == RESET_REASON_WATCHDOG_TASK ||
          last_reset_reason == RESET_REASON_WATCHDOG_INTERRUPT);
}

// ============================================================================
// DIAGNOSTICS (CLI USAGE - thread-safe logging)
// ============================================================================

void watchdogShowStatus() {
  logPrintln("\r\n=== WATCHDOG STATUS ===");
  logPrintf("State: %s\r\n", wdt_enabled ? "[ENABLED]" : "[DISABLED]");
  logPrintf("Timeout: %d sec\r\n", WATCHDOG_TIMEOUT_SEC);
  
  const char* status_str = "[UNKNOWN]";
  switch (watchdogGetStatus()) {
    case WDT_STATUS_OK: status_str = "[OK]"; break;
    case WDT_STATUS_TIMEOUT: status_str = "[FAIL] TIMEOUT"; break;
    case WDT_STATUS_PANIC: status_str = "[FAIL] PANIC"; break;
    case WDT_STATUS_RECOVERY_ATTEMPTED: status_str = "[WARN] RECOVERING"; break;
    case WDT_STATUS_DISABLED: status_str = "[DISABLED]"; break;
  }
  logPrintf("Status: %s\r\n", status_str);
  
  logPrintf("Last Reset: %s\r\n", resetReasonString(last_reset_reason));
  logPrintf("Total Resets: %lu\r\n", (unsigned long)wdt_stats.reset_count);
  logPrintf("Timeouts: %lu\r\n", (unsigned long)wdt_stats.timeouts_detected);
  logPrintln("");
}

void watchdogShowTasks() {
  logPrintln("\r\n=== MONITORED TASKS ===");
  logPrintln("Task                  Ticks     Missed   Age(ms)  Status");
  logPrintln("------------------------------------------------------");
  
  uint32_t now = millis();
  for (int i = 0; i < wdt_task_count; i++) {
    uint32_t age = now - wdt_tasks[i].last_tick;
    
    const char* status_str = "[FAIL]";
    if (age < (WATCHDOG_TIMEOUT_SEC * 1000 / 2)) status_str = "[OK]";
    else if (age < WATCHDOG_TIMEOUT_SEC * 1000) status_str = "[WARN]";
    
    logPrintf("%-20s  %-8lu  %-8lu %-8lu %s\r\n", 
        wdt_tasks[i].task_name, 
        (unsigned long)wdt_tasks[i].tick_count, 
        (unsigned long)wdt_tasks[i].missed_ticks, 
        (unsigned long)age,
        status_str);
  }
  logPrintln("");
}

void watchdogShowStats() {
  logPrintln("\r\n=== WATCHDOG STATISTICS ===");
  logPrintf("Timeouts: %lu\r\n", (unsigned long)wdt_stats.timeouts_detected);
  logPrintf("Recoveries: %lu\r\n", (unsigned long)wdt_stats.automatic_recoveries);
  logPrintf("Task Resets: %lu\r\n", (unsigned long)wdt_stats.task_resets);
  logPrintf("Sys Resets: %lu\r\n", (unsigned long)wdt_stats.system_resets);
  logPrintf("Total Feeds: %lu\r\n", (unsigned long)wdt_stats.total_ticks);
  logPrintf("Missed Ticks: %lu\r\n", (unsigned long)wdt_stats.missed_ticks);
  logPrintln("");
}

reset_reason_t watchdogGetLastResetReason() { return last_reset_reason; }
uint32_t watchdogGetResetCount() { return wdt_stats.reset_count; }

// PHASE 5.10: Implement proper task alive checking
bool watchdogIsTaskAlive(const char* task_name) {
  if (!wdt_initialized || !wdt_enabled || !task_name) return true;

  uint32_t now = millis();
  for (int i = 0; i < wdt_task_count; i++) {
    if (strcmp(wdt_tasks[i].task_name, task_name) == 0) {
      uint32_t time_since_feed = now - wdt_tasks[i].last_tick;
      // Task is alive if it fed within half the timeout period
      return (time_since_feed < (WATCHDOG_TIMEOUT_SEC * 1000 / 2));
    }
  }
  // Task not registered - return true to avoid false alarms
  return true;
}

// PHASE 5.10: Implement proper missed ticks retrieval
uint32_t watchdogGetMissedTicks(const char* task_name) {
  if (!wdt_initialized || !task_name) return 0;

  for (int i = 0; i < wdt_task_count; i++) {
    if (strcmp(wdt_tasks[i].task_name, task_name) == 0) {
      return wdt_tasks[i].missed_ticks;
    }
  }
  // Task not found
  return 0;
}

void watchdogRecovery() {
  logWarning("[WDT] Attempting recovery...");
  for (int i = 0; i < wdt_task_count; i++) {
    wdt_tasks[i].consecutive_misses = 0;
    wdt_tasks[i].last_tick = millis();
  }
  esp_task_wdt_reset();
  wdt_stats.automatic_recoveries++;
  wdt_status = WDT_STATUS_RECOVERY_ATTEMPTED;
  logInfo("[WDT] Recovery complete");
}

void watchdogResetStats() {
  logInfo("[WDT] Resetting statistics...");
  memset(&wdt_stats, 0, sizeof(wdt_stats));
  for (int i = 0; i < wdt_task_count; i++) {
    wdt_tasks[i].tick_count = 0;
    wdt_tasks[i].missed_ticks = 0;
    wdt_tasks[i].consecutive_misses = 0;
  }
  if (wdt_prefs.isKey("timeouts")) wdt_prefs.remove("timeouts");
}

void watchdogEnable() {
  if (wdt_initialized && !wdt_enabled) {
    wdt_enabled = true;
    esp_task_wdt_reset();
    logInfo("[WDT] Enabled");
  }
}

void watchdogDisable() {
  if (wdt_initialized && wdt_enabled) {
    wdt_enabled = false;
    logWarning("[WDT] Disabled");
  }
}

void watchdogPause() { wdt_pause_count++; }
void watchdogResume() { 
    if(wdt_pause_count > 0) wdt_pause_count--; 
    if(wdt_pause_count == 0 && wdt_enabled) esp_task_wdt_reset(); 
}

void watchdogDelay(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    if (wdt_enabled && wdt_pause_count == 0) esp_task_wdt_reset();
    delay(10);
  }
}

void watchdogSetCallback(watchdog_callback_t callback) { wdt_callback = callback; }
void watchdogLogStats() {
  wdt_prefs.putUInt("reset_count", wdt_stats.reset_count);
  wdt_prefs.putUInt("timeouts", wdt_stats.timeouts_detected);
  wdt_prefs.putUInt("recoveries", wdt_stats.automatic_recoveries);
}
watchdog_stats_t* watchdogGetStats() { return &wdt_stats; }
void watchdogPrintDetailedReport() { watchdogShowStatus(); watchdogShowTasks(); watchdogShowStats(); }

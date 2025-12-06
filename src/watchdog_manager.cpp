#include "watchdog_manager.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <string.h>

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

// FIX: Fully initialized array of structs
static watchdog_tick_t wdt_tasks[10] = {
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0},
    {NULL, 0, 0, 0, false, 0}, {NULL, 0, 0, 0, false, 0}
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
  logInfo("[WDT] Initializing...");
  
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
  logInfo("[WDT] System initialized");
}

// ============================================================================
// TASK REGISTRATION
// ============================================================================

void watchdogTaskAdd(const char* task_name) {
  if (!wdt_initialized) return;
  if (wdt_task_count >= 10) {
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
      
      if (wdt_tasks[i].consecutive_misses > 0) {
        wdt_stats.automatic_recoveries++;
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
// DIAGNOSTICS (CLI USAGE - uses Serial.print)
// ============================================================================

void watchdogShowStatus() {
  Serial.println("\n=== WATCHDOG STATUS ===");
  Serial.printf("State: %s\n", wdt_enabled ? "[ENABLED]" : "[DISABLED]");
  Serial.printf("Timeout: %d sec\n", WATCHDOG_TIMEOUT_SEC);
  
  Serial.print("Status: ");
  switch (watchdogGetStatus()) {
    case WDT_STATUS_OK: Serial.println("[OK]"); break;
    case WDT_STATUS_TIMEOUT: Serial.println("[FAIL] TIMEOUT"); break;
    case WDT_STATUS_PANIC: Serial.println("[FAIL] PANIC"); break;
    case WDT_STATUS_RECOVERY_ATTEMPTED: Serial.println("[WARN] RECOVERING"); break;
    case WDT_STATUS_DISABLED: Serial.println("[DISABLED]"); break;
  }
  
  Serial.printf("Last Reset: %s\n", resetReasonString(last_reset_reason));
  // FIX: Cast to unsigned long for printf
  Serial.printf("Total Resets: %lu\n", (unsigned long)wdt_stats.reset_count);
  Serial.printf("Timeouts: %lu\n", (unsigned long)wdt_stats.timeouts_detected);
  Serial.println();
}

void watchdogShowTasks() {
  Serial.println("\n=== MONITORED TASKS ===");
  Serial.println("Task                  Ticks     Missed   Age(ms)  Status");
  Serial.println("------------------------------------------------------");
  
  uint32_t now = millis();
  for (int i = 0; i < wdt_task_count; i++) {
    uint32_t age = now - wdt_tasks[i].last_tick;
    
    // FIX: Cast to unsigned long for printf
    Serial.printf("%-20s  %-8lu  %-8lu %-8lu ", 
        wdt_tasks[i].task_name, 
        (unsigned long)wdt_tasks[i].tick_count, 
        (unsigned long)wdt_tasks[i].missed_ticks, 
        (unsigned long)age);
    
    if (age < (WATCHDOG_TIMEOUT_SEC * 1000 / 2)) Serial.println("[OK]");
    else if (age < WATCHDOG_TIMEOUT_SEC * 1000) Serial.println("[WARN]");
    else Serial.println("[FAIL]");
  }
  Serial.println();
}

void watchdogShowStats() {
  Serial.println("\n=== WATCHDOG STATISTICS ===");
  // FIX: Cast to unsigned long for printf
  Serial.printf("Timeouts: %lu\n", (unsigned long)wdt_stats.timeouts_detected);
  Serial.printf("Recoveries: %lu\n", (unsigned long)wdt_stats.automatic_recoveries);
  Serial.printf("Task Resets: %lu\n", (unsigned long)wdt_stats.task_resets);
  Serial.printf("Sys Resets: %lu\n", (unsigned long)wdt_stats.system_resets);
  Serial.printf("Total Feeds: %lu\n", (unsigned long)wdt_stats.total_ticks);
  Serial.printf("Missed Ticks: %lu\n", (unsigned long)wdt_stats.missed_ticks);
  Serial.println();
}

reset_reason_t watchdogGetLastResetReason() { return last_reset_reason; }
uint32_t watchdogGetResetCount() { return wdt_stats.reset_count; }
bool watchdogIsTaskAlive(const char* task_name) { return true; } // Simplified
uint32_t watchdogGetMissedTicks(const char* task_name) { return 0; }

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
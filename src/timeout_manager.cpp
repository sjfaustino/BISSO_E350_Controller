#include "timeout_manager.h"
#include "serial_logger.h"
#include <string.h>
#include <stdio.h>

#define MAX_TIMEOUT_HANDLES 16
static timeout_handle_t timeout_handles[MAX_TIMEOUT_HANDLES];
static int active_timeout_count = 0;

static const char* timeout_names[] = {
  "ENCODER", "PLC", "LCD", "MOTION", "CALIB", "ESTOP", "CONFIG", "SAFETY", "CLI"
};
static const uint32_t timeout_values[] = {
  TIMEOUT_ENCODER_READ, TIMEOUT_PLC_RESPONSE, TIMEOUT_LCD_RESPONSE, 
  TIMEOUT_MOTION_EXECUTE, TIMEOUT_CALIBRATION_MOVE, TIMEOUT_EMERGENCY_STOP,
  TIMEOUT_CONFIG_SAVE, TIMEOUT_SAFETY_CHECK, TIMEOUT_CLI_COMMAND
};

void timeoutManagerInit() {
  logInfo("[TIMEOUT] Initializing...");
  memset(timeout_handles, 0, sizeof(timeout_handles));
  active_timeout_count = 0;
  logInfo("[TIMEOUT] [OK] Ready");
}

uint32_t timeoutGetStandard(timeout_type_t type) {
  if (type < TIMEOUT_MAX) return timeout_values[type];
  return 1000;
}

timeout_handle_t* timeoutStartCustom(timeout_type_t type, uint32_t custom_ms) {
  for (int i = 0; i < MAX_TIMEOUT_HANDLES; i++) {
    if (!timeout_handles[i].active) {
      timeout_handles[i].type = type;
      timeout_handles[i].start_time = millis();
      timeout_handles[i].timeout_ms = custom_ms;
      timeout_handles[i].active = true;
      timeout_handles[i].triggered = false;
      active_timeout_count++;
      return &timeout_handles[i];
    }
  }
  logError("[TIMEOUT] No free handles");
  return NULL;
}

timeout_handle_t* timeoutStart(timeout_type_t type) {
    return timeoutStartCustom(type, timeoutGetStandard(type));
}

bool timeoutCheck(timeout_handle_t* handle) {
  if (!handle || !handle->active) return false;
  if ((uint32_t)(millis() - handle->start_time) >= handle->timeout_ms) {
    handle->triggered = true;
    return true;
  }
  return false;
}

bool timeoutExpired(timeout_handle_t* handle) { return handle ? handle->triggered : false; }

void timeoutStop(timeout_handle_t* handle) {
  if (handle && handle->active) {
    handle->active = false;
    active_timeout_count--;
  }
}

uint32_t timeoutElapsed(timeout_handle_t* handle) {
    return (handle && handle->active) ? (uint32_t)(millis() - handle->start_time) : 0;
}

uint32_t timeoutRemaining(timeout_handle_t* handle) {
    if (!handle || !handle->active) return 0;
    uint32_t el = (uint32_t)(millis() - handle->start_time);
    return (el >= handle->timeout_ms) ? 0 : (handle->timeout_ms - el);
}

void timeoutResetAll() {
  logInfo("[TIMEOUT] Resetting all handles...");
  for(int i=0; i<MAX_TIMEOUT_HANDLES; i++) timeout_handles[i].active = false;
  active_timeout_count = 0;
  logInfo("[TIMEOUT] [OK] Reset complete.");
}

void timeoutShowDiagnostics() {
  serialLoggerLock();
  Serial.println("\n=== TIMEOUT DIAGNOSTICS ===");
  Serial.printf("Active: %d / %d\n", active_timeout_count, MAX_TIMEOUT_HANDLES);
  
  for (int i = 0; i < MAX_TIMEOUT_HANDLES; i++) {
    if (timeout_handles[i].active) {
        Serial.printf("  [%d] %s: Elapsed=%lu ms | Remaining=%lu ms\n",
            i, timeout_names[timeout_handles[i].type],
            (unsigned long)timeoutElapsed(&timeout_handles[i]),
            (unsigned long)timeoutRemaining(&timeout_handles[i]));
    }
  }
  Serial.println();
  serialLoggerUnlock();
}
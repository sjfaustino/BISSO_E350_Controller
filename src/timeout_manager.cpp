#include "timeout_manager.h"

#define MAX_TIMEOUT_HANDLES 16

static timeout_handle_t timeout_handles[MAX_TIMEOUT_HANDLES];
static int active_timeout_count = 0;

static const uint32_t timeout_values[TIMEOUT_MAX] = {
  TIMEOUT_ENCODER_READ,
  TIMEOUT_PLC_RESPONSE,
  TIMEOUT_LCD_RESPONSE,
  TIMEOUT_MOTION_EXECUTE,
  TIMEOUT_CALIBRATION_MOVE,
  TIMEOUT_EMERGENCY_STOP,
  TIMEOUT_CONFIG_SAVE,
  TIMEOUT_SAFETY_CHECK,
  TIMEOUT_CLI_COMMAND
};

static const char* timeout_names[TIMEOUT_MAX] = {
  "ENCODER_READ",
  "PLC_RESPONSE",
  "LCD_RESPONSE",
  "MOTION_EXECUTE",
  "CALIBRATION_MOVE",
  "EMERGENCY_STOP",
  "CONFIG_SAVE",
  "SAFETY_CHECK",
  "CLI_COMMAND"
};

void timeoutManagerInit() {
  Serial.println("[TIMEOUT] Timeout manager initializing...");
  memset(timeout_handles, 0, sizeof(timeout_handles));
  active_timeout_count = 0;
  Serial.println("[TIMEOUT] ✅ Timeout manager ready");
}

uint32_t timeoutGetStandard(timeout_type_t type) {
  if (type < TIMEOUT_MAX) {
    return timeout_values[type];
  }
  return 1000;  // Default 1 second
}

timeout_handle_t* timeoutStart(timeout_type_t type) {
  return timeoutStartCustom(type, timeout_values[type]);
}

timeout_handle_t* timeoutStartCustom(timeout_type_t type, uint32_t custom_ms) {
  // Find free handle
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
  
  Serial.println("[TIMEOUT] ERROR: No free timeout handles!");
  return NULL;
}

bool timeoutCheck(timeout_handle_t* handle) {
  if (!handle || !handle->active) {
    return false;
  }
  
  uint32_t elapsed = millis() - handle->start_time;
  if (elapsed >= handle->timeout_ms) {
    handle->triggered = true;
    return true;
  }
  
  return false;
}

bool timeoutExpired(timeout_handle_t* handle) {
  if (!handle) {
    return false;
  }
  return handle->triggered;
}

void timeoutStop(timeout_handle_t* handle) {
  if (handle && handle->active) {
    handle->active = false;
    active_timeout_count--;
  }
}

uint32_t timeoutElapsed(timeout_handle_t* handle) {
  if (!handle || !handle->active) {
    return 0;
  }
  return millis() - handle->start_time;
}

uint32_t timeoutRemaining(timeout_handle_t* handle) {
  if (!handle || !handle->active) {
    return 0;
  }
  
  uint32_t elapsed = millis() - handle->start_time;
  if (elapsed >= handle->timeout_ms) {
    return 0;
  }
  
  return handle->timeout_ms - elapsed;
}

void timeoutResetAll() {
  Serial.println("[TIMEOUT] Resetting all timeout handles...");
  
  for (int i = 0; i < MAX_TIMEOUT_HANDLES; i++) {
    if (timeout_handles[i].active) {
      timeout_handles[i].active = false;
      timeout_handles[i].triggered = false;
    }
  }
  
  active_timeout_count = 0;
  Serial.println("[TIMEOUT] ✅ All timeouts reset");
}

void timeoutShowDiagnostics() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║              TIMEOUT MANAGER DIAGNOSTICS                       ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[TIMEOUT] Active Handles: ");
  Serial.print(active_timeout_count);
  Serial.print(" / ");
  Serial.println(MAX_TIMEOUT_HANDLES);
  
  Serial.println("\n[TIMEOUTS] Standard Values:");
  for (int i = 0; i < TIMEOUT_MAX; i++) {
    Serial.print("  ");
    Serial.print(timeout_names[i]);
    Serial.print(": ");
    Serial.print(timeout_values[i]);
    Serial.println(" ms");
  }
  
  Serial.println("\n[ACTIVE] Running Timeouts:");
  bool any_active = false;
  for (int i = 0; i < MAX_TIMEOUT_HANDLES; i++) {
    if (timeout_handles[i].active) {
      any_active = true;
      uint32_t elapsed = timeoutElapsed(&timeout_handles[i]);
      uint32_t remaining = timeoutRemaining(&timeout_handles[i]);
      
      Serial.print("  [");
      Serial.print(i);
      Serial.print("] ");
      Serial.print(timeout_names[timeout_handles[i].type]);
      Serial.print(" - ");
      Serial.print(elapsed);
      Serial.print("ms elapsed / ");
      Serial.print(remaining);
      Serial.print("ms remaining");
      if (timeout_handles[i].triggered) {
        Serial.print(" [TRIGGERED]");
      }
      Serial.println();
    }
  }
  
  if (!any_active) {
    Serial.println("  (none)");
  }
  
  Serial.println();
}

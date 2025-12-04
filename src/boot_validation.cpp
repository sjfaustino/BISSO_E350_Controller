#include "boot_validation.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_constants.h" 
#include <Preferences.h>
#include <string.h>
#include <Arduino.h>

#define BOOT_NVS_NAMESPACE "bisso_boot"

static boot_sequence_t boot_seq = {0};
static subsystem_health_t subsystems[MAX_BOOT_SUBSYSTEMS] = {0};
static int subsystem_count = 0;
static bool degraded_mode = false;
static bool shutting_down = false;
static Preferences boot_prefs;

void bootValidationInit() {
  logInfo("[BOOT_VAL] Initializing...");
  
  boot_seq.boot_start_time = millis();
  boot_seq.overall_status = BOOT_OK;
  
  if (!boot_prefs.begin(BOOT_NVS_NAMESPACE, true)) { 
    logWarning("[BOOT_VAL] Could not open NVS");
  } else {
    uint32_t consecutive = boot_prefs.getUInt("consecutive_ok", 0);
    boot_prefs.end();
    logInfo("[BOOT_VAL] Consecutive boots: %lu", consecutive);
  }
  
  logInfo("[BOOT_VAL] Ready");
}

void bootRegisterSubsystem(const char* name) {
  if (subsystem_count >= MAX_BOOT_SUBSYSTEMS) { 
    logError("[BOOT_VAL] Too many subsystems!");
    return;
  }
  
  subsystems[subsystem_count].subsystem_name = name;
  subsystems[subsystem_count].initialized = false;
  subsystems[subsystem_count].healthy = false;
  subsystems[subsystem_count].error_count = 0;
  subsystems[subsystem_count].last_error = NULL;
  subsystems[subsystem_count].status = BOOT_OK;
  
  subsystem_count++;
}

void bootMarkInitialized(const char* name) {
  for (int i = 0; i < subsystem_count; i++) {
    if (strcmp(subsystems[i].subsystem_name, name) == 0) {
      subsystems[i].initialized = true;
      subsystems[i].healthy = true;
      subsystems[i].init_time_ms = millis() - boot_seq.boot_start_time;
      subsystems[i].status = BOOT_OK;
      boot_seq.systems_initialized++;
      boot_seq.systems_healthy++;
      return;
    }
  }
}

void bootMarkFailed(const char* name, const char* error_msg, boot_status_code_t status) {
  for (int i = 0; i < subsystem_count; i++) {
    if (strcmp(subsystems[i].subsystem_name, name) == 0) {
      subsystems[i].initialized = false;
      subsystems[i].healthy = false;
      subsystems[i].last_error = error_msg;
      subsystems[i].error_count++;
      subsystems[i].status = status;
      boot_seq.systems_failed++;
      boot_seq.overall_status = status;
      
      if (boot_prefs.begin(BOOT_NVS_NAMESPACE, false)) { 
        boot_prefs.putUInt("errors", boot_prefs.getUInt("errors", 0) + 1);
        boot_prefs.end();
      }
      
      faultLogError(FAULT_BOOT_FAILED, error_msg);
      return;
    }
  }
}

void bootMarkHealthy(const char* name) {
  for (int i = 0; i < subsystem_count; i++) {
    if (strcmp(subsystems[i].subsystem_name, name) == 0) {
      subsystems[i].healthy = true;
      return;
    }
  }
}

void bootMarkUnhealthy(const char* name, const char* error_msg) {
  for (int i = 0; i < subsystem_count; i++) {
    if (strcmp(subsystems[i].subsystem_name, name) == 0) {
      subsystems[i].healthy = false;
      subsystems[i].last_error = error_msg;
      subsystems[i].error_count++;
      return;
    }
  }
}

bool bootValidateAllSystems() {
  logInfo("[BOOT] === VALIDATION SEQUENCE ===");
  
  int failed_count = 0;
  int healthy_count = 0;
  
  for (int i = 0; i < subsystem_count; i++) {
    const char* status = subsystems[i].healthy ? "[OK]" : "[FAIL]";
    logInfo("[BOOT] %-15s: %s (Init: %lums)", subsystems[i].subsystem_name, status, subsystems[i].init_time_ms);
    
    if (!subsystems[i].healthy) failed_count++;
    else healthy_count++;
  }
  
  bool can_proceed = false;
  
  if (failed_count == 0) {
    logInfo("[BOOT] [PASS] All systems healthy.");
    boot_seq.boot_validation_passed = true;
    boot_seq.overall_status = BOOT_OK;
    
    if (boot_prefs.begin(BOOT_NVS_NAMESPACE, false)) { 
      uint32_t consecutive = boot_prefs.getUInt("consecutive_ok", 0) + 1;
      boot_prefs.putUInt("consecutive_ok", consecutive);
      boot_prefs.end();
    }
    
    can_proceed = true;
  } else if (healthy_count >= (subsystem_count - 2)) {
    logWarning("[BOOT] [WARN] DEGRADED MODE.");
    degraded_mode = true;
    boot_seq.boot_validation_passed = true;
    boot_seq.overall_status = BOOT_OK;
    can_proceed = true;
  } else {
    logError("[BOOT] [FAIL] CRITICAL FAILURE (%d systems down)", failed_count);
    boot_seq.boot_validation_passed = false;
    boot_seq.overall_status = BOOT_CRITICAL_ERROR;
    
    if (boot_prefs.begin(BOOT_NVS_NAMESPACE, false)) { 
      boot_prefs.putUInt("consecutive_ok", 0);
      boot_prefs.end();
    }
    
    can_proceed = false;
  }
  
  boot_seq.boot_complete_time = millis();
  boot_seq.total_boot_time_ms = boot_seq.boot_complete_time - boot_seq.boot_start_time;
  
  return can_proceed;
}

bool bootIsSubsystemHealthy(const char* name) {
  for (int i = 0; i < subsystem_count; i++) {
    if (strcmp(subsystems[i].subsystem_name, name) == 0) {
      return subsystems[i].healthy;
    }
  }
  return false;
}

boot_status_code_t bootGetStatus() { return boot_seq.overall_status; }

// CLI Helper: Uses Serial because it is user-requested
void bootShowStatus() {
  Serial.println("\n=== BOOT STATUS ===");
  Serial.printf("Status: %s\n", boot_seq.boot_validation_passed ? "[OK]" : "[FAIL]");
  Serial.printf("Time:   %lu ms\n", boot_seq.total_boot_time_ms);
  Serial.printf("Failed: %d\n", boot_seq.systems_failed);
}

void bootShowDetailedReport() {
  Serial.println("\n=== DETAILED BOOT REPORT ===");
  for (int i = 0; i < subsystem_count; i++) {
    Serial.printf("  %-15s: %s | Time: %4lums | Errors: %lu\n", 
      subsystems[i].subsystem_name,
      subsystems[i].healthy ? "[OK]" : "[FAIL]",
      subsystems[i].init_time_ms,
      subsystems[i].error_count);
      
    if (subsystems[i].last_error) {
      Serial.printf("    Error: %s\n", subsystems[i].last_error);
    }
  }
}

void bootShowSubsystemHealth() {
  Serial.println("\n=== HEALTH MONITOR ===");
  for (int i = 0; i < subsystem_count; i++) {
    Serial.printf("  %-15s: %s\n", subsystems[i].subsystem_name, subsystems[i].healthy ? "[GOOD]" : "[BAD]");
  }
}

void bootShowInitTimes() {
  Serial.println("\n=== INIT TIMING ===");
  for (int i = 0; i < subsystem_count; i++) {
    Serial.printf("  %-15s: %lu ms\n", subsystems[i].subsystem_name, subsystems[i].init_time_ms);
  }
  Serial.printf("Total: %lu ms\n", boot_seq.total_boot_time_ms);
}

bool bootAttemptRecovery(const char* subsystem) {
  logWarning("[BOOT_RECOVERY] Attempting recovery of %s", subsystem);
  faultLogWarning(FAULT_BOOT_RECOVERY_ATTEMPTED, subsystem);
  return false;
}

void bootHandleCriticalError(const char* error_msg) {
  logError("[BOOT] CRITICAL HALT: %s", error_msg);
  faultLogError(FAULT_CRITICAL_SYSTEM_ERROR, error_msg);
  
  Serial.println("\n*** SYSTEM HALTED ***");
  Serial.println("Critical error during boot.");
  
  while (1) {
    delay(1000);
  }
}

void bootRebootSystem() {
  logInfo("[BOOT] Reboot requested.");
  delay(1000);
  esp_restart();
}

void bootEmergencyHalt(const char* reason) {
  logError("[BOOT] EMERGENCY HALT: %s", reason);
  faultLogError(FAULT_EMERGENCY_HALT, reason);
  shutting_down = true;
  while (1) delay(10000);
}

bool bootCanOperateDegraded() { return (boot_seq.systems_failed <= 2); }

void bootSetDegradedMode(bool enabled) {
  degraded_mode = enabled;
  if (enabled) logWarning("[BOOT] Switched to DEGRADED MODE");
  else logInfo("[BOOT] Switched to NORMAL MODE");
}

bool bootIsDegradedMode() { return degraded_mode; }

void bootShowDegradedModeStatus() {
  Serial.printf("\nMode: %s\n", degraded_mode ? "DEGRADED" : "NORMAL");
  if (degraded_mode) {
      Serial.println("Operational Systems:");
      for (int i = 0; i < subsystem_count; i++) {
          if (subsystems[i].healthy) Serial.printf("  [OK] %s\n", subsystems[i].subsystem_name);
      }
  }
}

void bootLogErrors() {
  if (!boot_prefs.begin(BOOT_NVS_NAMESPACE, true)) {
    logError("[BOOT] NVS Error");
    return;
  }
  uint32_t error_count = boot_prefs.getUInt("errors", 0);
  boot_prefs.end();
  logInfo("[BOOT] Total recorded errors: %lu", error_count);
}

const char* bootGetLastError() {
  for (int i = subsystem_count - 1; i >= 0; i--) {
    if (subsystems[i].last_error) return subsystems[i].last_error;
  }
  return "None";
}

uint32_t bootGetErrorCount() {
  if (boot_prefs.begin(BOOT_NVS_NAMESPACE, true)) {
    uint32_t count = boot_prefs.getUInt("errors", 0);
    boot_prefs.end();
    return count;
  }
  return 0;
}

uint32_t bootGetConsecutiveSuccesses() {
  if (boot_prefs.begin(BOOT_NVS_NAMESPACE, true)) {
    uint32_t count = boot_prefs.getUInt("consecutive_ok", 0);
    boot_prefs.end();
    return count;
  }
  return 0;
}

void bootShowRecoveryHistory() {
  Serial.println("\n=== RECOVERY HISTORY ===");
  Serial.printf("Consecutive Boots: %lu\n", bootGetConsecutiveSuccesses());
  Serial.printf("Total Errors:      %lu\n", bootGetErrorCount());
}

void bootGracefulShutdown(const char* reason) {
  logInfo("[BOOT] Shutdown: %s", reason);
  shutting_down = true;
  faultLogWarning(FAULT_GRACEFUL_SHUTDOWN, reason);
  delay(2000);
  logInfo("[BOOT] Power off safe.");
}

bool bootIsShuttingDown() { return shutting_down; }
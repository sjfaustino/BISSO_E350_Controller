#include "boot_validation.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "system_constants.h" // Required for MAX_BOOT_SUBSYSTEMS
#include <Preferences.h>
#include <string.h>
#include <Arduino.h>

#define BOOT_NVS_NAMESPACE "bisso_boot"

#ifndef MAX_BOOT_SUBSYSTEMS
#define MAX_BOOT_SUBSYSTEMS 15
#endif

// Fully initialize struct to prevent warnings
static boot_sequence_t boot_seq = {0, 0, 0, 0, 0, 0, BOOT_OK, false};

static subsystem_health_t subsystems[MAX_BOOT_SUBSYSTEMS]; 
static int subsystem_count = 0;
static bool degraded_mode = false;
static bool shutting_down = false;
static Preferences boot_prefs;

void bootValidationInit() {
  logInfo("[BOOT_VAL] Initializing...");
  
  // Clear subsystems array safely
  memset(subsystems, 0, sizeof(subsystems));
  
  boot_seq.boot_start_time = millis();
  boot_seq.overall_status = BOOT_OK;
  
  if (!boot_prefs.begin(BOOT_NVS_NAMESPACE, true)) { 
    logWarning("[BOOT_VAL] Could not open NVS");
  } else {
    uint32_t consecutive = boot_prefs.getUInt("consecutive_ok", 0);
    boot_prefs.end();
    logInfo("[BOOT_VAL] Consecutive boots: %u", consecutive);
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
    // Cast to unsigned long for printf safety
    logInfo("[BOOT] %-15s: %s (Init: %lums)", 
            subsystems[i].subsystem_name, status, (unsigned long)subsystems[i].init_time_ms);
    
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

boot_status_code_t bootGetStatus() {
  return boot_seq.overall_status;
}

void bootShowStatus() {
  logPrintln("\n=== BOOT STATUS ===");
  
  logPrintf("Status: %s\n", boot_seq.boot_validation_passed ? "[OK]" : "[FAIL]");
  if (degraded_mode) logPrintln("Mode:   [WARN] DEGRADED");
  
  logPrintf("Time:   %lu ms\n", (unsigned long)boot_seq.total_boot_time_ms);
  logPrintf("Failed: %d\n", boot_seq.systems_failed);
}

void bootShowDetailedReport() {
  logPrintln("\n=== DETAILED BOOT REPORT ===");
  for (int i = 0; i < subsystem_count; i++) {
    logPrintf("  %-15s: %s | Time: %4lums | Errors: %lu\n", 
      subsystems[i].subsystem_name,
      subsystems[i].healthy ? "[OK]" : "[FAIL]",
      (unsigned long)subsystems[i].init_time_ms,
      (unsigned long)subsystems[i].error_count);
      
    if (subsystems[i].last_error) {
      logPrintf("    Error: %s\n", subsystems[i].last_error);
    }
  }
}

void bootShowSubsystemHealth() {
  logPrintln("\n=== HEALTH MONITOR ===");
  for (int i = 0; i < subsystem_count; i++) {
    logPrintf("  %-15s: %s\n", subsystems[i].subsystem_name, subsystems[i].healthy ? "[GOOD]" : "[BAD]");
  }
}

void bootShowInitTimes() {
  logPrintln("\n=== INIT TIMING ===");
  for (int i = 0; i < subsystem_count; i++) {
    logPrintf("  %-15s: %lu ms\n", subsystems[i].subsystem_name, (unsigned long)subsystems[i].init_time_ms);
  }
  logPrintf("Total: %lu ms\n", (unsigned long)boot_seq.total_boot_time_ms);
}

bool bootAttemptRecovery(const char* subsystem) {
  logWarning("[BOOT_RECOVERY] Attempting recovery of %s", subsystem);
  faultLogWarning(FAULT_BOOT_RECOVERY_ATTEMPTED, subsystem);
  return false;
}

void bootHandleCriticalError(const char* error_msg) {
  logError("[BOOT] CRITICAL HALT: %s", error_msg);
  faultLogError(FAULT_CRITICAL_SYSTEM_ERROR, error_msg);
  
  logPrintln("\n*** SYSTEM HALTED ***");
  logPrintln("Critical error during boot. Check logs.");
  
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
  
  while (1) {
    delay(10000);
  }
}

bool bootCanOperateDegraded() {
  return (boot_seq.systems_failed <= 2);
}

void bootSetDegradedMode(bool enabled) {
  degraded_mode = enabled;
  if (enabled) logWarning("[BOOT] Switched to DEGRADED MODE");
  else logInfo("[BOOT] Switched to NORMAL MODE");
}

bool bootIsDegradedMode() {
  return degraded_mode;
}

void bootShowDegradedModeStatus() {
  logPrintf("\nMode: %s\n", degraded_mode ? "DEGRADED" : "NORMAL");
  if (degraded_mode) {
      logPrintln("Operational Systems:");
      for (int i = 0; i < subsystem_count; i++) {
          if (subsystems[i].healthy) {
            logPrintf("  [OK] %s\n", subsystems[i].subsystem_name);
          }
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
  logInfo("[BOOT] Total recorded errors: %u", error_count);
}

const char* bootGetLastError() {
  for (int i = subsystem_count - 1; i >= 0; i--) {
    if (subsystems[i].last_error) {
      return subsystems[i].last_error;
    }
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
  logPrintln("\n=== RECOVERY HISTORY ===");
  logPrintf("Consecutive Boots: %lu\n", (unsigned long)bootGetConsecutiveSuccesses());
  logPrintf("Total Errors:      %lu\n", (unsigned long)bootGetErrorCount());
}

void bootGracefulShutdown(const char* reason) {
  logInfo("[BOOT] Shutdown: %s", reason);
  shutting_down = true;
  faultLogWarning(FAULT_GRACEFUL_SHUTDOWN, reason);
  delay(2000);
  logInfo("[BOOT] Power off safe.");
}

bool bootIsShuttingDown() {
  return shutting_down;
}

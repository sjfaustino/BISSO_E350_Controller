#include "boot_validation.h"
#include "fault_logging.h"
#include <Preferences.h>
#include <string.h>
#include "serial_logger.h"
#include <Arduino.h>

// ============================================================================
// BOOT STATE
// ============================================================================

static boot_sequence_t boot_seq = {0};
static subsystem_health_t subsystems[15] = {0};
static int subsystem_count = 0;
static bool degraded_mode = false;
static bool shutting_down = false;
static Preferences boot_prefs;

// ============================================================================
// BOOT VALIDATION INITIALIZATION
// ============================================================================

void bootValidationInit() {
  Serial.println("[BOOT_VALIDATION] Initializing boot validation system...");
  
  boot_seq.boot_start_time = millis();
  boot_seq.overall_status = BOOT_OK;
  boot_seq.systems_initialized = 0;
  boot_seq.systems_healthy = 0;
  boot_seq.systems_failed = 0;
  boot_seq.boot_validation_passed = false;
  
  // Initialize NVS for boot statistics
  if (!boot_prefs.begin("bisso_boot", true)) { // Read-only check first
    Serial.println("[BOOT_VALIDATION] Warning: Could not open boot NVS namespace");
  } else {
    uint32_t consecutive = boot_prefs.getUInt("consecutive_ok", 0);
    boot_prefs.end();
    Serial.print("[BOOT_VALIDATION] Consecutive successful boots: ");
    Serial.println(consecutive);
  }
  
  Serial.println("[BOOT_VALIDATION] ✅ Boot validation ready\n");
}

// ============================================================================
// SUBSYSTEM REGISTRATION & TRACKING
// ============================================================================

void bootRegisterSubsystem(const char* name) {
  if (subsystem_count >= 15) {
    Serial.println("[BOOT_VALIDATION] ERROR: Too many subsystems!");
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
      
      // Log to NVS
      if (boot_prefs.begin("bisso_boot", false)) { // Read-write
        boot_prefs.putUInt("errors", boot_prefs.getUInt("errors", 0) + 1);
        boot_prefs.end();
      }
      
      // Log to fault system
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

// ============================================================================
// VALIDATION & DIAGNOSTICS
// ============================================================================

bool bootValidateAllSystems() {
  Serial.println("\n[BOOT_VALIDATION] ╔═══════════════════════════════════════════╗");
  Serial.println("[BOOT_VALIDATION] ║     BOOT VALIDATION - ALL SYSTEMS        ║");
  Serial.println("[BOOT_VALIDATION] ╚═══════════════════════════════════════════╝\n");
  
  int initialized_count = 0;
  int healthy_count = 0;
  int failed_count = 0;
  
  Serial.println("[BOOT_VALIDATION] Subsystem Status:");
  Serial.println("[BOOT_VALIDATION] ┌────────────────────┬──────────┬──────────┐");
  Serial.println("[BOOT_VALIDATION] │ Subsystem          │ Init     │ Health   │");
  Serial.println("[BOOT_VALIDATION] ├────────────────────┼──────────┼──────────┤");
  
  for (int i = 0; i < subsystem_count; i++) {
    Serial.print("[BOOT_VALIDATION] │ ");
    Serial.print(subsystems[i].subsystem_name);
    Serial.print(String(20 - strlen(subsystems[i].subsystem_name), ' '));
    Serial.print(" │ ");
    
    if (subsystems[i].initialized) {
      Serial.print("✅ OK     ");
      initialized_count++;
    } else {
      Serial.print("❌ FAIL   ");
      failed_count++;
    }
    
    Serial.print(" │ ");
    
    if (subsystems[i].healthy) {
      Serial.print("✅ OK     ");
      healthy_count++;
    } else {
      Serial.print("❌ BAD    ");
    }
    
    Serial.println("│");
  }
  
  Serial.println("[BOOT_VALIDATION] └────────────────────┴──────────┴──────────┘\n");
  
  Serial.print("[BOOT_VALIDATION] Summary: ");
  Serial.print(initialized_count);
  Serial.print("/");
  Serial.print(subsystem_count);
  Serial.print(" initialized, ");
  Serial.print(healthy_count);
  Serial.print(" healthy, ");
  Serial.print(failed_count);
  Serial.println(" failed\n");
  
  // Determine if we can proceed
  bool can_proceed = false;
  
  if (failed_count == 0) {
    // All systems OK
    Serial.println("[BOOT_VALIDATION] ✅ BOOT VALIDATION PASSED - All systems healthy!");
    boot_seq.boot_validation_passed = true;
    boot_seq.overall_status = BOOT_OK;
    
    // Increment consecutive successes
    if (boot_prefs.begin("bisso_boot", false)) { 
      uint32_t consecutive = boot_prefs.getUInt("consecutive_ok", 0) + 1;
      boot_prefs.putUInt("consecutive_ok", consecutive);
      boot_prefs.end();
    }
    
    can_proceed = true;
  } else if (healthy_count >= (subsystem_count - 2)) {
    // Most systems OK - can operate in degraded mode
    Serial.println("[BOOT_VALIDATION] ⚠️  DEGRADED MODE - Some systems failed");
    Serial.println("[BOOT_VALIDATION] ⚠️  System can operate with reduced functionality");
    
    degraded_mode = true;
    boot_seq.boot_validation_passed = true;
    boot_seq.overall_status = BOOT_OK;
    can_proceed = true;
  } else {
    // Too many failures
    Serial.println("[BOOT_VALIDATION] ❌ BOOT VALIDATION FAILED - Too many systems down!");
    boot_seq.boot_validation_passed = false;
    boot_seq.overall_status = BOOT_CRITICAL_ERROR;
    
    // Reset consecutive successes
    if (boot_prefs.begin("bisso_boot", false)) { 
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
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║              BOOT SEQUENCE STATUS                         ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Overall Status: ");
  if (boot_seq.boot_validation_passed) {
    if (degraded_mode) {
      Serial.println("⚠️  DEGRADED MODE");
    } else {
      Serial.println("✅ OK");
    }
  } else {
    Serial.println("❌ FAILED");
  }
  
  Serial.print("[BOOT] Total Boot Time: ");
  Serial.print(boot_seq.total_boot_time_ms);
  Serial.println(" ms");
  
  Serial.print("[BOOT] Systems Initialized: ");
  Serial.print(boot_seq.systems_initialized);
  Serial.print("/");
  Serial.println(subsystem_count);
  
  Serial.print("[BOOT] Systems Healthy: ");
  Serial.print(boot_seq.systems_healthy);
  Serial.print("/");
  Serial.println(subsystem_count);
  
  Serial.print("[BOOT] Systems Failed: ");
  Serial.println(boot_seq.systems_failed);
  
  Serial.println();
}

void bootShowDetailedReport() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║         DETAILED BOOT VALIDATION REPORT                  ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("[BOOT] Subsystem Details:");
  
  for (int i = 0; i < subsystem_count; i++) {
    Serial.print("[BOOT] ");
    Serial.print(subsystems[i].subsystem_name);
    Serial.print(": ");
    
    if (subsystems[i].initialized) {
      Serial.print("✅ Init");
    } else {
      Serial.print("❌ Failed");
    }
    
    Serial.print(" | Health: ");
    if (subsystems[i].healthy) {
      Serial.print("✅");
    } else {
      Serial.print("❌");
    }
    
    Serial.print(" | Time: ");
    Serial.print(subsystems[i].init_time_ms);
    Serial.print("ms | Errors: ");
    Serial.println(subsystems[i].error_count);
    
    if (subsystems[i].last_error) {
      Serial.print("[BOOT]   Error: ");
      Serial.println(subsystems[i].last_error);
    }
  }
  
  Serial.println();
}

void bootShowSubsystemHealth() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║           SUBSYSTEM HEALTH MONITOR                       ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  for (int i = 0; i < subsystem_count; i++) {
    Serial.print("[BOOT] ");
    Serial.print(subsystems[i].subsystem_name);
    Serial.print(": ");
    
    if (subsystems[i].healthy) {
      Serial.println("🟢 HEALTHY");
    } else {
      Serial.println("🔴 UNHEALTHY");
    }
  }
  
  Serial.println();
}

void bootShowInitTimes() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║          INITIALIZATION TIMING ANALYSIS                  ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("[BOOT] Subsystem              Init Time (ms)");
  Serial.println("[BOOT] ──────────────────────────────────────");
  
  for (int i = 0; i < subsystem_count; i++) {
    Serial.print("[BOOT] ");
    Serial.print(subsystems[i].subsystem_name);
    Serial.print(String(25 - strlen(subsystems[i].subsystem_name), ' '));
    Serial.println(subsystems[i].init_time_ms);
  }
  
  Serial.print("[BOOT] Total Boot Time: ");
  Serial.print(boot_seq.total_boot_time_ms);
  Serial.println(" ms\n");
}

// ============================================================================
// ERROR RECOVERY
// ============================================================================

bool bootAttemptRecovery(const char* subsystem) {
  Serial.print("[BOOT_RECOVERY] Attempting recovery of ");
  Serial.print(subsystem);
  Serial.println("...");
  
  faultLogWarning(FAULT_BOOT_RECOVERY_ATTEMPTED, subsystem);
  
  return false;
}

void bootHandleCriticalError(const char* error_msg) {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║              CRITICAL ERROR - SYSTEM HALT                ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Critical Error: ");
  Serial.println(error_msg);
  
  faultLogError(FAULT_CRITICAL_SYSTEM_ERROR, error_msg);
  bootLogErrors();
  
  Serial.println("[BOOT] ❌ System cannot proceed");
  Serial.println("[BOOT] Please check error logs and restart system\n");
  
  while (1) {
    delay(1000);
  }
}

void bootRebootSystem() {
  Serial.println("[BOOT] Gracefully rebooting system...");
  delay(1000);
  esp_restart();
}

void bootEmergencyHalt(const char* reason) {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║              EMERGENCY HALT - SYSTEM HALTED              ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Emergency Halt Reason: ");
  Serial.println(reason);
  
  faultLogError(FAULT_EMERGENCY_HALT, reason);
  
  shutting_down = true;
  
  while (1) {
    delay(10000);
  }
}

// ============================================================================
// DEGRADED MODE
// ============================================================================

bool bootCanOperateDegraded() {
  return (boot_seq.systems_failed <= 2);
}

void bootSetDegradedMode(bool enabled) {
  degraded_mode = enabled;
  
  if (enabled) {
    Serial.println("[BOOT] ⚠️  System operating in DEGRADED MODE");
    Serial.println("[BOOT] Some functionality may be unavailable");
  } else {
    Serial.println("[BOOT] Normal mode resumed");
  }
}

bool bootIsDegradedMode() {
  return degraded_mode;
}

void bootShowDegradedModeStatus() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║           DEGRADED MODE STATUS                          ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  if (!degraded_mode) {
    Serial.println("[BOOT] System running in NORMAL MODE");
    return;
  }
  
  Serial.println("[BOOT] System running in DEGRADED MODE\n");
  Serial.println("[BOOT] Available Subsystems:");
  
  for (int i = 0; i < subsystem_count; i++) {
    if (subsystems[i].healthy) {
      Serial.print("[BOOT]  ✅ ");
    } else {
      Serial.print("[BOOT]  ❌ ");
    }
    Serial.println(subsystems[i].subsystem_name);
  }
  
  Serial.println();
}

// ============================================================================
// BOOT DIAGNOSTICS & LOGGING
// ============================================================================

void bootLogErrors() {
  if (!boot_prefs.begin("bisso_boot", true)) {
    logError("[BOOT] Cannot read errors: NVS not started.");
    return;
  }
  
  uint32_t error_count = boot_prefs.getUInt("errors", 0);
  boot_prefs.end();

  Serial.print("[BOOT] Logged ");
  Serial.print(error_count);
  Serial.println(" boot errors to NVS");
}

const char* bootGetLastError() {
  for (int i = subsystem_count - 1; i >= 0; i--) {
    if (subsystems[i].last_error) {
      return subsystems[i].last_error;
    }
  }
  return "No errors";
}

uint32_t bootGetErrorCount() {
  if (boot_prefs.begin("bisso_boot", true)) {
    uint32_t count = boot_prefs.getUInt("errors", 0);
    boot_prefs.end();
    return count;
  }
  return 0;
}

uint32_t bootGetConsecutiveSuccesses() {
  if (boot_prefs.begin("bisso_boot", true)) {
    uint32_t count = boot_prefs.getUInt("consecutive_ok", 0);
    boot_prefs.end();
    return count;
  }
  return 0;
}

void bootShowRecoveryHistory() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║            BOOT RECOVERY HISTORY                         ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Consecutive Successful Boots: ");
  Serial.println(bootGetConsecutiveSuccesses());
  
  Serial.print("[BOOT] Total Boot Errors: ");
  Serial.println(bootGetErrorCount());
  
  Serial.println();
}

// ============================================================================
// GRACEFUL SHUTDOWN
// ============================================================================

void bootGracefulShutdown(const char* reason) {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║             GRACEFUL SYSTEM SHUTDOWN                    ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Shutdown Reason: ");
  Serial.println(reason);
  
  shutting_down = true;
  
  faultLogWarning(FAULT_GRACEFUL_SHUTDOWN, reason);
  
  Serial.println("[BOOT] System shutting down gracefully...");
  delay(2000);
  
  Serial.println("[BOOT] Safe to power off now");
}

bool bootIsShuttingDown() {
  return shutting_down;
}
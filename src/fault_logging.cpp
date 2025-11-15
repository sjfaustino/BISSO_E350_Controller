#include "fault_logging.h"
#include "config_unified.h"
#include <Preferences.h>

static Preferences fault_prefs;
static bool estop_active = false;
static bool estop_recovery_requested = false;
static uint32_t last_fault_id = 0;

#define FAULT_LOG_MAX_ENTRIES 32
#define FAULT_LOG_NAMESPACE "bisso_faults"

void faultLoggingInit() {
  Serial.println("[FAULT] Fault logging system initializing...");
  
  if (!fault_prefs.begin(FAULT_LOG_NAMESPACE, false)) {
    Serial.println("[FAULT] ERROR: Failed to initialize fault storage!");
    return;
  }
  
  // Get last fault ID from NVS
  last_fault_id = fault_prefs.getUInt("last_id", 0);
  
  // Load boot fault count
  uint32_t boot_count = fault_prefs.getUInt("boot_count", 0);
  fault_prefs.putUInt("boot_count", boot_count + 1);
  
  Serial.print("[FAULT] Fault storage initialized. Boot count: ");
  Serial.println(boot_count + 1);
}

const char* faultCodeToString(fault_code_t code) {
  switch(code) {
    case FAULT_NONE_CODE: return "NONE";
    case FAULT_ENCODER_TIMEOUT: return "ENCODER_TIMEOUT";
    case FAULT_PLC_COMM_LOSS: return "PLC_COMM_LOSS";
    case FAULT_MOTION_STALL: return "MOTION_STALL";
    case FAULT_SAFETY_INTERLOCK: return "SAFETY_INTERLOCK";
    case FAULT_SOFT_LIMIT_EXCEEDED: return "SOFT_LIMIT_EXCEEDED";
    case FAULT_ESTOP_ACTIVATED: return "ESTOP_ACTIVATED";
    case FAULT_POWER_LOSS: return "POWER_LOSS";
    case FAULT_TEMPERATURE_HIGH: return "TEMPERATURE_HIGH";
    case FAULT_CALIBRATION_MISSING: return "CALIBRATION_MISSING";
    case FAULT_CONFIGURATION_INVALID: return "CONFIGURATION_INVALID";
    default: return "UNKNOWN";
  }
}

const char* faultSeverityToString(fault_severity_t severity) {
  switch(severity) {
    case FAULT_NONE: return "NONE";
    case FAULT_WARNING: return "WARNING";
    case FAULT_ERROR: return "ERROR";
    case FAULT_CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
  }
}

void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* message) {
  // Create fault entry
  fault_entry_t entry;
  entry.timestamp = millis();
  entry.severity = severity;
  entry.code = code;
  entry.axis = axis;
  entry.value = value;
  strncpy(entry.message, message, sizeof(entry.message) - 1);
  
  // Log to serial first
  char timestamp_str[16];
  snprintf(timestamp_str, sizeof(timestamp_str), "%lu", entry.timestamp);
  
  Serial.print("[FAULT] ");
  Serial.print(faultSeverityToString(severity));
  Serial.print(" [");
  Serial.print(timestamp_str);
  Serial.print("ms] ");
  Serial.print(faultCodeToString(code));
  Serial.print(" - ");
  Serial.println(message);
  
  // Save to NVS
  last_fault_id++;
  char key[32];
  snprintf(key, sizeof(key), "fault_%lu_sev", last_fault_id);
  fault_prefs.putUChar(key, (uint8_t)severity);
  
  snprintf(key, sizeof(key), "fault_%lu_code", last_fault_id);
  fault_prefs.putUChar(key, (uint8_t)code);
  
  snprintf(key, sizeof(key), "fault_%lu_axis", last_fault_id);
  fault_prefs.putInt(key, axis);
  
  snprintf(key, sizeof(key), "fault_%lu_val", last_fault_id);
  fault_prefs.putInt(key, value);
  
  snprintf(key, sizeof(key), "fault_%lu_msg", last_fault_id);
  fault_prefs.putString(key, message);
  
  snprintf(key, sizeof(key), "fault_%lu_ts", last_fault_id);
  fault_prefs.putULong(key, entry.timestamp);
  
  // Update last ID
  fault_prefs.putUInt("last_id", last_fault_id);
  
  // Log count by severity
  char count_key[32];
  snprintf(count_key, sizeof(count_key), "count_%s", faultSeverityToString(severity));
  uint32_t count = fault_prefs.getUInt(count_key, 0);
  fault_prefs.putUInt(count_key, count + 1);
}

void faultLogWarning(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_WARNING, code, -1, 0, message);
}

void faultLogError(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_ERROR, code, -1, 0, message);
}

void faultLogCritical(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_CRITICAL, code, -1, 0, message);
  estop_active = true;  // Auto-trigger estop on critical fault
}

void faultShowHistory() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║                      FAULT HISTORY (NVS)                      ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝");
  
  // Show statistics
  Serial.println("\n[STATS] Fault Summary:");
  Serial.print("  Total Faults: ");
  Serial.println(last_fault_id);
  
  Serial.print("  Warnings: ");
  Serial.println(fault_prefs.getUInt("count_WARNING", 0));
  
  Serial.print("  Errors: ");
  Serial.println(fault_prefs.getUInt("count_ERROR", 0));
  
  Serial.print("  Critical: ");
  Serial.println(fault_prefs.getUInt("count_CRITICAL", 0));
  
  Serial.print("  Boot Count: ");
  Serial.println(fault_prefs.getUInt("boot_count", 0));
  
  // Show recent faults
  Serial.println("\n[RECENT] Last 10 Faults:");
  uint32_t start = (last_fault_id > 10) ? last_fault_id - 10 : 1;
  
  for (uint32_t i = start; i <= last_fault_id; i++) {
    char key[32];
    snprintf(key, sizeof(key), "fault_%lu_ts", i);
    if (fault_prefs.isKey(key)) {
      snprintf(key, sizeof(key), "fault_%lu_sev", i);
      uint8_t sev = fault_prefs.getUChar(key);
      
      snprintf(key, sizeof(key), "fault_%lu_code", i);
      uint8_t code = fault_prefs.getUChar(key);
      
      snprintf(key, sizeof(key), "fault_%lu_msg", i);
      String msg = fault_prefs.getString(key, "?");
      
      snprintf(key, sizeof(key), "fault_%lu_ts", i);
      uint32_t ts = fault_prefs.getULong(key);
      
      Serial.print("  [");
      Serial.print(i);
      Serial.print("] ");
      Serial.print(ts);
      Serial.print("ms ");
      Serial.print(faultSeverityToString((fault_severity_t)sev));
      Serial.print(" - ");
      Serial.print(faultCodeToString((fault_code_t)code));
      Serial.print(": ");
      Serial.println(msg);
    }
  }
  
  Serial.println();
}

void faultClearHistory() {
  Serial.println("[FAULT] Clearing fault history from NVS...");
  
  // Clear all fault entries
  for (uint32_t i = 1; i <= last_fault_id; i++) {
    for (int j = 0; j < 6; j++) {
      char key[32];
      switch(j) {
        case 0: snprintf(key, sizeof(key), "fault_%lu_sev", i); break;
        case 1: snprintf(key, sizeof(key), "fault_%lu_code", i); break;
        case 2: snprintf(key, sizeof(key), "fault_%lu_axis", i); break;
        case 3: snprintf(key, sizeof(key), "fault_%lu_val", i); break;
        case 4: snprintf(key, sizeof(key), "fault_%lu_msg", i); break;
        case 5: snprintf(key, sizeof(key), "fault_%lu_ts", i); break;
      }
      fault_prefs.remove(key);
    }
  }
  
  // Reset counters
  fault_prefs.remove("count_WARNING");
  fault_prefs.remove("count_ERROR");
  fault_prefs.remove("count_CRITICAL");
  last_fault_id = 0;
  fault_prefs.putUInt("last_id", 0);
  
  Serial.println("[FAULT] ✅ Fault history cleared");
}

// Boot validation functions are implemented in boot_validation.cpp
// These declarations are here for reference only:
// - bootValidationInit()
// - bootValidateAllSystems()
// - bootShowStatus()

// Emergency stop management
void emergencyStopSetActive(bool active) {
  if (active && !estop_active) {
    estop_active = true;
    faultLogCritical(FAULT_ESTOP_ACTIVATED, "Emergency stop activated");
    Serial.println("\n⚠️  EMERGENCY STOP ACTIVATED");
    Serial.println("System halted. Operator confirmation required for recovery.");
  } else if (!active && estop_active) {
    estop_active = false;
    Serial.println("🟢 Emergency stop deactivated");
  }
}

bool emergencyStopIsActive() {
  return estop_active;
}

bool emergencyStopRequestRecovery() {
  if (!estop_active) {
    Serial.println("[ESTOP] No active emergency stop to recover from");
    return false;
  }
  
  estop_recovery_requested = true;
  Serial.println("[ESTOP] Recovery requested - awaiting operator confirmation...");
  return true;
}

void emergencyStopClearRecovery() {
  estop_recovery_requested = false;
  estop_active = false;
  Serial.println("[ESTOP] ✅ Emergency stop cleared - system ready");
}

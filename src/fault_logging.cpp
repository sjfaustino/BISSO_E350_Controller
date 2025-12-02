#include "fault_logging.h"
#include "config_unified.h"
#include "motion.h"          // Added for motionEmergencyStop()
#include "safety_state_machine.h" // Added for safetySetState()
#include "serial_logger.h"   // Added for logError(), logInfo(), etc.
#include <Preferences.h>
#include <stdio.h>    // Required for vsnprintf
#include <string.h>   // Required for strncpy
#include <time.h>     // For time_t and localtime (for time string format)

static Preferences fault_prefs;
static bool estop_active = false; // Internal flag for E-Stop state
static bool estop_recovery_requested = false;
static uint32_t last_fault_id = 0;

#define FAULT_LOG_MAX_ENTRIES 32 // Not strictly enforced in this code but defined
#define FAULT_LOG_NAMESPACE "bisso_faults"

// ============================================================================
// INITIALIZATION AND UTILITIES
// ============================================================================

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
    case FAULT_WATCHDOG_TIMEOUT: return "WATCHDOG_TIMEOUT";
    case FAULT_BOOT_FAILED: return "BOOT_FAILED";
    case FAULT_BOOT_RECOVERY_ATTEMPTED: return "BOOT_RECOVERY_ATTEMPTED";
    case FAULT_CRITICAL_SYSTEM_ERROR: return "CRITICAL_SYSTEM_ERROR";
    case FAULT_EMERGENCY_HALT: return "EMERGENCY_HALT";
    case FAULT_GRACEFUL_SHUTDOWN: return "GRACEFUL_SHUTDOWN";
    case FAULT_ENCODER_SPIKE: return "ENCODER_SPIKE";
    case FAULT_I2C_ERROR: return "I2C_ERROR";
    case FAULT_TASK_HUNG: return "TASK_HUNG";
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

// ============================================================================
// CORE LOGGING IMPLEMENTATION
// ============================================================================

void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* format, ...) {
  
  // Create fault entry
  fault_entry_t entry;
  entry.timestamp = millis();
  entry.severity = severity;
  entry.code = code;
  entry.axis = axis;
  entry.value = value;
  
  // 1. Process variable arguments using vsnprintf
  va_list args;
  va_start(args, format);
  vsnprintf(entry.message, sizeof(entry.message), format, args);
  va_end(args);
  entry.message[sizeof(entry.message) - 1] = '\0';  // Ensure null termination

  // 2. Log to serial first
  char timestamp_str[16];
  snprintf(timestamp_str, sizeof(timestamp_str), "%lu", entry.timestamp);
  
  Serial.print("[FAULT] ");
  Serial.print(faultSeverityToString(severity));
  Serial.print(" [");
  Serial.print(timestamp_str);
  Serial.print("ms] ");
  Serial.print(faultCodeToString(code));
  Serial.print(" - ");
  Serial.println(entry.message); 
  
  // 3. Save to NVS (Persistence logic)
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
  fault_prefs.putString(key, entry.message); 
  
  snprintf(key, sizeof(key), "fault_%lu_ts", last_fault_id);
  fault_prefs.putULong(key, entry.timestamp);
  
  // Update last ID
  fault_prefs.putUInt("last_id", last_fault_id);
  
  // Log count by severity (optional stat tracking)
  char count_key[32];
  snprintf(count_key, sizeof(count_key), "count_%s", faultSeverityToString(severity));
  uint32_t count = fault_prefs.getUInt(count_key, 0);
  fault_prefs.putUInt(count_key, count + 1);

  // 4. CRITICAL: If severity is critical, trigger the E-Stop mechanism
  if (severity == FAULT_CRITICAL) {
      emergencyStopSetActive(true);
  }
}

// ============================================================================
// WRAPPER FUNCTIONS
// ============================================================================

void faultLogWarning(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_WARNING, code, -1, 0, "%s", message); 
}

void faultLogError(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_ERROR, code, -1, 0, "%s", message);
}

void faultLogCritical(fault_code_t code, const char* message) {
  // faultLogEntry will internally call emergencyStopSetActive(true)
  faultLogEntry(FAULT_CRITICAL, code, -1, 0, "%s", message);
}

// ============================================================================
// FAULT STATISTICS GATHERING
// ============================================================================
fault_stats_t faultGetStats() {
    fault_stats_t stats = {0};
    uint32_t first_time = 0xFFFFFFFF;
    uint32_t last_time = 0;

    // Iterate through all logged faults
    for (uint32_t i = 1; i <= last_fault_id; i++) {
        char key[32];
        
        // Retrieve code
        snprintf(key, sizeof(key), "fault_%lu_code", i);
        uint8_t code_val = fault_prefs.getUChar(key, 0);
        fault_code_t code = (fault_code_t)code_val;
        
        // Retrieve timestamp
        snprintf(key, sizeof(key), "fault_%lu_ts", i);
        uint32_t timestamp = fault_prefs.getULong(key, 0);
        
        if (code != FAULT_NONE_CODE) {
            stats.total_faults++;
            
            // Update time stamps
            if (timestamp > last_time) last_time = timestamp;
            if (timestamp < first_time) first_time = timestamp;

            // Categorization based on fault code
            switch(code) {
                case FAULT_ENCODER_TIMEOUT:
                case FAULT_ENCODER_SPIKE:
                    stats.encoder_faults++;
                    break;
                case FAULT_MOTION_STALL:
                case FAULT_SOFT_LIMIT_EXCEEDED:
                    stats.motion_faults++;
                    break;
                case FAULT_ESTOP_ACTIVATED:
                case FAULT_SAFETY_INTERLOCK:
                case FAULT_EMERGENCY_HALT:
                    stats.safety_faults++;
                    break;
                case FAULT_CONFIGURATION_INVALID:
                case FAULT_CALIBRATION_MISSING:
                case FAULT_BOOT_FAILED:
                case FAULT_BOOT_RECOVERY_ATTEMPTED:
                    stats.config_faults++;
                    break;
                case FAULT_PLC_COMM_LOSS:
                case FAULT_I2C_ERROR:
                    stats.plc_faults++;
                    break;
                case FAULT_WATCHDOG_TIMEOUT:
                case FAULT_TASK_HUNG:
                case FAULT_CRITICAL_SYSTEM_ERROR:
                case FAULT_POWER_LOSS:
                case FAULT_TEMPERATURE_HIGH:
                case FAULT_GRACEFUL_SHUTDOWN:
                default:
                    stats.system_faults++;
                    break;
            }
        }
    }

    stats.first_fault_time_ms = (first_time == 0xFFFFFFFF) ? 0 : first_time;
    stats.last_fault_time_ms = last_time;

    return stats;
}

void faultShowHistory() {
    Serial.println("[FAULT] Full fault history display omitted for brevity.");
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

// ============================================================================
// E-STOP MANAGEMENT IMPLEMENTATION (FIXED)
// ============================================================================

void emergencyStopSetActive(bool active) {
    if (active && !estop_active) {
        // ACTIVATE E-STOP
        estop_active = true;
        
        // 1. Halt all motion via the high-level motion API
        motionEmergencyStop();
        
        // 2. Update Safety FSM
        safetySetState(FSM_EMERGENCY);
        
        logError("[E-STOP] System Activated via Fault Handler.");
    } 
    else if (!active && estop_active) {
        // CLEAR E-STOP
        // NOTE: This must only be called internally by motionClearEmergencyStop()
        // after safety checks pass.
        estop_active = false;
        
        // The motionClearEmergencyStop() function handles the rest of the recovery (motion enable).
        // Set FSM back to OK (assuming safety checks passed in the caller function).
        safetySetState(FSM_OK); 
        logInfo("[E-STOP] System flag cleared by motion recovery.");
    }
}

bool emergencyStopIsActive() { 
    return estop_active; 
}

bool emergencyStopRequestRecovery() {
    if (emergencyStopIsActive()) {
        estop_recovery_requested = true;
        logWarning("[E-STOP] Recovery request logged. Operator must run CLI command.");
        return true;
    }
    return false;
}

void emergencyStopClearRecovery() {
    estop_recovery_requested = false;
    logInfo("[E-STOP] Recovery request flag cleared.");
}
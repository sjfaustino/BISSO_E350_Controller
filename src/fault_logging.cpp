#include "fault_logging.h"
#include "config_unified.h"
#include "motion.h"          
#include "safety_state_machine.h" 
#include "serial_logger.h"   
#include "task_manager.h"    
#include <Preferences.h>
#include <stdio.h>    
#include <string.h>   
#include <time.h>     

static Preferences fault_prefs;
static bool estop_active = false; 
static bool estop_recovery_requested = false;
static uint32_t last_fault_id = 0;

#define FAULT_LOG_NAMESPACE "bisso_faults"

void faultLoggingInit() {
  Serial.println("[FAULT] Initializing...");
  if (!fault_prefs.begin(FAULT_LOG_NAMESPACE, false)) {
    Serial.println("[FAULT] [FAIL] NVS init failed!");
    return;
  }
  last_fault_id = fault_prefs.getUInt("last_id", 0);
  uint32_t boot_count = fault_prefs.getUInt("boot_count", 0);
  fault_prefs.putUInt("boot_count", boot_count + 1);
  Serial.printf("[FAULT] [OK] Ready. Boot count: %lu\n", boot_count + 1);
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
    case FAULT_CONFIGURATION_INVALID: return "CONFIG_INVALID";
    case FAULT_WATCHDOG_TIMEOUT: return "WDT_TIMEOUT";
    case FAULT_BOOT_FAILED: return "BOOT_FAILED";
    case FAULT_BOOT_RECOVERY_ATTEMPTED: return "BOOT_RECOVERY";
    case FAULT_CRITICAL_SYSTEM_ERROR: return "CRITICAL_ERROR";
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
    case FAULT_WARNING: return "WARN";
    case FAULT_ERROR: return "ERROR";
    case FAULT_CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
  }
}

// 1. ASYNC FRONTEND
void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* format, ...) {
  char msg_buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
  va_end(args);
  msg_buffer[sizeof(msg_buffer) - 1] = '\0';

  // Immediate Print
  Serial.printf("[FAULT_RT] %s: %s\n", faultSeverityToString(severity), msg_buffer);

  // Queue Push
  queue_message_t msg;
  msg.type = MSG_FAULT_LOGGED;
  msg.timestamp = millis();
  
  fault_entry_t* payload = (fault_entry_t*)msg.data;
  payload->severity = severity;
  payload->code = code;
  payload->axis = axis;
  payload->value = value;
  payload->timestamp = msg.timestamp;
  strncpy(payload->message, msg_buffer, 63);
  payload->message[63] = '\0';

  if (!taskSendMessage(taskGetFaultQueue(), &msg)) {
      Serial.println("[FAULT_RT] [WARN] Queue Full! Log dropped.");
  }

  if (severity == FAULT_CRITICAL) {
      emergencyStopSetActive(true);
  }
}

// 2. BLOCKING BACKEND (Task Only)
void faultLogToNVS(const fault_entry_t* entry) {
  if (!entry) return;
  last_fault_id++;
  char key[32];
  
  snprintf(key, sizeof(key), "fault_%lu_sev", last_fault_id);
  fault_prefs.putUChar(key, (uint8_t)entry->severity);
  snprintf(key, sizeof(key), "fault_%lu_code", last_fault_id);
  fault_prefs.putUChar(key, (uint8_t)entry->code);
  snprintf(key, sizeof(key), "fault_%lu_axis", last_fault_id);
  fault_prefs.putInt(key, entry->axis);
  snprintf(key, sizeof(key), "fault_%lu_val", last_fault_id);
  fault_prefs.putInt(key, entry->value);
  snprintf(key, sizeof(key), "fault_%lu_msg", last_fault_id);
  fault_prefs.putString(key, entry->message); 
  snprintf(key, sizeof(key), "fault_%lu_ts", last_fault_id);
  fault_prefs.putULong(key, entry->timestamp);
  
  fault_prefs.putUInt("last_id", last_fault_id);
  
  char count_key[32];
  snprintf(count_key, sizeof(count_key), "count_%s", faultSeverityToString(entry->severity));
  fault_prefs.putUInt(count_key, fault_prefs.getUInt(count_key, 0) + 1);
}

// Wrapper Functions
void faultLogWarning(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_WARNING, code, -1, 0, "%s", message); 
}
void faultLogError(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_ERROR, code, -1, 0, "%s", message);
}
void faultLogCritical(fault_code_t code, const char* message) {
  faultLogEntry(FAULT_CRITICAL, code, -1, 0, "%s", message);
}

fault_stats_t faultGetStats() {
    fault_stats_t stats = {0};
    uint32_t first_time = 0xFFFFFFFF;
    uint32_t last_time = 0;

    for (uint32_t i = 1; i <= last_fault_id; i++) {
        char key[32];
        snprintf(key, sizeof(key), "fault_%lu_code", i);
        uint8_t code_val = fault_prefs.getUChar(key, 0);
        fault_code_t code = (fault_code_t)code_val;
        
        snprintf(key, sizeof(key), "fault_%lu_ts", i);
        uint32_t timestamp = fault_prefs.getULong(key, 0);
        
        if (code != FAULT_NONE_CODE) {
            stats.total_faults++;
            if (timestamp > last_time) last_time = timestamp;
            if (timestamp < first_time) first_time = timestamp;

            switch(code) {
                case FAULT_ENCODER_TIMEOUT: case FAULT_ENCODER_SPIKE: stats.encoder_faults++; break;
                case FAULT_MOTION_STALL: case FAULT_SOFT_LIMIT_EXCEEDED: stats.motion_faults++; break;
                case FAULT_ESTOP_ACTIVATED: case FAULT_SAFETY_INTERLOCK: case FAULT_EMERGENCY_HALT: stats.safety_faults++; break;
                case FAULT_PLC_COMM_LOSS: case FAULT_I2C_ERROR: stats.plc_faults++; break;
                default: stats.system_faults++; break;
            }
        }
    }
    stats.first_fault_time_ms = (first_time == 0xFFFFFFFF) ? 0 : first_time;
    stats.last_fault_time_ms = last_time;
    return stats;
}

void faultShowHistory() {
    Serial.println("[FAULT] Full history dump via CLI only.");
}

void faultClearHistory() {
    Serial.println("[FAULT] Clearing history...");
    for (uint32_t i = 1; i <= last_fault_id; i++) {
      for (int j = 0; j < 6; j++) {
        char key[32];
        const char* suffixes[] = {"sev", "code", "axis", "val", "msg", "ts"};
        snprintf(key, sizeof(key), "fault_%lu_%s", i, suffixes[j]);
        fault_prefs.remove(key);
      }
    }
    fault_prefs.remove("count_WARNING");
    fault_prefs.remove("count_ERROR");
    fault_prefs.remove("count_CRITICAL");
    last_fault_id = 0;
    fault_prefs.putUInt("last_id", 0);
    Serial.println("[FAULT] [OK] Cleared.");
}

void emergencyStopSetActive(bool active) {
    if (active && !estop_active) {
        estop_active = true;
        motionEmergencyStop();
        safetySetState(FSM_EMERGENCY);
        logError("[E-STOP] ACTIVATED via Fault.");
    } 
    else if (!active && estop_active) {
        estop_active = false;
        safetySetState(FSM_OK); 
        logInfo("[E-STOP] CLEARED.");
    }
}

bool emergencyStopIsActive() { return estop_active; }

bool emergencyStopRequestRecovery() {
    if (emergencyStopIsActive()) {
        estop_recovery_requested = true;
        logWarning("[E-STOP] Recovery requested.");
        return true;
    }
    return false;
}

void emergencyStopClearRecovery() {
    estop_recovery_requested = false;
}
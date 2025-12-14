#include "fault_logging.h"
#include "config_unified.h"
#include "motion.h"
#include "safety_state_machine.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "log_rate_limiter.h"  // PHASE 2.5: Rate limit duplicate faults
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static Preferences fault_prefs;
static bool estop_active = false;
static bool estop_recovery_requested = false;
static uint32_t last_fault_id = 0;

#define FAULT_LOG_NAMESPACE "bisso_faults"

// PHASE 5.1: Ring buffer fallback for queue overflow
#define FAULT_RING_BUFFER_SIZE 8
static struct {
    fault_entry_t entries[FAULT_RING_BUFFER_SIZE];
    uint8_t head;
    uint8_t count;
    uint32_t total_dropped;
} fault_ring_buffer = {
    .entries = {0},
    .head = 0,
    .count = 0,
    .total_dropped = 0
};

// PHASE 5.1: Add fault to ring buffer fallback
static void faultAddToRingBuffer(const fault_entry_t* entry) {
    if (!entry) return;

    uint8_t idx = (fault_ring_buffer.head + fault_ring_buffer.count) % FAULT_RING_BUFFER_SIZE;
    memcpy(&fault_ring_buffer.entries[idx], entry, sizeof(fault_entry_t));

    if (fault_ring_buffer.count < FAULT_RING_BUFFER_SIZE) {
        fault_ring_buffer.count++;
    } else {
        // Buffer full - overwrite oldest entry
        fault_ring_buffer.head = (fault_ring_buffer.head + 1) % FAULT_RING_BUFFER_SIZE;
    }

    fault_ring_buffer.total_dropped++;

    // Log critical faults even when ring buffer is used
    if (entry->severity == FAULT_CRITICAL) {
        logError("[FAULT] [CRITICAL] Added to ring buffer (total buffered: %lu)",
                (unsigned long)fault_ring_buffer.total_dropped);
    }
}

void faultLoggingInit() {
  logInfo("[FAULT] Initializing...");

  if (!fault_prefs.begin(FAULT_LOG_NAMESPACE, false)) {
    logError("[FAULT] [FAIL] NVS init failed!");
    return;
  }

  last_fault_id = fault_prefs.getUInt("last_id", 0);
  uint32_t boot_count = fault_prefs.getUInt("boot_count", 0);
  fault_prefs.putUInt("boot_count", boot_count + 1);

  // PHASE 2.5: Initialize rate limiter to prevent duplicate fault log flooding
  logRateLimiterInit();

  logInfo("[FAULT] [OK] Ready. Boot count: %u", boot_count + 1);
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
    case FAULT_WATCHDOG_TIMEOUT: return "WATCHDOG_TIMEOUT";
    case FAULT_BOOT_FAILED: return "BOOT_FAILED";
    case FAULT_BOOT_RECOVERY_ATTEMPTED: return "BOOT_RECOVERY";
    case FAULT_CRITICAL_SYSTEM_ERROR: return "CRITICAL_ERROR";
    case FAULT_EMERGENCY_HALT: return "EMERGENCY_HALT";
    case FAULT_GRACEFUL_SHUTDOWN: return "GRACEFUL_SHUTDOWN";
    case FAULT_ENCODER_SPIKE: return "ENCODER_SPIKE";
    case FAULT_I2C_ERROR: return "I2C_ERROR";
    case FAULT_TASK_HUNG: return "TASK_HUNG";
    case FAULT_MOTION_TIMEOUT: return "MOTION_TIMEOUT";  // PHASE 5.1
    case FAULT_SPINDLE_OVERCURRENT: return "SPINDLE_OVERCURRENT";  // PHASE 5.1
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

void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* format, ...) {
  char msg_buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
  va_end(args);
  msg_buffer[sizeof(msg_buffer) - 1] = '\0';

  // PHASE 2.5: Rate limit duplicate faults to prevent log flooding
  // Critical faults always log, but we check rate limiter for throttling warnings
  bool should_log = true;
  if (severity != FAULT_CRITICAL) {
    // Use fault code as ID, use axis as sub_id for per-axis tracking
    int16_t rate_limit_sub_id = (axis >= 0 && axis < 10) ? axis : -1;
    should_log = logRateLimiterCheck((uint16_t)code, rate_limit_sub_id);

    if (!should_log) {
      // Suppress duplicate, but note in diagnostics
      return;
    }
  }

  Serial.printf("[FAULT_RT] %s: %s\n", faultSeverityToString(severity), msg_buffer);

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
      // PHASE 5.1: Use ring buffer fallback when queue is full
      Serial.println("[FAULT_RT] [WARN] Queue Full! Using ring buffer fallback.");
      faultAddToRingBuffer(payload);
  }

  if (severity == FAULT_CRITICAL) {
      emergencyStopSetActive(true);
  }
}

void faultLogToNVS(const fault_entry_t* entry) {
  if (!entry) return;
  last_fault_id++;
  char key[32];
  
  snprintf(key, sizeof(key), "fault_%lu_sev", (unsigned long)last_fault_id);
  fault_prefs.putUChar(key, (uint8_t)entry->severity);
  
  snprintf(key, sizeof(key), "fault_%lu_code", (unsigned long)last_fault_id);
  fault_prefs.putUChar(key, (uint8_t)entry->code);
  
  snprintf(key, sizeof(key), "fault_%lu_axis", (unsigned long)last_fault_id);
  fault_prefs.putInt(key, entry->axis);
  
  snprintf(key, sizeof(key), "fault_%lu_val", (unsigned long)last_fault_id);
  fault_prefs.putInt(key, entry->value);
  
  snprintf(key, sizeof(key), "fault_%lu_msg", (unsigned long)last_fault_id);
  fault_prefs.putString(key, entry->message); 
  
  snprintf(key, sizeof(key), "fault_%lu_ts", (unsigned long)last_fault_id);
  fault_prefs.putULong(key, entry->timestamp);
  
  fault_prefs.putUInt("last_id", last_fault_id);
}

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
    // FIX: Fully initialize struct to prevent "missing initializer" warnings
    fault_stats_t stats = {0, 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFF, 0};
    
    uint32_t first_time = 0xFFFFFFFF;
    uint32_t last_time = 0;

    for (uint32_t i = 1; i <= last_fault_id; i++) {
        char key[32];
        snprintf(key, sizeof(key), "fault_%lu_code", (unsigned long)i);
        if (!fault_prefs.isKey(key)) continue;

        uint8_t code_val = fault_prefs.getUChar(key, 0);
        fault_code_t code = (fault_code_t)code_val;
        
        snprintf(key, sizeof(key), "fault_%lu_ts", (unsigned long)i);
        uint32_t timestamp = fault_prefs.getULong(key, 0);
        
        if (code != FAULT_NONE_CODE) {
            stats.total_faults++;
            if (timestamp > last_time) last_time = timestamp;
            if (timestamp < first_time) first_time = timestamp;

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
    Serial.println("[FAULT] Full history dump via CLI only.");
}

void faultClearHistory() {
    Serial.println("[FAULT] Clearing history...");
    // NVS Clear logic...
    fault_prefs.clear(); 
    fault_prefs.putUInt("last_id", 0);
    last_fault_id = 0;
    Serial.println("[FAULT] [OK] Cleared");
}

void emergencyStopSetActive(bool active) {
    if (active && !estop_active) {
        estop_active = true;
        motionEmergencyStop();
        safetySetState(FSM_EMERGENCY);
        logError("[E-STOP] ACTIVATED via Fault");
    } 
    else if (!active && estop_active) {
        estop_active = false;
        safetySetState(FSM_OK); 
        logInfo("[E-STOP] CLEARED");
    }
}

bool emergencyStopIsActive() { return estop_active; }

bool emergencyStopRequestRecovery() {
    if (emergencyStopIsActive()) {
        estop_recovery_requested = true;
        return true;
    }
    return false;
}

void emergencyStopClearRecovery() {
    estop_recovery_requested = false;
}

// PHASE 5.1: Ring buffer diagnostics functions
uint32_t faultGetRingBufferDropCount() {
    return fault_ring_buffer.total_dropped;
}

uint8_t faultGetRingBufferEntryCount() {
    return fault_ring_buffer.count;
}

const fault_entry_t* faultGetRingBufferEntry(uint8_t index) {
    if (index >= fault_ring_buffer.count) {
        return NULL;
    }
    uint8_t actual_idx = (fault_ring_buffer.head + index) % FAULT_RING_BUFFER_SIZE;
    return &fault_ring_buffer.entries[actual_idx];
}

#pragma GCC diagnostic pop
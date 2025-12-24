#include "fault_logging.h"
#include "config_unified.h"
#include "log_rate_limiter.h"
#include "motion.h"
#include "safety_state_machine.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static Preferences fault_prefs;
static bool estop_active = false;
static bool estop_recovery_requested = false;

#define FAULT_LOG_NAMESPACE "bisso_faults"
#define FAULT_LOG_VERSION 2 // Increment to force migration/clear

// Maximum number of fault entries to keep in NVS
#define MAX_FAULT_ENTRIES_NVS 50

// NVS Blob Keys
#define KEY_HEADER "header"
#define KEY_FAULT_PREFIX "flt_" // Key will be flt_0, flt_1, ...

typedef struct {
  uint8_t version;
  uint8_t head;
  uint8_t count;
  uint8_t reserved; // Alignment
  uint32_t total_lifetime_faults;
} fault_log_header_t;

static fault_log_header_t log_header = {0};

// PHASE 5.1: Ring buffer fallback for queue overflow
#define FAULT_RING_BUFFER_SIZE 8
static struct {
  fault_entry_t entries[FAULT_RING_BUFFER_SIZE];
  uint8_t head;
  uint8_t count;
  uint32_t total_dropped;
} fault_ring_buffer = {
    .entries = {0}, .head = 0, .count = 0, .total_dropped = 0};

// PHASE 5.1: Add fault to ring buffer fallback
static void faultAddToRingBuffer(const fault_entry_t *entry) {
  if (!entry)
    return;

  uint8_t idx = (fault_ring_buffer.head + fault_ring_buffer.count) %
                FAULT_RING_BUFFER_SIZE;
  memcpy(&fault_ring_buffer.entries[idx], entry, sizeof(fault_entry_t));

  if (fault_ring_buffer.count < FAULT_RING_BUFFER_SIZE) {
    fault_ring_buffer.count++;
  } else {
    // Buffer full - overwrite oldest entry
    fault_ring_buffer.head =
        (fault_ring_buffer.head + 1) % FAULT_RING_BUFFER_SIZE;
  }

  fault_ring_buffer.total_dropped++;

  // Log critical faults even when ring buffer is used
  if (entry->severity == FAULT_CRITICAL) {
    logError("[FAULT] [CRITICAL] Added to ring buffer (total buffered: %lu)",
             (unsigned long)fault_ring_buffer.total_dropped);
  }
}

void faultLoggingInit() {
  logInfo("[FAULT] Initializing v%d...", FAULT_LOG_VERSION);

  if (!fault_prefs.begin(FAULT_LOG_NAMESPACE, false)) {
    logError("[FAULT] [FAIL] NVS init failed! Attempting recovery...");
    if (fault_prefs.begin(FAULT_LOG_NAMESPACE, false)) {
      fault_prefs.clear();
      fault_prefs.end();
      if (!fault_prefs.begin(FAULT_LOG_NAMESPACE, false)) {
        logError("[FAULT] [FAIL] Recovery failed. Logging disabled.");
        return;
      }
    } else {
      return;
    }
  }

  // Check version / migration
  bool header_valid =
      fault_prefs.getBytes(KEY_HEADER, &log_header, sizeof(log_header)) ==
      sizeof(log_header);

  if (!header_valid || log_header.version != FAULT_LOG_VERSION) {
    logWarning(
        "[FAULT] Schema mismatch or invalid header. Formatting Fault Log...");
    fault_prefs.clear();
    memset(&log_header, 0, sizeof(log_header));
    log_header.version = FAULT_LOG_VERSION;
    fault_prefs.putBytes(KEY_HEADER, &log_header, sizeof(log_header));
  }

  // PHASE 2.5: Initialize rate limiter
  logRateLimiterInit();

  logInfo("[FAULT] [OK] Ready. Faults: %d, Total Lifetime: %lu",
          log_header.count, (unsigned long)log_header.total_lifetime_faults);
}

const char *faultCodeToString(fault_code_t code) {
  switch (code) {
  case FAULT_NONE_CODE:
    return "NONE";
  case FAULT_ENCODER_TIMEOUT:
    return "ENCODER_TIMEOUT";
  case FAULT_PLC_COMM_LOSS:
    return "PLC_COMM_LOSS";
  case FAULT_MOTION_STALL:
    return "MOTION_STALL";
  case FAULT_SAFETY_INTERLOCK:
    return "SAFETY_INTERLOCK";
  case FAULT_SOFT_LIMIT_EXCEEDED:
    return "SOFT_LIMIT_EXCEEDED";
  case FAULT_ESTOP_ACTIVATED:
    return "ESTOP_ACTIVATED";
  case FAULT_POWER_LOSS:
    return "POWER_LOSS";
  case FAULT_TEMPERATURE_HIGH:
    return "TEMPERATURE_HIGH";
  case FAULT_CALIBRATION_MISSING:
    return "CALIBRATION_MISSING";
  case FAULT_CONFIGURATION_INVALID:
    return "CONFIG_INVALID";
  case FAULT_WATCHDOG_TIMEOUT:
    return "WATCHDOG_TIMEOUT";
  case FAULT_BOOT_FAILED:
    return "BOOT_FAILED";
  case FAULT_BOOT_RECOVERY_ATTEMPTED:
    return "BOOT_RECOVERY";
  case FAULT_CRITICAL_SYSTEM_ERROR:
    return "CRITICAL_ERROR";
  case FAULT_EMERGENCY_HALT:
    return "EMERGENCY_HALT";
  case FAULT_GRACEFUL_SHUTDOWN:
    return "GRACEFUL_SHUTDOWN";
  case FAULT_ENCODER_SPIKE:
    return "ENCODER_SPIKE";
  case FAULT_I2C_ERROR:
    return "I2C_ERROR";
  case FAULT_TASK_HUNG:
    return "TASK_HUNG";
  case FAULT_MOTION_TIMEOUT:
    return "MOTION_TIMEOUT";
  case FAULT_SPINDLE_OVERCURRENT:
    return "SPINDLE_OVERCURRENT";
  default:
    return "UNKNOWN";
  }
}

const char *faultSeverityToString(fault_severity_t severity) {
  switch (severity) {
  case FAULT_NONE:
    return "NONE";
  case FAULT_WARNING:
    return "WARN";
  case FAULT_ERROR:
    return "ERROR";
  case FAULT_CRITICAL:
    return "CRITICAL";
  default:
    return "UNKNOWN";
  }
}

void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis,
                   int32_t value, const char *format, ...) {
  char msg_buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
  va_end(args);
  msg_buffer[sizeof(msg_buffer) - 1] = '\0';

  // Rate Limiting
  if (severity != FAULT_CRITICAL) {
    int16_t rate_limit_sub_id = (axis >= 0 && axis < 10) ? axis : -1;
    if (!logRateLimiterCheck((uint16_t)code, rate_limit_sub_id))
      return;
  }

  Serial.printf("[FAULT_RT] %s: %s\n", faultSeverityToString(severity),
                msg_buffer);

  queue_message_t msg;
  msg.type = MSG_FAULT_LOGGED;
  msg.timestamp = millis();

  fault_entry_t *payload = (fault_entry_t *)msg.data;
  payload->severity = severity;
  payload->code = code;
  payload->axis = axis;
  payload->value = value;
  payload->timestamp = msg.timestamp;
  strncpy(payload->message, msg_buffer, 63);
  payload->message[63] = '\0';

  if (!taskSendMessage(taskGetFaultQueue(), &msg)) {
    Serial.println("[FAULT_RT] [WARN] Queue Full! Using ring buffer fallback.");
    faultAddToRingBuffer(payload);
  }

  if (severity == FAULT_CRITICAL) {
    emergencyStopSetActive(true);
  }
}

// NVS Write Cooldown
#define NVS_WRITE_COOLDOWN_MS 1000
static uint32_t last_nvs_write_time[FAULT_CODE_MAX] = {0};

void faultLogToNVS(const fault_entry_t *entry) {
  if (!entry)
    return;

  uint32_t now = millis();
  if (entry->code < FAULT_CODE_MAX) {
    uint32_t time_since_last_write = now - last_nvs_write_time[entry->code];
    if (time_since_last_write < NVS_WRITE_COOLDOWN_MS)
      return;
    last_nvs_write_time[entry->code] = now;
  }

  uint8_t write_idx =
      (log_header.head + log_header.count) % MAX_FAULT_ENTRIES_NVS;

  if (log_header.count >= MAX_FAULT_ENTRIES_NVS) {
    // Buffer full, overwrite head (oldest), advance head
    write_idx = log_header.head;
    log_header.head = (log_header.head + 1) % MAX_FAULT_ENTRIES_NVS;
  } else {
    log_header.count++;
  }

  log_header.total_lifetime_faults++;

  // Write Entry Blob
  char key[32];
  snprintf(key, sizeof(key), "%s%d", KEY_FAULT_PREFIX, write_idx);

  // CRITICAL: Handle Full Storage
  if (fault_prefs.putBytes(key, entry, sizeof(fault_entry_t)) == 0) {
    logError("[FAULT] NVS Full/Error writing fault %d. Formatting...",
             write_idx);
    fault_prefs.clear();
    // Reset header
    memset(&log_header, 0, sizeof(log_header));
    log_header.version = FAULT_LOG_VERSION;
    fault_prefs.putBytes(KEY_HEADER, &log_header, sizeof(log_header));
    // Retry once
    fault_prefs.putBytes(key, entry, sizeof(fault_entry_t));
  } else {
    // Update Header
    fault_prefs.putBytes(KEY_HEADER, &log_header, sizeof(log_header));
  }
}

void faultLogWarning(fault_code_t code, const char *message) {
  faultLogEntry(FAULT_WARNING, code, -1, 0, "%s", message);
}

void faultLogError(fault_code_t code, const char *message) {
  faultLogEntry(FAULT_ERROR, code, -1, 0, "%s", message);
}

void faultLogCritical(fault_code_t code, const char *message) {
  faultLogEntry(FAULT_CRITICAL, code, -1, 0, "%s", message);
}

fault_stats_t faultGetStats() {
  fault_stats_t stats = {0, 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFF, 0};

  // Scan all valid entries
  for (uint8_t i = 0; i < log_header.count; i++) {
    uint8_t slot = (log_header.head + i) % MAX_FAULT_ENTRIES_NVS;
    char key[32];
    snprintf(key, sizeof(key), "%s%d", KEY_FAULT_PREFIX, slot);

    fault_entry_t entry;
    if (fault_prefs.getBytes(key, &entry, sizeof(entry)) == sizeof(entry)) {
      stats.total_faults++;
      if (entry.timestamp > stats.last_fault_time_ms)
        stats.last_fault_time_ms = entry.timestamp;
      if (entry.timestamp < stats.first_fault_time_ms)
        stats.first_fault_time_ms = entry.timestamp;

      // Stats categories
      switch (entry.code) {
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
      default:
        stats.system_faults++;
        break;
      }
    }
  }

  if (stats.first_fault_time_ms == 0xFFFFFFFF)
    stats.first_fault_time_ms = 0;

  return stats;
}

void faultShowHistory() {
  Serial.println("[FAULT] Full history dump via CLI only.");
}

void faultClearHistory() {
  Serial.println("[FAULT] Clearing history...");
  // Clear faults
  for (int i = 0; i < MAX_FAULT_ENTRIES_NVS; i++) {
    char key[32];
    snprintf(key, sizeof(key), "%s%d", KEY_FAULT_PREFIX, i);
    if (fault_prefs.isKey(key))
      fault_prefs.remove(key);
  }
  // Reset Header
  log_header.count = 0;
  log_header.head = 0;
  log_header.total_lifetime_faults = 0;
  fault_prefs.putBytes(KEY_HEADER, &log_header, sizeof(log_header));

  Serial.println("[FAULT] [OK] Cleared");
}

void emergencyStopSetActive(bool active) {
  if (active && !estop_active) {
    estop_active = true;
    motionEmergencyStop();
    safetySetState(FSM_EMERGENCY);
    logError("[E-STOP] ACTIVATED via Fault");

    // PHASE 5.10: Signal event group for emergency stop
    systemEventsSafetySet(EVENT_SAFETY_ESTOP_PRESSED);
    systemEventsSafetySet(EVENT_SAFETY_ALARM_RAISED);
  } else if (!active && estop_active) {
    estop_active = false;
    safetySetState(FSM_OK);
    logInfo("[E-STOP] CLEARED");

    // PHASE 5.10: Signal event group for E-STOP release
    systemEventsSafetySet(EVENT_SAFETY_ESTOP_RELEASED);
    systemEventsSafetySet(EVENT_SAFETY_ALARM_CLEARED);
    systemEventsSafetyClear(EVENT_SAFETY_ESTOP_PRESSED);
    systemEventsSafetyClear(EVENT_SAFETY_ALARM_RAISED);
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

void emergencyStopClearRecovery() { estop_recovery_requested = false; }

// PHASE 5.1: Ring buffer diagnostics functions
uint32_t faultGetRingBufferDropCount() {
  return fault_ring_buffer.total_dropped;
}

uint8_t faultGetRingBufferEntryCount() { return fault_ring_buffer.count; }

const fault_entry_t *faultGetRingBufferEntry(uint8_t index) {
  if (index >= fault_ring_buffer.count) {
    return NULL;
  }
  uint8_t actual_idx =
      (fault_ring_buffer.head + index) % FAULT_RING_BUFFER_SIZE;
  return &fault_ring_buffer.entries[actual_idx];
}

#pragma GCC diagnostic pop
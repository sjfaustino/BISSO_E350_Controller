/**
 * @file safety.cpp
 * @brief Safety Monitor
 * @project PosiPro
 */

#include "safety.h"
#include "altivar31_modbus.h" // PHASE 5.5: VFD current-based stall detection
#include "axis_synchronization.h" // PHASE 5.6: Per-axis motion validation
#include "config_keys.h"
#include "config_unified.h"
#include "encoder_motion_integration.h"
#include "encoder_wj66.h" // For wj66GetStatus() and wj66IsStale()
#include "fault_logging.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides motionIsMoving, motionGetState
#include "safety_state_machine.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include "vfd_current_calibration.h" // PHASE 5.5: VFD current calibration
#include "system_tuning.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h> // For isnan() sensor validation
#include <string.h>

// PHASE 5.7: Cursor AI Fix - Thread-safe safety state with mutex protection
static safety_system_data_t safety_state;
static bool alarm_active = false;
static uint32_t alarm_trigger_time = 0;
static uint32_t last_stall_check = 0;

// SAFETY THRESHOLD CONSTANTS (replacing magic numbers)
// These multipliers are applied to user-configurable temperature thresholds
// to provide early warning (130%) and critical fault (140%) detection
#define SAFETY_THERMAL_WARNING_MULTIPLIER                                      \
  1.3f // Warn at 130% of warning threshold
#define SAFETY_THERMAL_CRITICAL_MULTIPLIER                                     \
  1.4f                                   // Fault at 140% of critical threshold
#define SAFETY_AXIS_QUALITY_CHECK_MS 500 // Check motion quality every 500ms
#define SAFETY_THERMAL_WARN_INTERVAL_MS                                        \
  5000 // Log thermal warnings max every 5s
#define SAFETY_MIN_ALARM_DURATION_MS                                           \
  1000 // Minimum time before alarm can be reset

// PHASE 5.7: Cursor AI Fix - Mutex to protect safety state from race conditions
// Multiple tasks can trigger alarms (Safety task, Motion task, Encoder task)
// Without protection, alarm state can be corrupted
static SemaphoreHandle_t safety_state_mutex = NULL;

void safetyInit() {
  logPrintln("[SAFETY] Initializing...");

  // PHASE 5.7: Cursor AI Fix - Create mutex for thread-safe safety state
  safety_state_mutex = xSemaphoreCreateMutex();
  if (safety_state_mutex == NULL) {
    logError("[SAFETY] CRITICAL: Failed to create safety state mutex!");
  } else {
    logInfo("[SAFETY] [OK] Safety state mutex created");
  }

  pinMode(SAFETY_ALARM_PIN, OUTPUT);
  digitalWrite(SAFETY_ALARM_PIN, LOW);

  safety_state.current_fault = SAFETY_OK;
  safety_state.fault_timestamp = 0;
  safety_state.fault_duration_ms = 0;
  safety_state.fault_count = 0;
  safety_state.history_index = 0;
  memset(safety_state.fault_message, 0, sizeof(safety_state.fault_message));
  memset(safety_state.fault_history, 0, sizeof(safety_state.fault_history));

  logInfo("[SAFETY] [OK] Alarm Pin: GPIO %d", SAFETY_ALARM_PIN);
}

void safetyUpdate() {
  uint32_t now = millis();

  // PHASE 5.1: Wraparound-safe timeout comparison
  if ((uint32_t)(now - last_stall_check) > SAFETY_STALL_CHECK_INTERVAL_MS) {
    last_stall_check = now;

    uint32_t stall_limit_ms =
        (uint32_t)configGetInt(KEY_STALL_TIMEOUT, SAFETY_MAX_STALL_TIME_MS);

    if (motionIsMoving()) {
      for (uint8_t axis = 0; axis < MOTION_AXES; axis++) {
        if (motionGetState(axis) == MOTION_EXECUTING) {
          if (encoderMotionHasError(axis) &&
              encoderMotionGetErrorDuration(axis) > stall_limit_ms) {

            safetyReportStall(axis);

            logError(
                "[SAFETY] [FAIL] Stall Axis %d (Dur: %lu ms > Limit: %lu ms)",
                axis, (unsigned long)encoderMotionGetErrorDuration(axis),
                (unsigned long)stall_limit_ms);
          }
        }
      }

      // PHASE 5.5: Supplementary VFD current-based stall detection
      if (vfdCalibrationIsValid()) {
        float current_amps = altivar31GetCurrentAmps();
        float threshold_amps = vfdCalibrationGetThreshold();

        // CRITICAL: Validate sensor data before safety decisions
        // Data freshness already checked in altivar31DetectFrequencyLoss()
        const altivar31_state_t *vfd_state = altivar31GetState();
        uint32_t data_age_ms = now - vfd_state->last_read_time_ms;

        // Only use VFD current data if fresh (<1s) and valid (not NaN, in
        // range)
        bool data_valid = (data_age_ms < 1000) && !isnan(current_amps) &&
                          (current_amps >= 0.0f && current_amps <= 100.0f);

        // Detect stall when current exceeds threshold (indicates mechanical
        // resistance)
        if (data_valid && vfdCalibrationIsStall(current_amps)) {
          logWarning(
              "[SAFETY] [WARN] VFD Current High: %.2f A (threshold: %.2f A)",
              current_amps, threshold_amps);

          // If encoder also indicates stall (or we haven't detected it yet),
          // trigger alarm This provides supplementary detection if encoder is
          // degraded
          if (!alarm_active) {
            safetyReportStall(0); // Report as Z-axis stall (spindle)
            logError(
                "[SAFETY] [FAIL] Motor Stall (VFD Current: %.2f A > %.2f A)",
                current_amps, threshold_amps);
          }
        }
      }
    }

    // PHASE 5.5: VFD Frequency Control Validation
    // Detect frequency loss during active motion (potential VFD/motor fault)
    if (motionIsMoving()) {
      static float last_frequency_hz = 0.0f;
      float current_freq = altivar31GetFrequencyHz();

      // CRITICAL: Validate frequency sensor data before safety decisions
      const altivar31_state_t *vfd_state = altivar31GetState();
      uint32_t data_age_ms = now - vfd_state->last_read_time_ms;

      // Only use frequency data if fresh (<1s) and valid (not NaN, in range
      // 0-60Hz typical)
      bool freq_valid = (data_age_ms < 1000) && !isnan(current_freq) &&
                        (current_freq >= 0.0f && current_freq <= 100.0f);

      // Check for sudden frequency loss (>80% drop in one cycle)
      // altivar31DetectFrequencyLoss() already includes freshness check
      if (freq_valid && altivar31DetectFrequencyLoss(last_frequency_hz)) {
        logError("[SAFETY] [FAIL] VFD Frequency Loss: %.1f Hz -> %.1f Hz (>80% "
                 "drop)",
                 last_frequency_hz, current_freq);

        if (!alarm_active) {
          // PHASE 5.10: Removed unsafe current_fault assignment (now done in safetyTriggerAlarm)
          char msg[64];
          snprintf(msg, sizeof(msg), "VFD FREQ LOSS %.1f->%.1f Hz",
                   last_frequency_hz, current_freq);
          faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, 0, 0,
                        "VFD frequency loss detected");
          safetyTriggerAlarm(msg, SAFETY_STALLED);
        }
      }

      // Only update last frequency if current reading is valid
      if (freq_valid) {
        last_frequency_hz = current_freq;
      }
    }

    // PHASE 5.5: VFD Thermal Monitoring
    // Monitor motor/VFD heatsink temperature with warning and critical
    // thresholds
    static uint32_t last_thermal_check = 0;
    if ((uint32_t)(now - last_thermal_check) > 1000) { // Check every 1 second
      last_thermal_check = now;

      int16_t thermal_state = altivar31GetThermalState();

      // CRITICAL: Validate thermal sensor data before safety decisions
      const altivar31_state_t *vfd_state = altivar31GetState();
      uint32_t data_age_ms = now - vfd_state->last_read_time_ms;

      // Valid range: 0-200% (100% = nominal, >118% typically triggers VFD
      // fault)
      bool thermal_valid =
          (data_age_ms < 1000) && (thermal_state > 0 && thermal_state <= 200);

      if (thermal_valid) { // Valid reading (percentage, 100% = nominal)
        int32_t temp_warn = configGetInt(KEY_VFD_TEMP_WARN, 85);
        int32_t temp_crit = configGetInt(KEY_VFD_TEMP_CRIT, 90);

        if (thermal_state > (temp_crit * SAFETY_THERMAL_CRITICAL_MULTIPLIER)) {
          logError("[SAFETY] [FAIL] VFD Thermal Critical: %d%% (>%ld°C)",
                   thermal_state, (long)temp_crit);

          if (!alarm_active) {
            // PHASE 5.10: Removed unsafe current_fault assignment (now done in safetyTriggerAlarm)
            char msg[64];
            snprintf(msg, sizeof(msg), "VFD OVERHEAT %d%%", thermal_state);
            faultLogEntry(FAULT_ERROR, FAULT_TEMPERATURE_HIGH, 0, 0,
                          "VFD thermal critical");
            safetyTriggerAlarm(msg, SAFETY_THERMAL);
          }

        } else if (thermal_state >
                   (temp_warn * SAFETY_THERMAL_WARNING_MULTIPLIER)) {
          static uint32_t last_thermal_warn = 0;
          if ((uint32_t)(now - last_thermal_warn) >
              SAFETY_THERMAL_WARN_INTERVAL_MS) {
            last_thermal_warn = now;
            faultLogWarning(FAULT_TEMPERATURE_HIGH, "VFD Thermal Warning (Pre-alarm)");
            logWarning("[SAFETY] [WARN] VFD Thermal Warning: %d%% (>%ld°C)",
                       thermal_state, (long)temp_warn);
          }
        }
      }
    }

    // PHASE 5.6: Axis Motion Quality Validation
    // Monitor per-axis synchronization quality and trigger alarms on
    // degradation
    static uint32_t last_axis_quality_check = 0;
    if ((uint32_t)(now - last_axis_quality_check) >
        SAFETY_AXIS_QUALITY_CHECK_MS) {
      last_axis_quality_check = now;

      // Check each axis quality score (0-100)
      for (uint8_t axis = 0; axis < 3; axis++) {
        const axis_metrics_t *metrics = axisSynchronizationGetAxisMetrics(axis);
        if (metrics) {
          // Trigger alarm if quality drops critically low (below 25%)
          if (metrics->quality_score < 25 && metrics->is_moving &&
              !alarm_active) {
            char axis_char = 'X' + axis;
            char msg[64];
            snprintf(msg, sizeof(msg), "AXIS %c QUALITY CRITICAL: %lu%%",
                     axis_char, (unsigned long)metrics->quality_score);
            logError("[SAFETY] [FAIL] %s", msg);
            faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, axis, 0,
                          "Axis motion quality critical");
            safetyTriggerAlarm(msg, SAFETY_STALLED);
            break; // Only trigger one alarm per check cycle
          }

          // Detect axis stall from quality metrics
          if (metrics->stalled && metrics->is_moving && !alarm_active) {
            char axis_char = 'X' + axis;
            char msg[64];
            snprintf(msg, sizeof(msg), "AXIS %c STALL (Quality: %lu%%)",
                     axis_char, (unsigned long)metrics->quality_score);
            logError("[SAFETY] [FAIL] %s", msg);
            faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, axis, 0,
                          "Axis motion stall detected via quality metrics");
            safetyTriggerAlarm(msg, SAFETY_STALLED);
            break;
          }

          // Log warning if quality is degraded (below 50%)
          if (metrics->quality_score < 50 && metrics->is_moving) {
            static uint32_t last_quality_warn[3] = {0, 0, 0};
            if ((uint32_t)(now - last_quality_warn[axis]) >
                3000) { // Warn every 3s max
              last_quality_warn[axis] = now;
              char axis_char = 'X' + axis;
              logWarning(
                  "[SAFETY] [WARN] AXIS %c motion quality degraded: %lu%%",
                  axis_char, (unsigned long)metrics->quality_score);
            }
          }
        }
      }
    }
  }

  if (alarm_active) {
    // PHASE 5.1: Wraparound-safe duration calculation
    safety_state.fault_duration_ms = (uint32_t)(millis() - alarm_trigger_time);
  }
}

bool safetyCheckMotionAllowed(uint8_t axis) {
  if (axis >= MOTION_AXES)
    return false;
  return !alarm_active && safety_state.current_fault == SAFETY_OK;
}

// PHASE 5.7 + 5.10: Thread-safe alarm trigger with proper mutex error handling
// PHASE 5.10: Added fault_type parameter to ensure thread-safe fault assignment
void safetyTriggerAlarm(const char *reason, safety_fault_t fault_type) {
  // PHASE 5.10: CRITICAL FIX - Mutex must succeed for safety operations
  // If mutex is NULL or acquisition fails, force hardware E-stop immediately
  if (safety_state_mutex == NULL) {
    logError("[SAFETY] [CRITICAL] Mutex not initialized - FORCING HARDWARE ESTOP");
    digitalWrite(SAFETY_ALARM_PIN, HIGH);
    motionEmergencyStop();
    return;
  }

  BaseType_t got_mutex = xSemaphoreTake(safety_state_mutex, pdMS_TO_TICKS(100));
  if (got_mutex != pdTRUE) {
    logError("[SAFETY] [CRITICAL] Mutex timeout - FORCING HARDWARE ESTOP");
    digitalWrite(SAFETY_ALARM_PIN, HIGH);
    motionEmergencyStop();
    return;
  }

  // NOW SAFE: Mutex is confirmed acquired
  if (alarm_active) {
    xSemaphoreGive(safety_state_mutex);
    return;
  }

  // PHASE 5.10: Set current_fault under mutex protection (thread-safe)
  safety_state.current_fault = fault_type;

  alarm_active = true;
  alarm_trigger_time = millis();
  safety_state.fault_timestamp = alarm_trigger_time;
  safety_state.fault_count++;

  safety_state.fault_history[safety_state.history_index] = fault_type;
  safety_state.history_index =
      (safety_state.history_index + 1) % SAFETY_FAULT_HISTORY_SIZE;

  snprintf(safety_state.fault_message, sizeof(safety_state.fault_message), "%s",
           reason);

  xSemaphoreGive(safety_state_mutex);

  // Hardware operations outside mutex (no shared state)
  digitalWrite(SAFETY_ALARM_PIN, HIGH);

  logError("[SAFETY] [ALARM] Triggered: %s", reason);
  logPrintf("[SAFETY] Fault Count: %lu\n",
                (unsigned long)safety_state.fault_count);

  // ITEM 4: E-Stop Recovery Guidance - Help operator understand what to check
  logPrintln("\n[SAFETY] ======== RECOVERY CHECKLIST ========");
  logPrintln("[SAFETY] Before resetting alarm, CHECK:");
  logPrintln("  1. Emergency stop button released");
  logPrintln("  2. Safety guards in place");
  logPrintln("  3. Material clamps secure");
  logPrintln("  4. Blade clear of obstruction");
  logPrintln("  5. Coolant level adequate");
  logPrintln("[SAFETY] Use 'safety reset' when ready");
  logPrintln("[SAFETY] ====================================\n");

  // CRITICAL: Deadlock-Safe Emergency Stop (Code Audit)
  // motionEmergencyStop() uses 10ms timeout to prevent deadlock
  // If Motion task holds mutex while blocked on I2C, E-stop still succeeds
  // via hardware PLC I/O layer (independent of motion_mutex)
  // See: docs/PosiPro_FINAL_AUDIT.md for complete safety analysis
  motionEmergencyStop();
}

// PHASE 5.7 + 5.10: Safety Alarm Reset with proper mutex error handling
void safetyResetAlarm() {
  // PHASE 5.10: CRITICAL FIX - Must acquire mutex for safe state access
  if (safety_state_mutex == NULL) {
    logError("[SAFETY] [CRITICAL] Mutex not initialized - cannot reset alarm safely");
    return;
  }

  BaseType_t got_mutex = xSemaphoreTake(safety_state_mutex, pdMS_TO_TICKS(100));
  if (got_mutex != pdTRUE) {
    logError("[SAFETY] [CRITICAL] Mutex timeout in resetAlarm - cannot proceed");
    return;
  }

  // NOW SAFE: Mutex is confirmed acquired
  if (!alarm_active) {
    xSemaphoreGive(safety_state_mutex);
    logWarning("[SAFETY] Alarm reset requested but no alarm is active");
    return;
  }

  // VALIDATION 1: Verify all axes have stopped moving
  bool any_axis_moving = false;
  for (uint8_t axis = 0; axis < MOTION_AXES; axis++) {
    motion_state_t state = motionGetState(axis);
    if (state == MOTION_EXECUTING || state == MOTION_WAIT_CONSENSO ||
        state == MOTION_HOMING_APPROACH_FAST ||
        state == MOTION_HOMING_BACKOFF ||
        state == MOTION_HOMING_APPROACH_FINE) {
      any_axis_moving = true;
      logWarning("[SAFETY] [BLOCKED] Cannot reset alarm - Axis %d still moving",
                 axis);
      break;
    }
  }
  if (any_axis_moving) {
    xSemaphoreGive(safety_state_mutex);  // Mutex confirmed acquired
    logError("[SAFETY] [BLOCKED] Alarm reset denied - motion must stop first");
    return;
  }

  // VALIDATION 2: Check encoder communication status
  // Note: This is a basic check - full encoder validation happens in encoder
  // task
  bool encoder_ok = true;
  encoder_status_t global_status = wj66GetStatus();
  if (global_status == ENCODER_ERROR || global_status == ENCODER_TIMEOUT) {
    encoder_ok = false;
    logWarning(
        "[SAFETY] [WARNING] Encoder communication error (global status: %d)",
        global_status);
  }
  // Also check per-axis stale status
  for (uint8_t axis = 0; axis < MOTION_AXES; axis++) {
    if (wj66IsStale(axis)) {
      encoder_ok = false;
      logWarning("[SAFETY] [WARNING] Axis %d encoder not responding (stale)",
                 axis);
    }
  }
  if (!encoder_ok && safety_state.current_fault == SAFETY_ENCODER_ERROR) {
    xSemaphoreGive(safety_state_mutex);  // Mutex confirmed acquired
    logError(
        "[SAFETY] [BLOCKED] Alarm reset denied - encoder fault not cleared");
    return;
  }

// VALIDATION 3: Wait minimum time after alarm trigger (prevent rapid reset)
#define SAFETY_MIN_ALARM_DURATION_MS                                           \
  1000 // Minimum 1 second before reset allowed
  uint32_t alarm_duration = (uint32_t)(millis() - alarm_trigger_time);
  if (alarm_duration < SAFETY_MIN_ALARM_DURATION_MS) {
    xSemaphoreGive(safety_state_mutex);  // Mutex confirmed acquired
    logWarning(
        "[SAFETY] [BLOCKED] Alarm reset too soon (%lu ms < %d ms minimum)",
        (unsigned long)alarm_duration, SAFETY_MIN_ALARM_DURATION_MS);
    return;
  }

  // All validations passed - safe to reset alarm
  alarm_active = false;
  safety_state.current_fault = SAFETY_OK;
  safety_state.fault_duration_ms = alarm_duration;

  if (safety_state_mutex)
    xSemaphoreGive(safety_state_mutex);

  // Hardware operations outside mutex (no shared state)
  digitalWrite(SAFETY_ALARM_PIN, LOW);

  logInfo("[SAFETY] [OK] Alarm reset (Duration: %lu ms, validations passed)",
      (unsigned long)safety_state.fault_duration_ms);
}

void safetyReportStall(uint8_t axis) {
  if (axis < MOTION_AXES) {
    // PHASE 5.10: Removed unsafe current_fault assignment (now done in safetyTriggerAlarm)
    char msg[64];
    snprintf(msg, sizeof(msg), "STALL Axis %d", axis);
    faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, axis, 0,
                  "Motion stall detected");
    safetyTriggerAlarm(msg, SAFETY_STALLED);
  }
}

void safetyReportSoftLimit(uint8_t axis) {
  if (axis < MOTION_AXES) {
    // PHASE 5.10: Removed unsafe current_fault assignment (now done in safetyTriggerAlarm)
    char msg[64];
    snprintf(msg, sizeof(msg), "LIMIT Axis %d", axis);
    faultLogEntry(FAULT_ERROR, FAULT_SOFT_LIMIT_EXCEEDED, axis, 0,
                  "Soft limit reached");

    // PHASE 5.10: Signal soft limit violation event
    systemEventsSafetySet(EVENT_SAFETY_SOFT_LIMIT_HIT);

    safetyTriggerAlarm(msg, SAFETY_SOFT_LIMIT);
  }
}

void safetyReportEncoderError(uint8_t axis) {
  if (axis < MOTION_AXES) {
    // PHASE 5.10: Removed unsafe current_fault assignment (now done in safetyTriggerAlarm)
    char msg[64];
    snprintf(msg, sizeof(msg), "ENC_ERR Axis %d", axis);
    faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, axis, 0,
                  "Encoder comm failure");
    safetyTriggerAlarm(msg, SAFETY_ENCODER_ERROR);
  }
}

void safetyReportPLCFault() {
  // PHASE 5.10: Removed unsafe current_fault assignment (now done in safetyTriggerAlarm)
  safetyTriggerAlarm("PLC_FAULT", SAFETY_PLC_FAULT);
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, 0,
                "PLC Consensus Failure");
}

safety_fault_t safetyGetCurrentFault() { return safety_state.current_fault; }
bool safetyIsAlarmed() { return alarm_active; }
uint32_t safetyGetAlarmDuration() {
  return alarm_active ? (millis() - alarm_trigger_time)
                      : safety_state.fault_duration_ms;
}

void safetyDiagnostics() {
  logPrintln("\n[SAFETY] === Diagnostics ===");
  logPrintf("Status: %s\n", alarm_active ? "[ALARM]" : "[OK]");

  const char *faultStr = "UNKNOWN";
  switch (safety_state.current_fault) {
  case SAFETY_OK:
    faultStr = "NONE";
    break;
  case SAFETY_STALLED:
    faultStr = "STALLED";
    break;
  case SAFETY_SOFT_LIMIT:
    faultStr = "SOFT_LIMIT";
    break;
  case SAFETY_PLC_FAULT:
    faultStr = "PLC_FAULT";
    break;
  case SAFETY_THERMAL:
    faultStr = "THERMAL";
    break;
  case SAFETY_ALARM:
    faultStr = "ALARM";
    break;
  case SAFETY_ENCODER_ERROR:
    faultStr = "ENCODER_ERROR";
    break;
  }
  logPrintf("Current Fault: %s\n", faultStr);
  logPrintf("GPIO State: %s\n",
                digitalRead(SAFETY_ALARM_PIN) ? "HIGH" : "LOW");
  logPrintf("Count: %lu\n", (unsigned long)safety_state.fault_count);
  logPrintf("Last Msg: %s\n", safety_state.fault_message);

  if (alarm_active) {
    logPrintf("Duration: %lu ms\n",
                  (unsigned long)safetyGetAlarmDuration());
  }

  logPrintln("\nHistory (Last 16):");
  for (int i = 0; i < SAFETY_FAULT_HISTORY_SIZE; i++) {
    int idx = (safety_state.history_index + i) % SAFETY_FAULT_HISTORY_SIZE;
    if (safety_state.fault_history[idx] != SAFETY_OK) {
      logPrintf(
          "  [%d] %s\n", i,
          faultCodeToString((fault_code_t)safety_state.fault_history[idx]));
    }
  }
}

/**
 * @file safety.cpp
 * @brief Safety Monitor
 * @project Gemini v3.5.0
 */

#include "safety.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides motionIsMoving, motionGetState
#include "fault_logging.h"
#include "encoder_motion_integration.h"
#include "system_constants.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include <Arduino.h>
#include "safety_state_machine.h"
#include "altivar31_modbus.h"  // PHASE 5.5: VFD current-based stall detection
#include "vfd_current_calibration.h"  // PHASE 5.5: VFD current calibration
#include <string.h>

static safety_system_data_t safety_state; 
static bool alarm_active = false;
static uint32_t alarm_trigger_time = 0;
static uint32_t last_stall_check = 0;

void safetyInit() {
  Serial.println("[SAFETY] Initializing...");
  pinMode(SAFETY_ALARM_PIN, OUTPUT);
  digitalWrite(SAFETY_ALARM_PIN, LOW);
  
  safety_state.current_fault = SAFETY_OK;
  safety_state.fault_timestamp = 0;
  safety_state.fault_duration_ms = 0;
  safety_state.fault_count = 0;
  safety_state.history_index = 0;
  memset(safety_state.fault_message, 0, sizeof(safety_state.fault_message));
  memset(safety_state.fault_history, 0, sizeof(safety_state.fault_history));
  
  Serial.printf("[SAFETY] [OK] Alarm Pin: GPIO %d\n", SAFETY_ALARM_PIN);
}

void safetyUpdate() {
  uint32_t now = millis();

  // PHASE 5.1: Wraparound-safe timeout comparison
  if ((uint32_t)(now - last_stall_check) > SAFETY_STALL_CHECK_INTERVAL_MS) {
    last_stall_check = now;
    
    uint32_t stall_limit_ms = (uint32_t)configGetInt(KEY_STALL_TIMEOUT, SAFETY_MAX_STALL_TIME_MS);

    if (motionIsMoving()) {
        for (uint8_t axis = 0; axis < MOTION_AXES; axis++) {
            if (motionGetState(axis) == MOTION_EXECUTING) {
                if (encoderMotionHasError(axis) &&
                    encoderMotionGetErrorDuration(axis) > stall_limit_ms) {

                    safetyReportStall(axis);

                    logError("[SAFETY] [FAIL] Stall Axis %d (Dur: %lu ms > Limit: %lu ms)",
                             axis, (unsigned long)encoderMotionGetErrorDuration(axis), (unsigned long)stall_limit_ms);
                }
            }
        }

        // PHASE 5.5: Supplementary VFD current-based stall detection
        if (vfdCalibrationIsValid()) {
            float current_amps = altivar31GetCurrentAmps();
            float threshold_amps = vfdCalibrationGetThreshold();

            // Detect stall when current exceeds threshold (indicates mechanical resistance)
            if (vfdCalibrationIsStall(current_amps)) {
                logWarn("[SAFETY] [WARN] VFD Current High: %.2f A (threshold: %.2f A)",
                        current_amps, threshold_amps);

                // If encoder also indicates stall (or we haven't detected it yet), trigger alarm
                // This provides supplementary detection if encoder is degraded
                if (!alarm_active) {
                    safetyReportStall(0);  // Report as Z-axis stall (spindle)
                    logError("[SAFETY] [FAIL] Motor Stall (VFD Current: %.2f A > %.2f A)",
                             current_amps, threshold_amps);
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
  if (axis >= MOTION_AXES) return false;
  return !alarm_active && safety_state.current_fault == SAFETY_OK;
}

void safetyTriggerAlarm(const char* reason) {
  if (alarm_active) return;
  
  alarm_active = true;
  alarm_trigger_time = millis();
  safety_state.fault_timestamp = alarm_trigger_time;
  safety_state.fault_count++;
  
  safety_state.fault_history[safety_state.history_index] = safety_state.current_fault;
  safety_state.history_index = (safety_state.history_index + 1) % SAFETY_FAULT_HISTORY_SIZE;
  
  snprintf(safety_state.fault_message, sizeof(safety_state.fault_message), "%s", reason);
  
  digitalWrite(SAFETY_ALARM_PIN, HIGH);
  
  Serial.printf("[SAFETY] [ALARM] Triggered: %s\n", reason);
  Serial.printf("[SAFETY] Fault Count: %lu\n", (unsigned long)safety_state.fault_count);
  
  motionEmergencyStop();
}

void safetyResetAlarm() {
  if (!alarm_active) return;

  alarm_active = false;
  digitalWrite(SAFETY_ALARM_PIN, LOW);
  safety_state.current_fault = SAFETY_OK;
  // PHASE 5.1: Wraparound-safe duration calculation
  safety_state.fault_duration_ms = (uint32_t)(millis() - alarm_trigger_time);

  Serial.printf("[SAFETY] [OK] Alarm reset (Duration: %lu ms)\n", (unsigned long)safety_state.fault_duration_ms);
}

void safetyReportStall(uint8_t axis) {
  if (axis < MOTION_AXES) {
    safety_state.current_fault = SAFETY_STALLED;
    char msg[64];
    snprintf(msg, sizeof(msg), "STALL Axis %d", axis);
    faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, axis, 0, "Motion stall detected");
    safetyTriggerAlarm(msg);
  }
}

void safetyReportSoftLimit(uint8_t axis) {
  if (axis < MOTION_AXES) {
    safety_state.current_fault = SAFETY_SOFT_LIMIT;
    char msg[64];
    snprintf(msg, sizeof(msg), "LIMIT Axis %d", axis);
    faultLogEntry(FAULT_ERROR, FAULT_SOFT_LIMIT_EXCEEDED, axis, 0, "Soft limit reached");
    safetyTriggerAlarm(msg);
  }
}

void safetyReportEncoderError(uint8_t axis) {
  if (axis < MOTION_AXES) {
    safety_state.current_fault = SAFETY_ENCODER_ERROR;
    char msg[64];
    snprintf(msg, sizeof(msg), "ENC_ERR Axis %d", axis);
    faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, axis, 0, "Encoder comm failure");
    safetyTriggerAlarm(msg);
  }
}

void safetyReportPLCFault() {
  safety_state.current_fault = SAFETY_PLC_FAULT;
  safetyTriggerAlarm("PLC_FAULT");
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, 0, "PLC Consensus Failure");
}

safety_fault_t safetyGetCurrentFault() { return safety_state.current_fault; }
bool safetyIsAlarmed() { return alarm_active; }
uint32_t safetyGetAlarmDuration() {
  return alarm_active ? (millis() - alarm_trigger_time) : safety_state.fault_duration_ms;
}

void safetyDiagnostics() {
  Serial.println("\n[SAFETY] === Diagnostics ===");
  Serial.printf("Status: %s\n", alarm_active ? "[ALARM]" : "[OK]");
  
  const char* faultStr = "UNKNOWN";
  switch(safety_state.current_fault) {
    case SAFETY_OK: faultStr = "NONE"; break;
    case SAFETY_STALLED: faultStr = "STALLED"; break;
    case SAFETY_SOFT_LIMIT: faultStr = "SOFT_LIMIT"; break;
    case SAFETY_PLC_FAULT: faultStr = "PLC_FAULT"; break;
    case SAFETY_THERMAL: faultStr = "THERMAL"; break;
    case SAFETY_ALARM: faultStr = "ALARM"; break;
    case SAFETY_ENCODER_ERROR: faultStr = "ENCODER_ERROR"; break;
  }
  Serial.printf("Current Fault: %s\n", faultStr);
  Serial.printf("GPIO State: %s\n", digitalRead(SAFETY_ALARM_PIN) ? "HIGH" : "LOW");
  Serial.printf("Count: %lu\n", (unsigned long)safety_state.fault_count);
  Serial.printf("Last Msg: %s\n", safety_state.fault_message);
  
  if (alarm_active) {
    Serial.printf("Duration: %lu ms\n", (unsigned long)safetyGetAlarmDuration());
  }
  
  Serial.println("\nHistory (Last 16):");
  for (int i = 0; i < SAFETY_FAULT_HISTORY_SIZE; i++) {
    int idx = (safety_state.history_index + i) % SAFETY_FAULT_HISTORY_SIZE;
    if (safety_state.fault_history[idx] != SAFETY_OK) {
      Serial.printf("  [%d] %s\n", i, faultCodeToString((fault_code_t)safety_state.fault_history[idx])); 
    }
  }
}
#include "safety.h"
#include "motion.h"
#include "fault_logging.h"
#include "encoder_motion_integration.h"
#include "system_constants.h"
#include "serial_logger.h"
#include <Arduino.h>
#include "safety_state_machine.h"
#include <string.h>

static safety_system_data_t safety_state; 
static bool alarm_active = false;
static uint32_t alarm_trigger_time = 0;
static uint32_t last_stall_check = 0;

void safetyInit() {
  Serial.println("[SAFETY] Safety system initializing...");
  pinMode(SAFETY_ALARM_PIN, OUTPUT);
  digitalWrite(SAFETY_ALARM_PIN, LOW);
  
  safety_state.current_fault = SAFETY_OK;
  safety_state.fault_timestamp = 0;
  safety_state.fault_duration_ms = 0;
  safety_state.fault_count = 0;
  safety_state.history_index = 0;
  memset(safety_state.fault_message, 0, sizeof(safety_state.fault_message));
  memset(safety_state.fault_history, 0, sizeof(safety_state.fault_history));
  
  Serial.print("[SAFETY] Alarm pin set to GPIO ");
  Serial.println(SAFETY_ALARM_PIN);
  Serial.println("[SAFETY] Safety system ready");
}

void safetyUpdate() {
  uint32_t now = millis();
  
  if (now - last_stall_check > SAFETY_STALL_CHECK_INTERVAL_MS) {
    last_stall_check = now;
    
    if (motionIsMoving()) {
        
        for (uint8_t axis = 0; axis < MOTION_AXES; axis++) {
            if (motionGetState(axis) == MOTION_EXECUTING) {
                
                if (encoderMotionHasError(axis) && 
                    encoderMotionGetErrorDuration(axis) > SAFETY_MAX_STALL_TIME_MS) {
                    
                    safetyReportStall(axis); 
                    
                    logError("Stall detected on axis %d (Error duration: %lu ms)", 
                             axis, SAFETY_MAX_STALL_TIME_MS);
                }
            }
        }
    }
  }
  
  if (alarm_active) {
    safety_state.fault_duration_ms = millis() - alarm_trigger_time;
  }
}

bool safetyCheckMotionAllowed(uint8_t axis) {
  if (axis >= MOTION_AXES) return false;
  return !alarm_active && safety_state.current_fault == SAFETY_OK;
}

void safetyTriggerAlarm(const char* reason) {
  if (alarm_active) {
    return;
  }
  
  alarm_active = true;
  alarm_trigger_time = millis();
  safety_state.fault_timestamp = alarm_trigger_time;
  safety_state.fault_count++;
  
  safety_state.fault_history[safety_state.history_index] = safety_state.current_fault;
  safety_state.history_index = (safety_state.history_index + 1) % SAFETY_FAULT_HISTORY_SIZE;
  
  snprintf(safety_state.fault_message, sizeof(safety_state.fault_message), "%s", reason);
  
  digitalWrite(SAFETY_ALARM_PIN, HIGH);
  
  Serial.print("[SAFETY] *** ALARM TRIGGERED: ");
  Serial.println(reason);
  Serial.print("[SAFETY] Fault count: ");
  Serial.println(safety_state.fault_count);
  
  motionEmergencyStop();
}

void safetyResetAlarm() {
  if (!alarm_active) {
    return;
  }
  
  alarm_active = false;
  digitalWrite(SAFETY_ALARM_PIN, LOW);
  safety_state.current_fault = SAFETY_OK;
  safety_state.fault_duration_ms = millis() - alarm_trigger_time;
  
  Serial.print("[SAFETY] Alarm reset (duration: ");
  Serial.print(safety_state.fault_duration_ms);
  Serial.println(" ms)");
}

void safetyReportStall(uint8_t axis) {
  if (axis < MOTION_AXES) {
    safety_state.current_fault = SAFETY_STALLED;
    char msg[64];
    snprintf(msg, sizeof(msg), "STALL on axis %d", axis);
    // --- NEW FAULT LOGGING ---
    faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, axis, 0, "Motion stall detected on Axis %d", axis);
    // -------------------------
    safetyTriggerAlarm(msg);
  }
}

void safetyReportSoftLimit(uint8_t axis) {
  if (axis < MOTION_AXES) {
    safety_state.current_fault = SAFETY_SOFT_LIMIT;
    char msg[64];
    snprintf(msg, sizeof(msg), "SOFT_LIMIT on axis %d", axis);
    // --- NEW FAULT LOGGING ---
    faultLogEntry(FAULT_ERROR, FAULT_SOFT_LIMIT_EXCEEDED, axis, 0, "Soft limit reached on Axis %d", axis);
    // -------------------------
    safetyTriggerAlarm(msg);
  }
}

void safetyReportEncoderError(uint8_t axis) {
  if (axis < MOTION_AXES) {
    safety_state.current_fault = SAFETY_ENCODER_ERROR;
    char msg[64];
    snprintf(msg, sizeof(msg), "ENCODER_ERROR on axis %d", axis);
    // --- NEW FAULT LOGGING ---
    faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, axis, 0, "Encoder communication failure on Axis %d", axis);
    // -------------------------
    safetyTriggerAlarm(msg);
  }
}

void safetyReportPLCFault() {
  safety_state.current_fault = SAFETY_PLC_FAULT;
  safetyTriggerAlarm("PLC_FAULT");
  // --- NEW FAULT LOGGING ---
  faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, -1, 0, "PLC Consensus/Comm failure detected");
  // -------------------------
}

safety_fault_t safetyGetCurrentFault() {
  return safety_state.current_fault;
}

bool safetyIsAlarmed() {
  return alarm_active;
}

uint32_t safetyGetAlarmDuration() {
  if (alarm_active) {
    return millis() - alarm_trigger_time;
  }
  return safety_state.fault_duration_ms;
}

void safetyDiagnostics() {
  Serial.println("\n[SAFETY] === Safety System Diagnostics ===");
  Serial.print("Alarm Status: ");
  Serial.println(alarm_active ? "ACTIVE" : "INACTIVE");
  
  Serial.print("Current Fault: ");
  switch(safety_state.current_fault) {
    case SAFETY_OK: Serial.println("NONE"); break;
    case SAFETY_STALLED: Serial.println("STALLED"); break;
    case SAFETY_SOFT_LIMIT: Serial.println("SOFT_LIMIT"); break;
    case SAFETY_PLC_FAULT: Serial.println("PLC_FAULT"); break;
    case SAFETY_THERMAL: Serial.println("THERMAL"); break;
    case SAFETY_ALARM: Serial.println("ALARM"); break;
    case SAFETY_ENCODER_ERROR: Serial.println("ENCODER_ERROR"); break;
    default: Serial.println("UNKNOWN");
  }
  
  Serial.print("Alarm Pin (GPIO ");
  Serial.print(SAFETY_ALARM_PIN);
  Serial.print("): ");
  Serial.println(digitalRead(SAFETY_ALARM_PIN) ? "HIGH" : "LOW");
  
  Serial.print("Total Faults: ");
  Serial.println(safety_state.fault_count);
  
  Serial.print("Last Fault Message: ");
  Serial.println(safety_state.fault_message);
  
  if (alarm_active) {
    Serial.print("Alarm Duration: ");
    Serial.print(safetyGetAlarmDuration());
    Serial.println(" ms");
  }
  
  Serial.println("\nRecent Fault History (Last 16 Events):");
  for (int i = 0; i < SAFETY_FAULT_HISTORY_SIZE; i++) {
    int idx = (safety_state.history_index + i) % SAFETY_FAULT_HISTORY_SIZE;
    if (safety_state.fault_history[idx] != SAFETY_OK) {
      Serial.print("  [");
      Serial.print(i);
      Serial.print("] Fault: ");
      // FIX: Use faultCodeToString for human-readable output
      Serial.println(faultCodeToString((fault_code_t)safety_state.fault_history[idx])); 
    }
  }
}
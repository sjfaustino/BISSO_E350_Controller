#include "safety.h"

static safety_state_t safety_state;
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
  // Check for stalled axes (would integrate with motion system)
  uint32_t now = millis();
  
  if (now - last_stall_check > 100) {
    last_stall_check = now;
    
    // Stall detection logic would go here
    // Check if any axis position hasn't changed for SAFETY_MAX_STALL_TIME_MS
  }
  
  // Update fault duration
  if (alarm_active) {
    safety_state.fault_duration_ms = millis() - alarm_trigger_time;
  }
}

bool safetyCheckMotionAllowed(uint8_t axis) {
  if (axis >= 4) return false;
  return !alarm_active && safety_state.current_fault == SAFETY_OK;
}

void safetyTriggerAlarm(const char* reason) {
  if (alarm_active) {
    return; // Already alarmed
  }
  
  alarm_active = true;
  alarm_trigger_time = millis();
  safety_state.fault_timestamp = alarm_trigger_time;
  safety_state.fault_count++;
  
  // Add to history
  safety_state.fault_history[safety_state.history_index] = safety_state.current_fault;
  safety_state.history_index = (safety_state.history_index + 1) % SAFETY_FAULT_HISTORY_SIZE;
  
  // Set message
  snprintf(safety_state.fault_message, sizeof(safety_state.fault_message), "%s", reason);
  
  // Activate alarm pin
  digitalWrite(SAFETY_ALARM_PIN, HIGH);
  
  Serial.print("[SAFETY] *** ALARM TRIGGERED: ");
  Serial.println(reason);
  Serial.print("[SAFETY] Fault count: ");
  Serial.println(safety_state.fault_count);
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
  if (axis < 4) {
    safety_state.current_fault = SAFETY_STALLED;
    char msg[64];
    snprintf(msg, sizeof(msg), "STALL on axis %d", axis);
    safetyTriggerAlarm(msg);
  }
}

void safetyReportSoftLimit(uint8_t axis) {
  if (axis < 4) {
    safety_state.current_fault = SAFETY_SOFT_LIMIT;
    char msg[64];
    snprintf(msg, sizeof(msg), "SOFT_LIMIT on axis %d", axis);
    safetyTriggerAlarm(msg);
  }
}

void safetyReportEncoderError(uint8_t axis) {
  if (axis < 4) {
    safety_state.current_fault = SAFETY_ENCODER_ERROR;
    char msg[64];
    snprintf(msg, sizeof(msg), "ENCODER_ERROR on axis %d", axis);
    safetyTriggerAlarm(msg);
  }
}

void safetyReportPLCFault() {
  safety_state.current_fault = SAFETY_PLC_FAULT;
  safetyTriggerAlarm("PLC_FAULT");
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
  
  Serial.println("\nRecent Fault History:");
  for (int i = 0; i < SAFETY_FAULT_HISTORY_SIZE; i++) {
    int idx = (safety_state.history_index + i) % SAFETY_FAULT_HISTORY_SIZE;
    if (safety_state.fault_history[idx] != SAFETY_OK) {
      Serial.print("  [");
      Serial.print(i);
      Serial.print("] Fault: ");
      Serial.println((int)safety_state.fault_history[idx]);
    }
  }
}

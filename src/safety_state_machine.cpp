#include "safety_state_machine.h"
#include "fault_logging.h"
#include "serial_logger.h"

static safety_fsm_state_t current_safety_state = FSM_OK;

const char* safetyStateToString(safety_fsm_state_t state) {
  switch (state) {
    case FSM_OK:        return "OK";
    case FSM_WARNING:   return "WARNING";
    case FSM_ALARM:     return "ALARM";
    case FSM_CRITICAL:  return "CRITICAL";
    case FSM_EMERGENCY: return "EMERGENCY";
    default:            return "UNKNOWN";
  }
}

const char* safetyTransitionDescription(safety_fsm_state_t from, safety_fsm_state_t to) {
  if (from == FSM_OK && to == FSM_WARNING) return "Warning condition detected";
  if (from == FSM_WARNING && to == FSM_ALARM) return "Warning escalated to alarm";
  if (from == FSM_ALARM && to == FSM_CRITICAL) return "Alarm escalated to critical";
  if (from == FSM_CRITICAL && to == FSM_EMERGENCY) return "Critical condition - E-Stop";
  if ((from == FSM_WARNING || from == FSM_ALARM || from == FSM_CRITICAL) && to == FSM_OK) return "Fault condition cleared";
  if (to == FSM_EMERGENCY) return "Emergency stop activated";
  return "State transition";
}

bool safetyIsValidStateTransition(safety_fsm_state_t current, safety_fsm_state_t new_state) {
  switch (current) {
    case FSM_OK:
      return (new_state == FSM_WARNING || new_state == FSM_EMERGENCY);
    case FSM_WARNING:
      return (new_state == FSM_ALARM || new_state == FSM_OK || new_state == FSM_EMERGENCY);
    case FSM_ALARM:
      return (new_state == FSM_CRITICAL || new_state == FSM_WARNING || new_state == FSM_OK || new_state == FSM_EMERGENCY);
    case FSM_CRITICAL:
      return (new_state == FSM_EMERGENCY || new_state == FSM_ALARM || new_state == FSM_WARNING || new_state == FSM_OK);
    case FSM_EMERGENCY:
      return (new_state == FSM_CRITICAL || new_state == FSM_ALARM || new_state == FSM_WARNING || new_state == FSM_OK);
    default:
      return false;
  }
}

bool safetySetState(safety_fsm_state_t new_state) {
  if (!safetyIsValidStateTransition(current_safety_state, new_state)) {
    Serial.printf("[SAFETY] [FAIL] Invalid FSM transition: %s -> %s\n", 
        safetyStateToString(current_safety_state), safetyStateToString(new_state));
    faultLogError(FAULT_BOOT_FAILED, "Invalid safety FSM transition");
    return false;
  }
  
  safety_fsm_state_t previous = current_safety_state;
  current_safety_state = new_state;
  
  Serial.printf("[SAFETY] FSM: %s -> %s (%s)\n", 
      safetyStateToString(previous), 
      safetyStateToString(new_state), 
      safetyTransitionDescription(previous, new_state));
  
  if (new_state == FSM_EMERGENCY || new_state == FSM_CRITICAL) {
    faultLogError(FAULT_EMERGENCY_HALT, safetyTransitionDescription(previous, new_state));
  } else if (new_state == FSM_ALARM) {
    faultLogWarning(FAULT_WATCHDOG_TIMEOUT, safetyTransitionDescription(previous, new_state));
  } else if (new_state == FSM_WARNING) {
    logWarning("[SAFETY] Warning: %s", safetyTransitionDescription(previous, new_state));
  }
  
  return true;
}

safety_fsm_state_t safetyGetState() {
  return current_safety_state;
}
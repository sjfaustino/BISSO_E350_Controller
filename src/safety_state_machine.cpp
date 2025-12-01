#include "safety_state_machine.h"
#include "fault_logging.h"
#include "serial_logger.h"

// FIX: Corrected internal state type to match the new header definition
static safety_fsm_state_t current_safety_state = FSM_OK;

// FIX: Corrected argument type and return type to safety_fsm_state_t
const char* safetyStateToString(safety_fsm_state_t state) {
  switch (state) {
    case FSM_OK:        return "OK";
    case FSM_WARNING:   return "WARNING";
    case FSM_ALARM:     return "ALARM";
    case FSM_CRITICAL:  return "CRITICAL";
    case FSM_EMERGENCY: return "EMERGENCY";
    default:               return "UNKNOWN";
  }
}

// FIX: Corrected argument types to safety_fsm_state_t
const char* safetyTransitionDescription(safety_fsm_state_t from, safety_fsm_state_t to) {
  // Describe why this transition is happening
  if (from == FSM_OK && to == FSM_WARNING) {
    return "Warning condition detected";
  }
  if (from == FSM_WARNING && to == FSM_ALARM) {
    return "Warning escalated to alarm";
  }
  if (from == FSM_ALARM && to == FSM_CRITICAL) {
    return "Alarm escalated to critical";
  }
  if (from == FSM_CRITICAL && to == FSM_EMERGENCY) {
    return "Critical condition - emergency stop activated";
  }
  if ((from == FSM_WARNING || from == FSM_ALARM || from == FSM_CRITICAL) && 
      to == FSM_OK) {
    return "Fault condition cleared";
  }
  if (to == FSM_EMERGENCY) {
    return "Emergency stop activated";
  }
  return "State transition";
}

// FIX: Corrected argument types and return type
bool safetyIsValidStateTransition(safety_fsm_state_t current, safety_fsm_state_t new_state) {
  // Safety state machine: can escalate to higher severity, or return to OK
  
  switch (current) {
    case FSM_OK:
      // From OK, can go to WARNING or EMERGENCY (direct e-stop)
      return (new_state == FSM_WARNING || new_state == FSM_EMERGENCY);
    
    case FSM_WARNING:
      // From WARNING, can go to: ALARM, back to OK, or EMERGENCY
      return (new_state == FSM_ALARM || 
              new_state == FSM_OK || 
              new_state == FSM_EMERGENCY);
    
    case FSM_ALARM:
      // From ALARM, can go to: CRITICAL, back to WARNING/OK, or EMERGENCY
      return (new_state == FSM_CRITICAL || 
              new_state == FSM_WARNING || 
              new_state == FSM_OK || 
              new_state == FSM_EMERGENCY);
    
    case FSM_CRITICAL:
      // From CRITICAL, can go to: EMERGENCY, or back to lower states
      return (new_state == FSM_EMERGENCY || 
              new_state == FSM_ALARM || 
              new_state == FSM_WARNING || 
              new_state == FSM_OK);
    
    case FSM_EMERGENCY:
      // From EMERGENCY, can only go back through controlled recovery
      return (new_state == FSM_CRITICAL || 
              new_state == FSM_ALARM || 
              new_state == FSM_WARNING || 
              new_state == FSM_OK);
    
    default:
      return false;
  }
}

// FIX: Corrected argument type
bool safetySetState(safety_fsm_state_t new_state) {
  // Validate transition
  if (!safetyIsValidStateTransition(current_safety_state, new_state)) {
    Serial.print("[SAFETY] ERROR: Invalid state transition from ");
    Serial.print(safetyStateToString(current_safety_state));
    Serial.print(" to ");
    Serial.println(safetyStateToString(new_state));
    
    faultLogError(FAULT_BOOT_FAILED, "Invalid safety state transition");
    return false;
  }
  
  safety_fsm_state_t previous = current_safety_state;
  current_safety_state = new_state;
  
  // Log transition
  Serial.print("[SAFETY] State transition: ");
  Serial.print(safetyStateToString(previous));
  Serial.print(" -> ");
  Serial.print(safetyStateToString(new_state));
  Serial.print(" (");
  Serial.print(safetyTransitionDescription(previous, new_state));
  Serial.println(")");
  
  // Log based on severity
  if (new_state == FSM_EMERGENCY || new_state == FSM_CRITICAL) {
    faultLogError(FAULT_EMERGENCY_HALT, safetyTransitionDescription(previous, new_state));
  } else if (new_state == FSM_ALARM) {
    faultLogWarning(FAULT_WATCHDOG_TIMEOUT, safetyTransitionDescription(previous, new_state));
  } else if (new_state == FSM_WARNING) {
    logWarning("Safety warning: %s", safetyTransitionDescription(previous, new_state));
  }
  
  return true;
}

// FIX: Corrected return type
safety_fsm_state_t safetyGetState() {
  return current_safety_state;
}
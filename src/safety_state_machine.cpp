#include "safety_state_machine.h"
#include "fault_logging.h"
#include "serial_logger.h"

// ============================================================================
// SAFETY STATE MACHINE IMPLEMENTATION
// ============================================================================

static safety_state_t current_safety_state = SAFETY_OK;

const char* safetyStateToString(safety_state_t state) {
  switch (state) {
    case SAFETY_OK:        return "OK";
    case SAFETY_WARNING:   return "WARNING";
    case SAFETY_ALARM:     return "ALARM";
    case SAFETY_CRITICAL:  return "CRITICAL";
    case SAFETY_EMERGENCY: return "EMERGENCY";
    default:               return "UNKNOWN";
  }
}

const char* safetyTransitionDescription(safety_state_t from, safety_state_t to) {
  // Describe why this transition is happening
  if (from == SAFETY_OK && to == SAFETY_WARNING) {
    return "Warning condition detected";
  }
  if (from == SAFETY_WARNING && to == SAFETY_ALARM) {
    return "Warning escalated to alarm";
  }
  if (from == SAFETY_ALARM && to == SAFETY_CRITICAL) {
    return "Alarm escalated to critical";
  }
  if (from == SAFETY_CRITICAL && to == SAFETY_EMERGENCY) {
    return "Critical condition - emergency stop activated";
  }
  if ((from == SAFETY_WARNING || from == SAFETY_ALARM || from == SAFETY_CRITICAL) && 
      to == SAFETY_OK) {
    return "Fault condition cleared";
  }
  if (to == SAFETY_EMERGENCY) {
    return "Emergency stop activated";
  }
  return "State transition";
}

bool safetyIsValidStateTransition(safety_state_t current, safety_state_t new_state) {
  // Safety state machine: can escalate to higher severity, or return to OK
  
  switch (current) {
    case SAFETY_OK:
      // From OK, can go to WARNING or EMERGENCY (direct e-stop)
      return (new_state == SAFETY_WARNING || new_state == SAFETY_EMERGENCY);
    
    case SAFETY_WARNING:
      // From WARNING, can go to: ALARM, back to OK, or EMERGENCY
      return (new_state == SAFETY_ALARM || 
              new_state == SAFETY_OK || 
              new_state == SAFETY_EMERGENCY);
    
    case SAFETY_ALARM:
      // From ALARM, can go to: CRITICAL, back to WARNING/OK, or EMERGENCY
      return (new_state == SAFETY_CRITICAL || 
              new_state == SAFETY_WARNING || 
              new_state == SAFETY_OK || 
              new_state == SAFETY_EMERGENCY);
    
    case SAFETY_CRITICAL:
      // From CRITICAL, can go to: EMERGENCY, or back to lower states
      return (new_state == SAFETY_EMERGENCY || 
              new_state == SAFETY_ALARM || 
              new_state == SAFETY_WARNING || 
              new_state == SAFETY_OK);
    
    case SAFETY_EMERGENCY:
      // From EMERGENCY, can only go back through controlled recovery
      return (new_state == SAFETY_CRITICAL || 
              new_state == SAFETY_ALARM || 
              new_state == SAFETY_WARNING || 
              new_state == SAFETY_OK);
    
    default:
      return false;
  }
}

bool safetySetState(safety_state_t new_state) {
  // Validate transition
  if (!safetyIsValidStateTransition(current_safety_state, new_state)) {
    Serial.print("[SAFETY] ERROR: Invalid state transition from ");
    Serial.print(safetyStateToString(current_safety_state));
    Serial.print(" to ");
    Serial.println(safetyStateToString(new_state));
    
    faultLogError(FAULT_BOOT_FAILED, "Invalid safety state transition");
    return false;
  }
  
  safety_state_t previous = current_safety_state;
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
  if (new_state == SAFETY_EMERGENCY || new_state == SAFETY_CRITICAL) {
    faultLogError(FAULT_EMERGENCY_HALT, safetyTransitionDescription(previous, new_state));
  } else if (new_state == SAFETY_ALARM) {
    faultLogWarning(FAULT_WATCHDOG_TIMEOUT, safetyTransitionDescription(previous, new_state));
  } else if (new_state == SAFETY_WARNING) {
    logWarning("Safety warning: %s", safetyTransitionDescription(previous, new_state));
  }
  
  return true;
}

safety_state_t safetyGetState() {
  return current_safety_state;
}

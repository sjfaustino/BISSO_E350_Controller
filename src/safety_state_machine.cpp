#include "safety_state_machine.h"
#include "fault_logging.h"
#include "serial_logger.h"

// ============================================================================
// STATE MACHINE DEFINITION
// ============================================================================

// Internal state
static safety_fsm_state_t current_safety_state = FSM_OK;

// Transition definition structure
struct SafetyTransition {
    safety_fsm_state_t from;
    safety_fsm_state_t to;
    const char* description;
    fault_severity_t log_severity;
};

// --- TRANSITION TABLE ---
// Defines ALL valid state changes. Any transition not listed here is INVALID.
static const SafetyTransition fsm_table[] = {
    // FROM OK
    {FSM_OK,       FSM_WARNING,   "Warning condition detected",   FAULT_WARNING},
    {FSM_OK,       FSM_EMERGENCY, "Emergency stop activated",     FAULT_CRITICAL},
    
    // FROM WARNING
    {FSM_WARNING,  FSM_OK,        "Warning cleared",              FAULT_NONE},
    {FSM_WARNING,  FSM_ALARM,     "Escalated to Alarm",           FAULT_WARNING},
    {FSM_WARNING,  FSM_EMERGENCY, "Emergency stop activated",     FAULT_CRITICAL},
    
    // FROM ALARM
    {FSM_ALARM,    FSM_OK,        "Alarm cleared",                FAULT_NONE},
    {FSM_ALARM,    FSM_WARNING,   "De-escalated to Warning",      FAULT_WARNING},
    {FSM_ALARM,    FSM_CRITICAL,  "Escalated to Critical",        FAULT_ERROR},
    {FSM_ALARM,    FSM_EMERGENCY, "Emergency stop activated",     FAULT_CRITICAL},

    // FROM CRITICAL
    {FSM_CRITICAL, FSM_OK,        "Critical fault cleared",       FAULT_NONE},
    {FSM_CRITICAL, FSM_WARNING,   "De-escalated to Warning",      FAULT_WARNING},
    {FSM_CRITICAL, FSM_ALARM,     "De-escalated to Alarm",        FAULT_WARNING},
    {FSM_CRITICAL, FSM_EMERGENCY, "Emergency stop activated",     FAULT_CRITICAL},

    // FROM EMERGENCY
    {FSM_EMERGENCY, FSM_OK,       "E-Stop Recovery",              FAULT_NONE},
    {FSM_EMERGENCY, FSM_CRITICAL, "Transition to Critical",       FAULT_ERROR},
    {FSM_EMERGENCY, FSM_ALARM,    "Transition to Alarm",          FAULT_WARNING},
    {FSM_EMERGENCY, FSM_WARNING,  "Transition to Warning",        FAULT_WARNING}
};

static const size_t FSM_TABLE_SIZE = sizeof(fsm_table) / sizeof(fsm_table[0]);

// ============================================================================
// IMPLEMENTATION
// ============================================================================

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

// Helper to find a transition entry
static const SafetyTransition* findTransition(safety_fsm_state_t from, safety_fsm_state_t to) {
    for (size_t i = 0; i < FSM_TABLE_SIZE; i++) {
        if (fsm_table[i].from == from && fsm_table[i].to == to) {
            return &fsm_table[i];
        }
    }
    return NULL;
}

const char* safetyTransitionDescription(safety_fsm_state_t from, safety_fsm_state_t to) {
    const SafetyTransition* t = findTransition(from, to);
    return t ? t->description : "Invalid Transition";
}

bool safetyIsValidStateTransition(safety_fsm_state_t current, safety_fsm_state_t new_state) {
    return findTransition(current, new_state) != NULL;
}

bool safetySetState(safety_fsm_state_t new_state) {
    // 1. No change check
    if (current_safety_state == new_state) return true;

    // 2. Lookup Transition
    const SafetyTransition* trans = findTransition(current_safety_state, new_state);

    // 3. Validate
    if (!trans) {
        Serial.printf("[SAFETY] [FAIL] Invalid FSM transition: %s -> %s\n", 
            safetyStateToString(current_safety_state), safetyStateToString(new_state));
        faultLogError(FAULT_BOOT_FAILED, "Invalid safety FSM transition attempt");
        return false;
    }

    // 4. Execute Transition
    safety_fsm_state_t previous = current_safety_state;
    current_safety_state = new_state;

    // 5. Log
    Serial.printf("[SAFETY] FSM: %s -> %s (%s)\n", 
        safetyStateToString(previous), 
        safetyStateToString(new_state), 
        trans->description);

    // 6. Route to Fault System based on severity defined in table
    if (trans->log_severity == FAULT_CRITICAL) {
        faultLogCritical(FAULT_EMERGENCY_HALT, trans->description);
    } else if (trans->log_severity == FAULT_ERROR) {
        faultLogError(FAULT_CRITICAL_SYSTEM_ERROR, trans->description);
    } else if (trans->log_severity == FAULT_WARNING) {
        faultLogWarning(FAULT_SAFETY_INTERLOCK, trans->description);
    } else {
        logInfo("[SAFETY] %s", trans->description);
    }

    return true;
}

safety_fsm_state_t safetyGetState() {
  return current_safety_state;
}
#ifndef SAFETY_STATE_MACHINE_H
#define SAFETY_STATE_MACHINE_H

#include <Arduino.h>

// ============================================================================
// SAFETY FSM STATE DEFINITIONS
// ============================================================================

// FIX: Renaming the type to safety_fsm_state_t to resolve the structural conflict.
typedef enum {
  FSM_OK = 0,              // Normal operation
  FSM_WARNING = 1,         // Warning condition
  FSM_ALARM = 2,           // Alarm - reduced operation
  FSM_CRITICAL = 3,        // Critical fault - motion stopped
  FSM_EMERGENCY = 4        // Emergency stop activated
} safety_fsm_state_t; // This is the final name for the ENUM type

/**
 * Validate safety state transition
 * * @param current Current safety state
 * @param new_state Requested new state
 * @return true if transition is valid, false otherwise
 */
bool safetyIsValidStateTransition(safety_fsm_state_t current, safety_fsm_state_t new_state);

/**
 * Set safety state with validation
 * * @param new_state Requested new safety state
 * @return true if state changed, false if invalid transition
 */
bool safetySetState(safety_fsm_state_t new_state);

/**
 * Get current safety state
 * * @return Current safety state
 */
safety_fsm_state_t safetyGetState();

/**
 * Convert safety state to string for diagnostics
 * * @param state Safety state
 * @return String representation of state
 */
const char* safetyStateToString(safety_fsm_state_t state);

/**
 * Get transition description for logging
 * * @param from Source state
 * @param to Destination state
 * @return String describing the transition
 */
const char* safetyTransitionDescription(safety_fsm_state_t from, safety_fsm_state_t to);

#endif // SAFETY_STATE_MACHINE_H
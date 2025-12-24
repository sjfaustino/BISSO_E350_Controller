/**
 * @file motion_state_machine.h
 * @brief Formal State Machine Framework for Motion Control
 * @project BISSO E350 Controller
 * @details Table-driven FSM with entry/exit actions and thread-safe transitions
 */

#ifndef MOTION_STATE_MACHINE_H
#define MOTION_STATE_MACHINE_H

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include "motion.h"

// Forward declaration
class Axis;

/**
 * @brief State handler function type
 * @param axis Pointer to axis instance
 * @param current_pos Current encoder position
 * @param global_target Global target position
 * @param consensus_active Whether PLC consensus signal is active
 */
typedef void (*state_handler_t)(Axis* axis, int32_t current_pos,
                                 int32_t global_target, bool consensus_active);

/**
 * @brief State entry/exit callback function type
 * @param axis Pointer to axis instance
 */
typedef void (*state_callback_t)(Axis* axis);

/**
 * @brief State machine definition structure
 */
typedef struct {
    motion_state_t state;           // State identifier
    const char* name;               // State name (for logging)
    state_callback_t on_entry;      // Called when entering this state (can be NULL)
    state_handler_t handler;        // Called every update cycle in this state
    state_callback_t on_exit;       // Called when leaving this state (can be NULL)
} state_definition_t;

/**
 * @brief Transition structure for explicit state machine transitions
 */
typedef struct {
    motion_state_t from_state;      // Source state
    motion_state_t to_state;        // Destination state
    bool (*condition)(Axis* axis);  // Transition condition (can be NULL for always)
} state_transition_t;

/**
 * @brief Motion State Machine Class
 * @details Manages state transitions and state handler execution
 */
class MotionStateMachine {
public:
    /**
     * @brief Initialize state machine
     */
    static void init();

    /**
     * @brief Execute state machine update for an axis
     * @param axis Axis to update
     * @param current_pos Current encoder position
     * @param global_target Global target position
     * @param consensus_active Whether PLC consensus signal is active
     */
    static void update(Axis* axis, int32_t current_pos,
                      int32_t global_target, bool consensus_active);

    /**
     * @brief Request state transition (thread-safe)
     * @param axis Axis to transition
     * @param new_state Target state
     * @return true if transition was successful
     */
    static bool transitionTo(Axis* axis, motion_state_t new_state);

    /**
     * @brief Get state handler for a given state
     * @param state State to look up
     * @return Pointer to state definition, or NULL if not found
     */
    static const state_definition_t* getStateDefinition(motion_state_t state);

private:
    /**
     * @brief Execute entry callback for a state
     * @param axis Axis entering the state
     * @param state New state
     */
    static void executeEntryAction(Axis* axis, motion_state_t state);

    /**
     * @brief Execute exit callback for a state
     * @param axis Axis exiting the state
     * @param state Old state
     */
    static void executeExitAction(Axis* axis, motion_state_t state);

    /**
     * @brief Check if a transition is valid
     * @param from_state Source state
     * @param to_state Destination state
     * @return true if transition is allowed
     */
    static bool isValidTransition(motion_state_t from_state, motion_state_t to_state);
};

// ============================================================================
// STATE HANDLER DECLARATIONS
// ============================================================================

// Core motion states
void state_idle_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_wait_consenso_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_executing_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_stopping_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_paused_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_error_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);

// Homing states
void state_homing_approach_fast_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_homing_backoff_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_homing_approach_fine_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_homing_settle_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);

// Special states
void state_dwell_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);
void state_wait_pin_handler(Axis* axis, int32_t pos, int32_t target, bool consensus);

// Entry/Exit callbacks (optional, define as needed)
void state_executing_entry(Axis* axis);
void state_stopping_entry(Axis* axis);
void state_idle_entry(Axis* axis);
void state_error_entry(Axis* axis);

#endif // MOTION_STATE_MACHINE_H

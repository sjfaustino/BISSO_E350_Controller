/**
 * @file axis.h
 * @brief Axis class definition for motion control
 */

#ifndef AXIS_H
#define AXIS_H

#include <Arduino.h>
#include "system_constants.h"
#include "motion_state.h"

// Forward declaration of MotionStateMachine to avoid circular dependencies
class Axis {
public:
    uint8_t id;
    motion_state_t state;
    int32_t position;
    int32_t prev_position;
    int32_t target_position;
    int32_t position_at_stop;
    bool enabled;
    bool soft_limit_enabled;
    int32_t soft_limit_min;
    int32_t soft_limit_max;
    
    // Timing & Status
    uint32_t dwell_end_ms;
    uint32_t prev_update_ms;
    uint32_t last_actual_update_ms;
    int32_t last_actual_position;
    int32_t predicted_position;
    uint32_t state_entry_ms;             // Timestamp of last state transition
    int32_t homing_trigger_pos;          // Position when homing sensor triggered
    float velocity_counts_ms;
    float current_velocity_mm_s;
    float commanded_speed_mm_s;
    int saved_speed_profile;

    // Maintenance & Distance
    double accumulated_distance_units;
    bool maintenance_alert_logged;
    
    // I/O Wait
    uint8_t wait_pin_id;
    uint8_t wait_pin_type;
    uint8_t wait_pin_state;
    uint32_t wait_pin_timeout_ms;

    bool _error_logged;
    bool prediction_stale_logged; 

    Axis();
    void init(uint8_t axis_id);
    bool checkSoftLimits(bool strict_mode);
    void updateState(int32_t current_pos, int32_t global_target_pos, bool consensus_active);

    // Friends for state machine access
    friend class MotionStateMachine;
};

#endif // AXIS_H

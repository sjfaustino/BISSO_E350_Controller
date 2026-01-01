/**
 * @file test/mocks/plc_mock.cpp
 * @brief Mock implementation of PLC contactor system for testing
 */

#include "plc_mock.h"
#include <cstdio>
#include <cstring>

plc_mock_state_t plc_mock_init(void)
{
    plc_mock_state_t plc;
    std::memset(&plc, 0, sizeof(plc));

    plc.active_axis = AXIS_NONE;
    plc.previous_axis = AXIS_NONE;
    plc.x_select_gpio = 0;
    plc.y_select_gpio = 0;
    plc.z_select_gpio = 0;
    plc.motor_run_relay = 0;
    
    // Bit-level output state
    plc.output_register = 0xFF;    // All OFF (active-low)
    plc.last_output_register = 0xFF;
    plc.output_write_count = 0;
    plc.axis_select = 255;         // No axis
    plc.direction_positive = 0;
    plc.direction_negative = 0;
    plc.speed_profile = 255;       // No speed
    
    plc.last_switch_time_ms = 0;
    plc.switch_settling_ms = 50;  // Typical contactor settling time
    plc.contactor_operations = 0;
    plc.has_switching_error = 0;

    return plc;
}

void plc_mock_select_axis(plc_mock_state_t* plc, axis_t axis)
{
    if (!plc) return;

    // Clear all axis selections
    plc->x_select_gpio = 0;
    plc->y_select_gpio = 0;
    plc->z_select_gpio = 0;

    // Set the new axis
    switch (axis) {
        case AXIS_X:
            plc->x_select_gpio = 1;
            break;
        case AXIS_Y:
            plc->y_select_gpio = 1;
            break;
        case AXIS_Z:
            plc->z_select_gpio = 1;
            break;
        case AXIS_NONE:
            // All clear (already done above)
            break;
    }

    // If axis changed, count the operation and start settling timer
    if (axis != plc->active_axis) {
        plc->previous_axis = plc->active_axis;
        plc->active_axis = axis;

        // Only count actual switches (not idle transitions)
        if (plc->previous_axis != AXIS_NONE || axis != AXIS_NONE) {
            plc->contactor_operations++;
        }

        plc->last_switch_time_ms = 0;  // Start settling timer
    }
}

void plc_mock_set_motor_run(plc_mock_state_t* plc, uint8_t run)
{
    if (!plc) return;

    plc->motor_run_relay = run ? 1 : 0;
}

void plc_mock_advance_time(plc_mock_state_t* plc, uint32_t time_ms)
{
    if (!plc) return;

    plc->last_switch_time_ms += time_ms;
}

uint8_t plc_mock_is_settled(plc_mock_state_t* plc)
{
    if (!plc) return 1;

    // Check if settling time has passed
    return (plc->last_switch_time_ms >= plc->switch_settling_ms) ? 1 : 0;
}

void plc_mock_inject_switching_error(plc_mock_state_t* plc)
{
    if (!plc) return;

    plc->has_switching_error = 1;
}

axis_t plc_mock_get_active_axis(plc_mock_state_t* plc)
{
    if (!plc) return AXIS_NONE;

    // Only return active axis if contactor is settled
    if (plc_mock_is_settled(plc)) {
        return plc->active_axis;
    }

    // During settling, return previous axis as "active"
    return (axis_t)plc->previous_axis;
}

uint8_t plc_mock_is_axis_selected(plc_mock_state_t* plc, axis_t axis)
{
    if (!plc) return 0;

    if (plc->has_switching_error) {
        return 0;  // Switching failed
    }

    // Check if contactor is settled
    if (!plc_mock_is_settled(plc)) {
        return 0;  // Still switching
    }

    return (plc->active_axis == axis) ? 1 : 0;
}

uint8_t plc_mock_get_motor_run(plc_mock_state_t* plc)
{
    if (!plc) return 0;

    return plc->motor_run_relay;
}

uint32_t plc_mock_get_operations(plc_mock_state_t* plc)
{
    if (!plc) return 0;

    return plc->contactor_operations;
}

void plc_mock_reset_operation_count(plc_mock_state_t* plc)
{
    if (!plc) return;

    plc->contactor_operations = 0;
}

void plc_mock_get_status(plc_mock_state_t* plc, char* buffer, size_t buffer_size)
{
    if (!plc || !buffer) return;

    const char* axis_names[] = {"X", "Y", "Z", "NONE"};
    const char* active_name = (plc->active_axis <= AXIS_Z)
        ? axis_names[plc->active_axis]
        : "NONE";
    const char* error_str = plc->has_switching_error ? "ERROR" : "OK";
    const char* settled_str = plc_mock_is_settled(plc) ? "settled" : "switching";

    snprintf(buffer, buffer_size,
        "PLC[%s] Active:%s Motor:%s Time:%ums Ops:%u %s Reg:0x%02X",
        error_str,
        active_name,
        plc->motor_run_relay ? "RUN" : "STOP",
        plc->last_switch_time_ms,
        plc->contactor_operations,
        settled_str,
        plc->output_register);
}

// =====================================================================
// BIT-LEVEL OUTPUT CAPTURE IMPLEMENTATION
// =====================================================================

// Bit definitions matching plc_iface.h
#define PLC_OUT_AXIS_X_SELECT 0
#define PLC_OUT_AXIS_Y_SELECT 1
#define PLC_OUT_AXIS_Z_SELECT 2
#define PLC_OUT_DIR_POSITIVE  3
#define PLC_OUT_DIR_NEGATIVE  4
#define PLC_OUT_SPEED_FAST    5
#define PLC_OUT_SPEED_MEDIUM  6
#define PLC_OUT_SPEED_SLOW    7

void plc_mock_write_output(plc_mock_state_t* plc, uint8_t value)
{
    if (!plc) return;

    plc->last_output_register = plc->output_register;
    plc->output_register = value;
    plc->output_write_count++;

    // Decode axis select (active-low: 0 = ON)
    // Only one should be active, but check all
    if (!(value & (1 << PLC_OUT_AXIS_X_SELECT))) {
        plc->axis_select = 0;  // X
    } else if (!(value & (1 << PLC_OUT_AXIS_Y_SELECT))) {
        plc->axis_select = 1;  // Y
    } else if (!(value & (1 << PLC_OUT_AXIS_Z_SELECT))) {
        plc->axis_select = 2;  // Z
    } else {
        plc->axis_select = 255;  // None
    }

    // Decode direction (active-low)
    plc->direction_positive = !(value & (1 << PLC_OUT_DIR_POSITIVE)) ? 1 : 0;
    plc->direction_negative = !(value & (1 << PLC_OUT_DIR_NEGATIVE)) ? 1 : 0;

    // Decode speed profile (active-low, only one should be set)
    // Maps: FAST=Y6(bit5), MEDIUM=Y7(bit6), SLOW=Y8(bit7)
    // After inversion fix: profile 0=slow, 1=medium, 2=fast
    if (!(value & (1 << PLC_OUT_SPEED_FAST))) {
        plc->speed_profile = 2;  // Hardware FAST = profile 2
    } else if (!(value & (1 << PLC_OUT_SPEED_MEDIUM))) {
        plc->speed_profile = 1;  // Hardware MEDIUM = profile 1
    } else if (!(value & (1 << PLC_OUT_SPEED_SLOW))) {
        plc->speed_profile = 0;  // Hardware SLOW = profile 0
    } else {
        plc->speed_profile = 255;  // No speed set
    }
}

uint8_t plc_mock_get_output_register(plc_mock_state_t* plc)
{
    return plc ? plc->output_register : 0xFF;
}

uint8_t plc_mock_get_axis_select(plc_mock_state_t* plc)
{
    return plc ? plc->axis_select : 255;
}

uint8_t plc_mock_get_direction(plc_mock_state_t* plc)
{
    if (!plc) return 255;
    if (plc->direction_positive && !plc->direction_negative) return 1;
    if (!plc->direction_positive && plc->direction_negative) return 0;
    return 255;  // Invalid or no direction
}

uint8_t plc_mock_get_speed_profile(plc_mock_state_t* plc)
{
    return plc ? plc->speed_profile : 255;
}

uint32_t plc_mock_get_write_count(plc_mock_state_t* plc)
{
    return plc ? plc->output_write_count : 0;
}

void plc_mock_reset(plc_mock_state_t* plc)
{
    if (!plc) return;
    
    *plc = plc_mock_init();
}

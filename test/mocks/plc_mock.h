/**
 * @file test/mocks/plc_mock.h
 * @brief Mock implementation of PLC contactor system for testing
 *
 * Models the PLC that receives signals from the ESP32 controller and switches
 * motor power between axes. Only one axis can be active at a time via contactors.
 */

#ifndef PLC_MOCK_H
#define PLC_MOCK_H

#include <cstdint>
#include <cstddef>

/**
 * @brief Axis enumeration
 */
typedef enum {
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2,
    AXIS_NONE = 255
} axis_t;

/**
 * @brief Mock PLC contactor system state
 */
typedef struct {
    // Contactor switching state
    axis_t active_axis;              // Currently active axis (AXIS_NONE if idle)
    uint8_t previous_axis;           // Previous active axis

    // Relay states (simulating GPIO inputs from ESP32)
    uint8_t x_select_gpio;           // GPIO state to select X axis
    uint8_t y_select_gpio;           // GPIO state to select Y axis
    uint8_t z_select_gpio;           // GPIO state to select Z axis
    uint8_t motor_run_relay;         // GPIO state for VFD run relay (r1)

    // =====================================================================
    // BIT-LEVEL OUTPUT CAPTURE (for testing new PLC API)
    // =====================================================================
    uint8_t output_register;         // Raw 8-bit output (mirrors shadow register)
    uint8_t last_output_register;    // Previous output for change detection
    uint32_t output_write_count;     // Number of I2C writes to output
    
    // Decoded output bits (for test assertions)
    uint8_t axis_select;             // 0=X, 1=Y, 2=Z, 255=none
    uint8_t direction_positive;      // 1=Y4 active (forward)
    uint8_t direction_negative;      // 1=Y5 active (reverse)
    uint8_t speed_profile;           // 0=fast, 1=medium, 2=slow, 255=none

    // Switching timing
    uint32_t last_switch_time_ms;    // Timestamp of last contactor switch
    uint16_t switch_settling_ms;     // Time to wait after switch (~50ms typical)

    // Diagnostic counters
    uint32_t contactor_operations;   // Total number of contactor switches
    uint8_t has_switching_error;     // Flag if switching failed
} plc_mock_state_t;

/**
 * @brief Initialize PLC mock to default state
 * All axes idle, no motor power
 *
 * @return Initialized PLC mock state
 */
extern plc_mock_state_t plc_mock_init(void);

/**
 * @brief Set GPIO input to select axis
 * Simulates ESP32 controller setting GPIO pins to select which axis to activate
 *
 * @param plc Pointer to PLC state
 * @param axis Axis to select (AXIS_X, AXIS_Y, AXIS_Z, or AXIS_NONE for idle)
 */
extern void plc_mock_select_axis(plc_mock_state_t* plc, axis_t axis);

/**
 * @brief Set motor run relay GPIO
 * Simulates ESP32 setting the r1 relay pin to start/stop VFD
 *
 * @param plc Pointer to PLC state
 * @param run 1 to start motor, 0 to stop
 */
extern void plc_mock_set_motor_run(plc_mock_state_t* plc, uint8_t run);

/**
 * @brief Simulate contactor settling time
 * In reality, contactors take ~50ms to physically switch over
 *
 * @param plc Pointer to PLC state
 * @param time_ms Time to advance
 */
extern void plc_mock_advance_time(plc_mock_state_t* plc, uint32_t time_ms);

/**
 * @brief Check if contactor has finished switching
 * Returns 1 when safe to start motion on newly selected axis
 *
 * @param plc Pointer to PLC state
 * @return 1 if contactor settled, 0 if still switching
 */
extern uint8_t plc_mock_is_settled(plc_mock_state_t* plc);

/**
 * @brief Inject a contactor switching error
 * Simulates a case where contactor fails to switch properly
 *
 * @param plc Pointer to PLC state
 */
extern void plc_mock_inject_switching_error(plc_mock_state_t* plc);

/**
 * @brief Get current active axis
 *
 * @param plc Pointer to PLC state
 * @return Currently active axis
 */
extern axis_t plc_mock_get_active_axis(plc_mock_state_t* plc);

/**
 * @brief Check if axis is currently selected (contactor closed)
 *
 * @param plc Pointer to PLC state
 * @param axis Axis to check
 * @return 1 if selected, 0 otherwise
 */
extern uint8_t plc_mock_is_axis_selected(plc_mock_state_t* plc, axis_t axis);

/**
 * @brief Get motor run state
 *
 * @param plc Pointer to PLC state
 * @return 1 if motor run relay is active, 0 otherwise
 */
extern uint8_t plc_mock_get_motor_run(plc_mock_state_t* plc);

/**
 * @brief Get contactor operation count
 * Used to verify contactor wear estimation
 *
 * @param plc Pointer to PLC state
 * @return Total number of contactor switching operations
 */
extern uint32_t plc_mock_get_operations(plc_mock_state_t* plc);

/**
 * @brief Reset contactor operation counter
 *
 * @param plc Pointer to PLC state
 */
extern void plc_mock_reset_operation_count(plc_mock_state_t* plc);

/**
 * @brief Get PLC status summary for debugging
 *
 * @param plc Pointer to PLC state
 * @param buffer Output buffer for status string
 * @param buffer_size Size of output buffer
 */
extern void plc_mock_get_status(plc_mock_state_t* plc, char* buffer, size_t buffer_size);

// =====================================================================
// BIT-LEVEL OUTPUT CAPTURE API (for testing new PLC interface)
// =====================================================================

/**
 * @brief Simulate an I2C write to the output register
 * This intercepts what would be written to the PCF8574 @ 0x24
 *
 * @param plc Pointer to PLC state
 * @param value 8-bit value written (active-low: 0=ON, 1=OFF)
 */
extern void plc_mock_write_output(plc_mock_state_t* plc, uint8_t value);

/**
 * @brief Get the raw output register value
 *
 * @param plc Pointer to PLC state
 * @return Raw 8-bit output register
 */
extern uint8_t plc_mock_get_output_register(plc_mock_state_t* plc);

/**
 * @brief Get the decoded axis selection (0=X, 1=Y, 2=Z, 255=none)
 */
extern uint8_t plc_mock_get_axis_select(plc_mock_state_t* plc);

/**
 * @brief Get the decoded direction (1=positive, 0=negative, 255=none)
 */
extern uint8_t plc_mock_get_direction(plc_mock_state_t* plc);

/**
 * @brief Get the decoded speed profile (0=fast, 1=medium, 2=slow, 255=none)
 */
extern uint8_t plc_mock_get_speed_profile(plc_mock_state_t* plc);

/**
 * @brief Get the I2C write count for verification
 */
extern uint32_t plc_mock_get_write_count(plc_mock_state_t* plc);

/**
 * @brief Reset the mock for a new test
 */
extern void plc_mock_reset(plc_mock_state_t* plc);

#endif // PLC_MOCK_H

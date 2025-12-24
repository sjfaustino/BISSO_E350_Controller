/**
 * @file test/mocks/motion_mock.h
 * @brief Mock implementation of motion controller for testing
 *
 * Models the motion planning and validation system that ensures safe,
 * coordinated axis motion within physical constraints.
 */

#ifndef MOTION_MOCK_H
#define MOTION_MOCK_H

#include <cstdint>
#include <cstddef>

/**
 * @brief Motion state enumeration
 */
typedef enum {
    MOTION_IDLE = 0,
    MOTION_MOVING = 1,
    MOTION_STALLED = 2,
    MOTION_ERROR = 3,
    MOTION_E_STOPPED = 4
} motion_state_t;

/**
 * @brief Move validation result
 */
typedef enum {
    MOVE_VALID = 0,
    MOVE_INVALID_AXIS = 1,
    MOVE_INVALID_DISTANCE = 2,
    MOVE_INVALID_SPEED = 3,
    MOVE_SOFT_LIMIT_VIOLATION = 4,
    MOVE_HARDWARE_ERROR = 5
} move_validation_result_t;

/**
 * @brief Stall detection status
 */
typedef enum {
    STALL_NONE = 0,
    STALL_WARNING = 1,      // Motor current too high, might stall
    STALL_DETECTED = 2,     // Motor not moving, current present
} stall_status_t;

/**
 * @brief Mock motion controller state
 * Represents the motion planning and validation system
 */
typedef struct {
    // Current motion state
    motion_state_t state;               // Current motion state
    uint8_t active_axis;                // Currently active axis (0=X, 1=Y, 2=Z)

    // Motion parameters
    int32_t target_position_steps;      // Target position in encoder steps
    int32_t current_position_steps;     // Current position in encoder steps
    uint16_t current_speed_hz;          // Current VFD speed in Hz
    uint32_t motion_start_time_ms;      // When current motion started

    // Soft limits
    int32_t soft_limit_low_steps;       // Minimum allowed position
    int32_t soft_limit_high_steps;      // Maximum allowed position

    // Speed constraints
    uint16_t min_safe_speed_hz;         // Minimum speed (LSP, default 1 Hz)
    uint16_t max_safe_speed_hz;         // Maximum speed (HSP, default 105 Hz)

    // Stall detection
    stall_status_t stall_status;        // Current stall detection status
    float motor_current_amps;           // Current motor draw
    float stall_current_threshold_amps; // Current threshold for stall detection
    uint32_t stall_detection_time_ms;   // Time motor has been over threshold

    // Quality metrics
    float motion_quality_score;         // 0-100%, based on jitter and velocity match
    float velocity_deviation_percent;   // How far actual velocity deviates from target

    // Diagnostic counters
    uint32_t move_attempts;             // Total attempted moves
    uint32_t move_completed;            // Successfully completed moves
    uint32_t move_errors;               // Moves that failed

    // Emergency stop state
    uint8_t e_stop_active;              // Emergency stop is active
} motion_mock_state_t;

/**
 * @brief Initialize motion controller mock to default state
 *
 * Default configuration:
 * - Idle state
 * - No active motion
 * - Soft limits: 0 to 500mm (assuming 100 PPM encoder)
 * - Speed range: 1-105 Hz
 *
 * @return Initialized motion mock state
 */
extern motion_mock_state_t motion_mock_init(void);

/**
 * @brief Validate and plan a move
 * Checks constraints before allowing motion to start
 *
 * @param motion Pointer to motion state
 * @param axis Axis to move (0=X, 1=Y, 2=Z)
 * @param distance_steps Distance to move in encoder steps
 * @param speed_hz Speed in Hz
 * @return Move validation result
 */
extern move_validation_result_t motion_mock_validate_move(
    motion_mock_state_t* motion,
    uint8_t axis,
    int32_t distance_steps,
    uint16_t speed_hz);

/**
 * @brief Start a validated move
 *
 * @param motion Pointer to motion state
 * @param axis Axis to move
 * @param distance_steps Distance in steps
 * @param speed_hz Speed in Hz
 */
extern void motion_mock_start_move(
    motion_mock_state_t* motion,
    uint8_t axis,
    int32_t distance_steps,
    uint16_t speed_hz);

/**
 * @brief Simulate time passing and update motion state
 * Advances motion, detects stalls, updates quality metrics
 *
 * @param motion Pointer to motion state
 * @param encoder_feedback_steps Actual position from encoder
 * @param encoder_velocity_mms Actual velocity from encoder
 * @param motor_current_amps Current motor draw in amps
 * @param time_ms Time to advance in milliseconds
 */
extern void motion_mock_update(
    motion_mock_state_t* motion,
    int32_t encoder_feedback_steps,
    float encoder_velocity_mms,
    float motor_current_amps,
    uint32_t time_ms);

/**
 * @brief Check if motion is complete
 *
 * @param motion Pointer to motion state
 * @return 1 if motion complete, 0 otherwise
 */
extern uint8_t motion_mock_is_complete(motion_mock_state_t* motion);

/**
 * @brief Activate emergency stop
 * Immediately halts motion and prevents new moves
 *
 * @param motion Pointer to motion state
 */
extern void motion_mock_e_stop(motion_mock_state_t* motion);

/**
 * @brief Deactivate emergency stop
 * Allows motion to resume
 *
 * @param motion Pointer to motion state
 */
extern void motion_mock_clear_e_stop(motion_mock_state_t* motion);

/**
 * @brief Set soft limit for axis
 *
 * @param motion Pointer to motion state
 * @param axis Axis to set limit for
 * @param low_steps Lower limit in encoder steps
 * @param high_steps Upper limit in encoder steps
 */
extern void motion_mock_set_soft_limits(
    motion_mock_state_t* motion,
    uint8_t axis,
    int32_t low_steps,
    int32_t high_steps);

/**
 * @brief Get stall detection status
 *
 * @param motion Pointer to motion state
 * @return Current stall detection status
 */
extern stall_status_t motion_mock_get_stall_status(motion_mock_state_t* motion);

/**
 * @brief Get motion quality score
 * Based on velocity match and jitter
 *
 * @param motion Pointer to motion state
 * @return Quality score 0-100%
 */
extern float motion_mock_get_quality_score(motion_mock_state_t* motion);

/**
 * @brief Get velocity deviation from target
 *
 * @param motion Pointer to motion state
 * @return Deviation as percentage
 */
extern float motion_mock_get_velocity_deviation(motion_mock_state_t* motion);

/**
 * @brief Get current motion state
 *
 * @param motion Pointer to motion state
 * @return Current motion state
 */
extern motion_state_t motion_mock_get_state(motion_mock_state_t* motion);

/**
 * @brief Reset motion controller to idle state
 *
 * @param motion Pointer to motion state
 */
extern void motion_mock_reset(motion_mock_state_t* motion);

/**
 * @brief Get motion diagnostics summary
 *
 * @param motion Pointer to motion state
 * @param buffer Output buffer for status string
 * @param buffer_size Size of output buffer
 */
extern void motion_mock_get_diagnostics(motion_mock_state_t* motion, char* buffer, size_t buffer_size);

#endif // MOTION_MOCK_H

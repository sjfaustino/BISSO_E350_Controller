/**
 * @file test/mocks/encoder_mock.h
 * @brief Mock implementation of WJ66 optical encoder for testing
 *
 * Models the behavior of optical encoders mounted on each motor axis.
 * WJ66 encoders typically provide 100 PPR (pulses per revolution).
 */

#ifndef ENCODER_MOCK_H
#define ENCODER_MOCK_H

#include <cstdint>
#include <cmath>

/**
 * @brief Mock encoder state
 * Represents one encoder on an axis (X, Y, or Z)
 */
typedef struct {
    // Calibration state
    uint16_t ppm;                   // Pulses per millimeter (calibration value, ~100 typical)
    uint8_t calibrated;             // 1 if calibrated, 0 if needs calibration

    // Position tracking
    int32_t pulse_count;            // Absolute pulse count (can go negative)
    float position_mm;              // Current position in millimeters

    // Velocity tracking
    float velocity_mms;             // Current velocity in mm/s
    float target_velocity_mms;      // Target velocity (from VFD)

    // Motion jitter and noise simulation
    float jitter_amplitude;         // Jitter amplitude in mm/s (0.0-2.0 typical)
    float noise_frequency_hz;       // Noise oscillation frequency

    // Communication
    uint32_t last_update_ms;        // Timestamp of last position update
    uint8_t comms_error;            // Communication error flag
    uint8_t comms_timeout;          // No data received timeout flag

    // Deviation tracking
    float deviation_percent;        // Deviation from expected velocity (0-100%)
    float max_deviation_seen;       // Maximum deviation seen during motion

    // Status
    uint8_t is_moving;              // 1 if axis is currently moving
} encoder_mock_state_t;

/**
 * @brief Initialize encoder mock to default state
 *
 * Default configuration:
 * - PPM = 100 (standard WJ66 encoding)
 * - Position = 0mm
 * - Not calibrated
 * - No jitter/noise
 *
 * @return Initialized encoder mock state
 */
extern encoder_mock_state_t encoder_mock_init(void);

/**
 * @brief Set encoder calibration (PPM - pulses per millimeter)
 * Must be done before accurate motion tracking
 *
 * @param encoder Pointer to encoder state
 * @param ppm Pulses per millimeter (typical: 100 for WJ66)
 */
extern void encoder_mock_calibrate(encoder_mock_state_t* encoder, uint16_t ppm);

/**
 * @brief Set target velocity for encoder to track
 * Simulates what the motor *should* be doing based on VFD frequency
 *
 * @param encoder Pointer to encoder state
 * @param velocity_mms Target velocity in mm/s
 */
extern void encoder_mock_set_target_velocity(encoder_mock_state_t* encoder, float velocity_mms);

/**
 * @brief Simulate time passing and update encoder position
 * Advances position based on current velocity, adds jitter/noise
 *
 * @param encoder Pointer to encoder state
 * @param time_ms Time to advance in milliseconds
 */
extern void encoder_mock_advance_time(encoder_mock_state_t* encoder, uint32_t time_ms);

/**
 * @brief Inject motion jitter to simulate wear
 * Jitter amplitude increases with bearing wear
 *
 * @param encoder Pointer to encoder state
 * @param jitter_amplitude_mms Jitter amplitude in mm/s
 */
extern void encoder_mock_inject_jitter(encoder_mock_state_t* encoder, float jitter_amplitude_mms);

/**
 * @brief Inject deviation from expected velocity
 * Models cases where motor isn't achieving target speed
 *
 * @param encoder Pointer to encoder state
 * @param deviation_percent Deviation as percentage (e.g., 50 = 50% slower)
 */
extern void encoder_mock_inject_deviation(encoder_mock_state_t* encoder, float deviation_percent);

/**
 * @brief Inject communication error
 * Simulates case where encoder stops responding
 *
 * @param encoder Pointer to encoder state
 */
extern void encoder_mock_inject_comms_error(encoder_mock_state_t* encoder);

/**
 * @brief Clear communication error
 *
 * @param encoder Pointer to encoder state
 */
extern void encoder_mock_clear_comms_error(encoder_mock_state_t* encoder);

/**
 * @brief Get current position in millimeters
 *
 * @param encoder Pointer to encoder state
 * @return Current position in mm
 */
extern float encoder_mock_get_position_mm(encoder_mock_state_t* encoder);

/**
 * @brief Get current position in pulses
 *
 * @param encoder Pointer to encoder state
 * @return Current pulse count
 */
extern int32_t encoder_mock_get_position_pulses(encoder_mock_state_t* encoder);

/**
 * @brief Get current velocity in mm/s
 *
 * @param encoder Pointer to encoder state
 * @return Current velocity
 */
extern float encoder_mock_get_velocity_mms(encoder_mock_state_t* encoder);

/**
 * @brief Get jitter amplitude measurement
 *
 * @param encoder Pointer to encoder state
 * @return Jitter amplitude in mm/s
 */
extern float encoder_mock_get_jitter_amplitude(encoder_mock_state_t* encoder);

/**
 * @brief Get deviation from target
 *
 * @param encoder Pointer to encoder state
 * @return Deviation as percentage
 */
extern float encoder_mock_get_deviation(encoder_mock_state_t* encoder);

/**
 * @brief Check if encoder is calibrated
 *
 * @param encoder Pointer to encoder state
 * @return 1 if calibrated, 0 otherwise
 */
extern uint8_t encoder_mock_is_calibrated(encoder_mock_state_t* encoder);

/**
 * @brief Check for communication errors
 *
 * @param encoder Pointer to encoder state
 * @return 1 if error exists, 0 otherwise
 */
extern uint8_t encoder_mock_has_error(encoder_mock_state_t* encoder);

/**
 * @brief Reset position to zero
 *
 * @param encoder Pointer to encoder state
 */
extern void encoder_mock_reset_position(encoder_mock_state_t* encoder);

/**
 * @brief Get encoder status summary for debugging
 *
 * @param encoder Pointer to encoder state
 * @param buffer Output buffer for status string
 * @param buffer_size Size of output buffer
 */
extern void encoder_mock_get_status(encoder_mock_state_t* encoder, char* buffer, size_t buffer_size);

#endif // ENCODER_MOCK_H

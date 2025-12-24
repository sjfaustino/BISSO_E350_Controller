/**
 * @file test/mocks/vfd_mock.h
 * @brief Mock implementation of Altivar 31 VFD for testing
 *
 * This mock simulates the behavior of an Altivar 31 VFD that would normally
 * communicate via Modbus. Used for Motion Control and Configuration tests.
 */

#ifndef VFD_MOCK_H
#define VFD_MOCK_H

#include <cstdint>
#include <cstring>

/**
 * @brief Mock Altivar 31 VFD state
 * Represents the VFD as it would appear during actual operation
 */
typedef struct {
    // Output state
    uint16_t frequency_hz;          // Current output frequency (0-105 Hz)
    float motor_current_amps;       // Current motor draw
    float motor_temperature_c;      // Motor temperature

    // Configuration state
    uint16_t hsp;                   // High Speed Preset (default: 105 Hz)
    uint16_t lsp;                   // Low Speed Preset (default: 1 Hz)
    uint8_t acc_decel_time_10s;    // Acceleration time in 0.1s units (default: 6 = 0.6s)
    uint8_t dec_decel_time_10s;    // Deceleration time in 0.1s units (default: 4 = 0.4s)

    // Status flags
    uint8_t is_running;             // VFD running (1) or stopped (0)
    uint8_t has_fault;              // Fault flag
    uint8_t fault_code;             // Fault code if fault is present

    // Simulated motor physics
    float acceleration_hz_per_ms;   // How fast frequency ramps up
    float deceleration_hz_per_ms;   // How fast frequency ramps down

    // Target frequency for ramping
    uint16_t target_frequency_hz;
} vfd_mock_state_t;

/**
 * @brief Initialize VFD mock to default state (matching real Altivar 31 settings)
 *
 * Default configuration:
 * - HSP = 105 Hz (maximum)
 * - LSP = 1 Hz (creep limit)
 * - ACC = 0.6s (acceleration ramp)
 * - DEC = 0.4s (deceleration ramp)
 * - Frequency = 0 Hz (idle)
 *
 * @return Initialized VFD mock state
 */
extern vfd_mock_state_t vfd_mock_init(void);

/**
 * @brief Set target frequency and simulate ramping
 * Models the behavior of the VFD accelerating/decelerating to the target frequency
 *
 * @param vfd Pointer to VFD state
 * @param target_hz Target frequency (will be clamped to LSP-HSP range)
 */
extern void vfd_mock_set_frequency(vfd_mock_state_t* vfd, uint16_t target_hz);

/**
 * @brief Simulate time passing and update VFD ramp state
 * Call this to advance the VFD through its acceleration/deceleration phase
 *
 * @param vfd Pointer to VFD state
 * @param time_ms Time to advance in milliseconds
 */
extern void vfd_mock_advance_time(vfd_mock_state_t* vfd, uint32_t time_ms);

/**
 * @brief Simulate motor current draw based on frequency
 * Models realistic motor behavior (current increases with frequency)
 *
 * @param vfd Pointer to VFD state
 * @return Current motor current in amps (0.0-10.0A typical)
 */
extern float vfd_mock_get_motor_current(vfd_mock_state_t* vfd);

/**
 * @brief Simulate motor thermal behavior
 * Temperature rises with current, falls with time at idle
 *
 * @param vfd Pointer to VFD state
 * @param time_ms Time step for thermal simulation
 */
extern void vfd_mock_update_temperature(vfd_mock_state_t* vfd, uint32_t time_ms);

/**
 * @brief Inject a VFD fault for testing fault handling
 *
 * @param vfd Pointer to VFD state
 * @param fault_code Fault code to set (see Altivar 31 manual)
 */
extern void vfd_mock_inject_fault(vfd_mock_state_t* vfd, uint8_t fault_code);

/**
 * @brief Clear any fault condition
 *
 * @param vfd Pointer to VFD state
 */
extern void vfd_mock_clear_fault(vfd_mock_state_t* vfd);

/**
 * @brief Check if VFD is at target frequency (within tolerance)
 *
 * @param vfd Pointer to VFD state
 * @param tolerance_hz Acceptable frequency variation
 * @return 1 if at target (within tolerance), 0 otherwise
 */
extern uint8_t vfd_mock_is_at_frequency(vfd_mock_state_t* vfd, uint16_t tolerance_hz);

/**
 * @brief Get VFD status summary for debugging
 * Useful for test diagnostics
 *
 * @param vfd Pointer to VFD state
 * @param buffer Output buffer for status string
 * @param buffer_size Size of output buffer
 */
extern void vfd_mock_get_status(vfd_mock_state_t* vfd, char* buffer, size_t buffer_size);

#endif // VFD_MOCK_H

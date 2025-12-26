/**
 * @file altivar31_modbus.h
 * @brief Altivar 31 VFD Modbus RTU Driver (PHASE 5.5)
 * @project BISSO E350 Controller
 * @details Modbus RTU interface for Schneider Altivar 31 VFD
 *          Provides asynchronous and synchronous queries for motor current,
 *          frequency, status, faults, and thermal state.
 *
 * Register Addresses (Verified from ATV312 Programming Manual BBV51701):
 * - 3202: Output frequency (rFr, 0.1 Hz units)
 * - 3204: Motor current (LCr, 0.1 A units)
 * - 3201: Status word (ETA, bit flags for running/ready/fault)
 * - 8606: Fault code (ERRD, 0 = no fault)
 * - 3209: Thermal state (tHd, 1% units, 100% = nominal)
 */

#ifndef ALTIVAR31_MODBUS_H
#define ALTIVAR31_MODBUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MODBUS REGISTER ADDRESSES (Decimal)
// ============================================================================
// Verified against Altivar 31/312 Programming Manual BBV51701

#define ALTIVAR31_REG_OUTPUT_FREQ       3202    // rFr: Output frequency (0.1 Hz units)
#define ALTIVAR31_REG_DRIVE_CURRENT     3204    // LCr: Motor current (0.1 A units)
#define ALTIVAR31_REG_DRIVE_STATUS      3201    // ETA: Status word (bit flags)
#define ALTIVAR31_REG_FAULT_CODE        8606    // ERRD: Fault code
#define ALTIVAR31_REG_THERMAL_STATE     3209    // tHd: Drive heatsink thermal state (1% units)

// Drive status values
#define ALTIVAR31_STATUS_IDLE           0
#define ALTIVAR31_STATUS_RUNNING        1
#define ALTIVAR31_STATUS_FAULT          2
#define ALTIVAR31_STATUS_OVERHEAT       3

// ============================================================================
// VFD STATE STRUCTURE
// ============================================================================

typedef struct {
    bool enabled;                       // Device enabled/connected flag
    uint8_t slave_address;              // Modbus slave ID (1-247, typically 1)
    uint32_t baud_rate;                 // Baud rate in bps (19200 typical)

    // Real-time measurements
    int16_t frequency_raw;              // Raw register value (0.1 Hz units)
    float frequency_hz;                 // Output frequency in Hz

    int16_t current_raw;                // Raw register value (0.1 A units)
    float current_amps;                 // Motor current in amperes

    uint16_t status_word;               // Operating status (bit flags)
    uint16_t fault_code;                // Fault code (0 = no fault)
    int16_t thermal_state;              // Thermal state (1% units, 100% = nominal)

    // Statistics
    uint32_t last_read_time_ms;         // Timestamp of last successful read
    uint32_t last_error_time_ms;        // Timestamp of last error
    uint32_t read_count;                // Successful reads
    uint32_t error_count;               // Read errors
    uint32_t consecutive_errors;        // Consecutive communication failures

} altivar31_state_t;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize Altivar 31 Modbus driver
 * @param slave_address Modbus slave ID (typically 1)
 * @param baud_rate Baud rate (9600 typical, must match shared RS485 bus)
 * @return true if successful
 */
bool altivar31ModbusInit(uint8_t slave_address, uint32_t baud_rate);

// ============================================================================
// ASYNCHRONOUS QUERIES (Non-blocking, 2-phase pattern)
// ============================================================================

/**
 * @brief Initiate asynchronous read of motor current
 * @details Call this first, then call altivar31ModbusReceiveResponse() to get result
 * @return true if request sent, false on error
 */
bool altivar31ModbusReadCurrent(void);

/**
 * @brief Initiate asynchronous read of output frequency
 * @return true if request sent, false on error
 */
bool altivar31ModbusReadFrequency(void);

/**
 * @brief Initiate asynchronous read of drive status word
 * @return true if request sent, false on error
 */
bool altivar31ModbusReadStatus(void);

/**
 * @brief Initiate asynchronous read of fault code
 * @return true if request sent, false on error
 */
bool altivar31ModbusReadFaultCode(void);

/**
 * @brief Initiate asynchronous read of thermal state
 * @return true if request sent, false on error
 */
bool altivar31ModbusReadThermalState(void);

/**
 * @brief Receive response from asynchronous Modbus query
 * @details Call after sending a read request; updates internal state on success
 * @return true if response received and parsed, false if still waiting or error
 */
bool altivar31ModbusReceiveResponse(void);

// ============================================================================
// DATA ACCESSORS (Cached, non-blocking)
// ============================================================================

/**
 * @brief Get motor current in amperes (most recent measurement)
 * @return Motor current in amps
 */
float altivar31GetCurrentAmps(void);

/**
 * @brief Get motor current raw register value
 * @return Raw value (0.1 A per unit)
 */
int16_t altivar31GetCurrentRaw(void);

/**
 * @brief Get output frequency in Hz (most recent measurement)
 * @return Frequency in Hz
 */
float altivar31GetFrequencyHz(void);

/**
 * @brief Get output frequency raw register value
 * @return Raw value (0.1 Hz per unit)
 */
int16_t altivar31GetFrequencyRaw(void);

/**
 * @brief Get drive status word
 * @return Status word (bit flags)
 */
uint16_t altivar31GetStatusWord(void);

/**
 * @brief Get fault code (0 = no fault)
 * @return Fault code from VFD
 */
uint16_t altivar31GetFaultCode(void);

/**
 * @brief Get thermal state
 * @return Percentage (100% = nominal, >118% triggers thermal fault)
 */
int16_t altivar31GetThermalState(void);

/**
 * @brief Check if VFD is in fault state
 * @return true if fault code != 0
 */
bool altivar31IsFaulted(void);

/**
 * @brief Check if motor is running
 * @return true if running bit (bit 3) is set in status word
 */
bool altivar31IsRunning(void);

/**
 * @brief Get complete VFD state snapshot
 * @return Pointer to current state structure
 */
const altivar31_state_t* altivar31GetState(void);

// ============================================================================
// MOTION VALIDATION (PHASE 5.5)
// ============================================================================

/**
 * @brief Check if VFD is running (output frequency > 0)
 * @return true if motor is running, false if idle/stopped
 */
bool altivar31IsMotorRunning(void);

/**
 * @brief Detect frequency loss during motion (potential stall)
 * @details Returns true if frequency suddenly drops to near-zero during active motion
 * @param previous_freq_hz Last known frequency in Hz
 * @return true if frequency loss detected (freq dropped >80% in one cycle)
 */
bool altivar31DetectFrequencyLoss(float previous_freq_hz);

// ============================================================================
// ERROR HANDLING & DIAGNOSTICS
// ============================================================================

/**
 * @brief Reset error counters
 */
void altivar31ResetErrorCounters(void);

/**
 * @brief Print VFD diagnostics to serial console
 */
void altivar31PrintDiagnostics(void);

#ifdef __cplusplus
}
#endif

#endif // ALTIVAR31_MODBUS_H

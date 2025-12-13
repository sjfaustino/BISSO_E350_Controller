/**
 * @file altivar31_modbus.h
 * @brief Altivar 31 VFD Modbus RTU Driver (PHASE 5.5)
 * @project BISSO E350 Controller
 * @details Modbus RTU interface for Schneider Altivar 31 VFD
 *          Queries motor current, frequency, status, and temperature
 *
 * Standard Altivar 31 Modbus Registers (verify against your VFD manual):
 * - 0200: Output frequency (0.01 Hz units, INT16)
 * - 0203: Drive current (0.1 A units, INT16)
 * - 0205: Drive status (0=idle, 1=running, 2=fault, etc.)
 * - 0204: Fault code (INT16)
 * - 0210: Internal heatsink temperature (°C, INT16, if available)
 *
 * NOTE: Register addresses may vary by Altivar 31 variant/firmware version.
 *       Verify against your specific VFD manual before deployment.
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
// Verify these against your specific Altivar 31 manual

#define ALTIVAR31_REG_OUTPUT_FREQ       0x0200   // Output frequency (0.01 Hz units)
#define ALTIVAR31_REG_FAULT_CODE        0x0204   // Fault code
#define ALTIVAR31_REG_DRIVE_STATUS      0x0205   // Drive operating status
#define ALTIVAR31_REG_DRIVE_CURRENT     0x0203   // Motor current (0.1 A units)
#define ALTIVAR31_REG_TEMPERATURE       0x0210   // Heatsink temperature (°C, if available)

// Drive status values
#define ALTIVAR31_STATUS_IDLE           0
#define ALTIVAR31_STATUS_RUNNING        1
#define ALTIVAR31_STATUS_FAULT          2
#define ALTIVAR31_STATUS_OVERHEAT       3

// ============================================================================
// VFD STATE STRUCTURE
// ============================================================================

typedef struct {
    uint8_t slave_address;              // Modbus slave ID (1-247, typically 1)
    uint32_t baud_rate;                 // Baud rate in bps (9600 typical)

    // Real-time measurements
    uint16_t output_frequency_raw;      // Raw register value (0.01 Hz units)
    float output_frequency_hz;          // Output frequency in Hz

    uint16_t drive_current_raw;         // Raw register value (0.1 A units)
    float drive_current_amps;           // Motor current in amperes (RMS)

    uint16_t drive_status;              // Operating status
    uint16_t fault_code;                // Fault code (0 = no fault)

    int16_t heatsink_temp_c;            // Heatsink temperature (°C)

    // Statistics
    uint32_t last_read_time_ms;         // Timestamp of last successful read
    uint32_t last_error_time_ms;        // Timestamp of last error
    uint32_t read_count;                // Successful reads
    uint32_t error_count;               // Read errors
    uint32_t consecutive_errors;        // Consecutive communication failures

    // Peak/average tracking
    float current_peak_amps;            // Peak current seen
    float current_average_amps;         // Moving average

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
// SYNCHRONOUS QUERIES (Blocking)
// ============================================================================

/**
 * @brief Query current from Altivar 31 (blocking)
 * @return Motor current in amperes, or -1.0 on error
 */
float altivar31ReadCurrent(void);

/**
 * @brief Query output frequency from Altivar 31 (blocking)
 * @return Output frequency in Hz, or -1.0 on error
 */
float altivar31ReadFrequency(void);

/**
 * @brief Query drive status (blocking)
 * @return Status code (0=idle, 1=running, 2=fault, etc.), or -1 on error
 */
int16_t altivar31ReadStatus(void);

/**
 * @brief Query fault code (blocking)
 * @return Fault code (0 = no fault), or -1 on error
 */
int16_t altivar31ReadFaultCode(void);

/**
 * @brief Query heatsink temperature (blocking)
 * @return Temperature in °C, or -999 on error or if register unavailable
 */
int16_t altivar31ReadTemperature(void);

// ============================================================================
// STATE ACCESS
// ============================================================================

/**
 * @brief Get current VFD state snapshot (cached, non-blocking)
 * @return Pointer to current state structure
 */
const altivar31_state_t* altivar31GetState(void);

/**
 * @brief Get last error message
 * @return Error description or NULL
 */
const char* altivar31GetLastError(void);

#ifdef __cplusplus
}
#endif

#endif // ALTIVAR31_MODBUS_H

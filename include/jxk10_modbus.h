/**
 * @file jxk10_modbus.h
 * @brief JXK-10 Current Transducer Modbus RTU Driver
 * @project BISSO E350 Controller - Phase 5.0
 * @details Modbus RTU interface for JXK-10 Hall effect current sensor (0-50A AC)
 *          on shared RS485 bus with WJ66 encoder multiplexing.
 */

#ifndef JXK10_MODBUS_H
#define JXK10_MODBUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Modbus register addresses (decimal)
#define JXK10_REG_CURRENT        0x0000   // Measured current (INT16, ×100, e.g., 3000 = 30.00A)
#define JXK10_REG_STATUS         0x0001   // Device status (bit 0=overload, bit 1=fault)
#define JXK10_REG_SLAVE_ADDR     0x0002   // Slave address (1-247, default 1)
#define JXK10_REG_BAUD_RATE      0x0003   // Baud rate code (9600=4, default)
#define JXK10_REG_RANGE_CONFIG   0x0004   // Range config (0=0-50A, 1=0-100A, etc.)
#define JXK10_REG_SAVE_CONFIG    0x0005   // Save config (write 1 to save to EEPROM)
#define JXK10_REG_RAW_ADC        0x4000   // Raw ADC value (diagnostic, 12-bit)

// Current sensor status bits
#define JXK10_STATUS_OVERLOAD    (1 << 0)
#define JXK10_STATUS_FAULT       (1 << 1)

// Baud rate codes for register 0x0003
#define JXK10_BAUD_1200          1
#define JXK10_BAUD_2400          2
#define JXK10_BAUD_4800          3
#define JXK10_BAUD_9600          4    // Default
#define JXK10_BAUD_19200         5
#define JXK10_BAUD_38400         6
#define JXK10_BAUD_57600         7
#define JXK10_BAUD_115200        10

// Range configuration codes
#define JXK10_RANGE_0_50A        0
#define JXK10_RANGE_0_100A       1

// JXK-10 device state
typedef struct {
    uint8_t slave_address;              // Modbus slave ID (1-247, default 1)
    uint32_t baud_rate;                 // Baud rate in bps
    int16_t current_raw;                // Raw Modbus register value (×100)
    float current_amps;                 // Calculated current in amperes
    uint16_t device_status;             // Status register value
    uint32_t last_read_time_ms;         // Timestamp of last successful read
    uint32_t last_error_time_ms;        // Timestamp of last error
    uint32_t read_count;                // Statistics: successful reads
    uint32_t error_count;               // Statistics: read errors
    bool is_overload;                   // Overload flag (bit 0 of status)
    bool is_fault;                      // Fault flag (bit 1 of status)
    uint32_t consecutive_errors;        // Count of consecutive communication failures
} jxk10_state_t;

/**
 * @brief Initialize JXK-10 Modbus driver
 * @param slave_address Modbus slave ID (1-247, default 1)
 * @param baud_rate Baud rate in bps (9600 default, must match encoder baud for shared bus)
 * @return true if successful, false on error
 */
bool jxk10ModbusInit(uint8_t slave_address, uint32_t baud_rate);

/**
 * @brief Read current from JXK-10 (non-blocking)
 * @details Initiates Modbus read of current register (0x0000)
 *          Must be followed by jxk10ModbusReceiveResponse() after sufficient delay
 * @return true if request sent successfully, false on error
 */
bool jxk10ModbusReadCurrent(void);

/**
 * @brief Receive Modbus response and parse current value
 * @details Non-blocking reception of Modbus response
 *          Must be called after jxk10ModbusReadCurrent() with delay for response time
 * @return true if response received and parsed, false if no response or error
 */
bool jxk10ModbusReceiveResponse(void);

/**
 * @brief Read device status (overload/fault flags)
 * @return true if status read successful, false on error
 */
bool jxk10ModbusReadStatus(void);

/**
 * @brief Get current measurement in amperes
 * @return Current value in amperes (0.0 to 50.0 for standard 50A model)
 */
float jxk10GetCurrentAmps(void);

/**
 * @brief Get raw Modbus register value (×100)
 * @return Raw value from register (e.g., 3000 = 30.00A)
 */
int16_t jxk10GetCurrentRaw(void);

/**
 * @brief Check if device is reporting overload condition
 * @return true if overload bit is set, false otherwise
 */
bool jxk10IsOverload(void);

/**
 * @brief Check if device is reporting fault condition
 * @return true if fault bit is set, false otherwise
 */
bool jxk10IsFault(void);

/**
 * @brief Change slave address on device
 * @details Writes to register 0x0002 and saves to EEPROM via register 0x0005
 * @param new_address New Modbus slave ID (1-247)
 * @return true if successful, false on error
 */
bool jxk10ModbusSetSlaveAddress(uint8_t new_address);

/**
 * @brief Change baud rate
 * @details Updates internal state and encoder_hal baud rate
 * @param baud_rate New baud rate in bps (must be standard RS485 rate)
 * @return true if successful, false on error
 */
bool jxk10ModbusSetBaudRate(uint32_t baud_rate);

/**
 * @brief Get device state/statistics
 * @return Pointer to jxk10 state structure
 */
const jxk10_state_t* jxk10GetState(void);

/**
 * @brief Reset error counters and flags
 */
void jxk10ResetErrorCounters(void);

/**
 * @brief Print JXK-10 diagnostics to console
 */
void jxk10PrintDiagnostics(void);

#ifdef __cplusplus
}
#endif

#endif // JXK10_MODBUS_H

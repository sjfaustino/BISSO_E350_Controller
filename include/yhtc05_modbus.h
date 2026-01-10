/**
 * @file yhtc05_modbus.h
 * @brief YH-TC05 Tachometer/RPM Sensor Modbus RTU Driver
 * @project BISSO E350 Controller
 * @details Modbus RTU interface for YH-TC05 non-contact RPM sensor.
 *          Used for saw blade motor speed monitoring.
 */

#ifndef YHTC05_MODBUS_H
#define YHTC05_MODBUS_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// DEVICE STATE
// ============================================================================

typedef struct {
    bool enabled;                   // Device enabled/connected flag
    uint8_t slave_address;          // Modbus slave ID (1-247)
    uint32_t baud_rate;             // Baud rate in bps
    
    // Measurements
    uint16_t rpm;                   // Current RPM
    uint32_t pulse_count;           // Total pulse count
    uint16_t status;                // Status register
    
    // Derived values
    bool is_spinning;               // Motor is spinning (RPM > threshold)
    bool is_stalled;                // Motor stalled (was spinning, now stopped)
    
    // Stall detection
    uint16_t stall_threshold_rpm;   // RPM below this = stalled
    uint32_t stall_time_ms;         // How long below threshold
    uint32_t stall_detect_time_ms;  // Time when stall was detected
    
    // Statistics
    uint32_t last_read_time_ms;     // Timestamp of last successful read
    uint32_t last_error_time_ms;    // Timestamp of last error
    uint32_t read_count;            // Successful reads
    uint32_t error_count;           // Read errors
    uint32_t consecutive_errors;    // Consecutive failures
    
    // Peak tracking
    uint16_t peak_rpm;              // Maximum RPM recorded
} yhtc05_state_t;

#ifdef __cplusplus
#include "modbus_driver.h"

class YhTc05Driver : public ModbusDriver {
public:
    YhTc05Driver();
    
    uint16_t getRPM() const;
    uint32_t getPulseCount() const;
    bool isSpinning() const;
    bool isStalled() const;
    uint16_t getPeakRPM() const;
    
    // Configuration
    void setStallThreshold(uint16_t rpm, uint32_t time_ms);
    void resetStallDetection();
    void resetPeakRPM();
    
    const yhtc05_state_t* getState() const;

protected:
    bool poll() override;
    bool onResponse(const uint8_t* data, uint16_t len) override;

private:
    yhtc05_state_t _state;
    uint8_t _tx_buffer[16];
    bool _was_spinning;
    uint32_t _below_threshold_since_ms;
};

extern YhTc05Driver YhTc05;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MODBUS REGISTER ADDRESSES (Typical YH-TC05 protocol)
// ============================================================================

#define YHTC05_REG_RPM              0x0000  // Current RPM (UINT16)
#define YHTC05_REG_COUNT_LOW        0x0001  // Pulse count low word (UINT16)
#define YHTC05_REG_COUNT_HIGH       0x0002  // Pulse count high word (UINT16)
#define YHTC05_REG_STATUS           0x0003  // Status flags
#define YHTC05_REG_PULSES_PER_REV   0x0010  // Pulses per revolution (config)
#define YHTC05_REG_SLAVE_ADDR       0x0011  // Modbus slave address (config)
#define YHTC05_REG_BAUD_RATE        0x0012  // Baud rate code (config)

// Status flags
#define YHTC05_STATUS_VALID         (1 << 0)  // Reading is valid
#define YHTC05_STATUS_MOTION        (1 << 1)  // Motion detected
#define YHTC05_STATUS_ALARM         (1 << 2)  // Speed alarm (over/under)



// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize YH-TC05 driver (does NOT register with RS485 bus)
 * @param slave_address Modbus slave ID (typically 1-3)
 * @param baud_rate Baud rate (must match RS485 bus)
 * @return true if successful
 */
bool yhtc05ModbusInit(uint8_t slave_address, uint32_t baud_rate);

/**
 * @brief Register YH-TC05 with RS-485 device registry
 * @param poll_interval_ms How often to poll (recommended: 200-500ms)
 * @param priority Device priority (0-255, recommended: 100)
 * @return true if registered
 */
bool yhtc05RegisterWithBus(uint16_t poll_interval_ms, uint8_t priority);

/**
 * @brief Unregister from RS-485 bus
 * @return true if unregistered
 */
bool yhtc05UnregisterFromBus(void);

// ============================================================================
// ASYNC POLLING (Called by registry)
// ============================================================================

/**
 * @brief Initiate RPM read (called by RS485 scheduler)
 * @return true if request sent
 */
bool yhtc05ModbusReadRPM(void* ctx);

/**
 * @brief Process response from Modbus read
 * @param data Response buffer
 * @param len Response length
 * @return true if valid response
 */
bool yhtc05ModbusOnResponse(void* ctx, const uint8_t* data, uint16_t len);

// ============================================================================
// DATA ACCESSORS
// ============================================================================

/**
 * @brief Get current RPM reading
 * @return RPM value (0 if not spinning or error)
 */
uint16_t yhtc05GetRPM(void);

/**
 * @brief Get total pulse count
 * @return Cumulative pulse count
 */
uint32_t yhtc05GetPulseCount(void);

/**
 * @brief Check if motor is spinning
 * @return true if RPM > stall threshold
 */
bool yhtc05IsSpinning(void);

/**
 * @brief Check if motor is stalled
 * @details Returns true if motor was spinning and stopped unexpectedly
 * @return true if stalled condition detected
 */
bool yhtc05IsStalled(void);

/**
 * @brief Get peak RPM recorded
 * @return Maximum RPM since last reset
 */
uint16_t yhtc05GetPeakRPM(void);

/**
 * @brief Get device state
 * @return Pointer to state structure
 */
const yhtc05_state_t* yhtc05GetState(void);

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * @brief Set stall detection threshold
 * @param rpm_threshold RPM below this is considered stalled
 * @param time_ms Time in ms below threshold before stall is declared
 */
void yhtc05SetStallThreshold(uint16_t rpm_threshold, uint32_t time_ms);

/**
 * @brief Reset stall detection state
 */
void yhtc05ResetStallDetection(void);

/**
 * @brief Reset peak RPM tracking
 */
void yhtc05ResetPeakRPM(void);

/**
 * @brief Reset error counters
 */
void yhtc05ResetErrorCounters(void);

// ============================================================================
// DIAGNOSTICS
// ============================================================================

/**
 * @brief Print diagnostics to serial console
 */
void yhtc05PrintDiagnostics(void);

#ifdef __cplusplus
}
#endif

#endif // YHTC05_MODBUS_H

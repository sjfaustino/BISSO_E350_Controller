#pragma once

#include "rs485_device_registry.h"
#include <stdint.h>

/**
 * @brief Abstract base class for Modbus drivers
 * @details Wraps rs485_device_registry integration, providing an OOP interface.
 *          Drivers should inherit from this class and implement poll() and onResponse().
 */
class ModbusDriver {
public:
    ModbusDriver(const char* name, rs485_device_type_t type, 
                 uint8_t slave_address, uint16_t poll_interval_ms, uint8_t priority);
    virtual ~ModbusDriver();

    /**
     * @brief Initialize and register with the bus
     * @param baud_rate Bus baud rate (for reference, actual rate set by bus manager)
     * @return true if registered successfully
     */
    virtual bool begin(uint32_t baud_rate);

    // Enable/Disable
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Configuration
    void setSlaveAddress(uint8_t address);
    uint8_t getSlaveAddress() const;
    void setPollInterval(uint16_t interval_ms);
    
    // Status & Diagnostics
    uint32_t getPollCount() const;
    uint32_t getErrorCount() const;
    uint32_t getConsecutiveErrors() const;
    void resetErrorCounters();

    // Timestamps
    uint32_t getLastReadTime() const;
    uint32_t getLastErrorTime() const;

    /**
     * @brief Print standard diagnostics for this device
     */
    virtual void printDiagnostics() const;

    // Registry Access (for advanced use)
    const rs485_device_t* getDeviceDescriptor() const;
    rs485_device_t* getMutableDeviceDescriptor();

protected:
    // Pure virtual methods to be implemented by device drivers
    
    /**
     * @brief Initiate a Modbus poll (send request)
     * @return true if request sent
     */
    virtual bool poll() = 0;

    /**
     * @brief Handle response data
     * @param data Received bytes
     * @param len Length
     * @return true if valid response parsed
     */
    virtual bool onResponse(const uint8_t* data, uint16_t len) = 0;

    /**
     * @brief Sync base class counters into a driver-specific state struct
     * @details Call from getState() to populate common fields (error_count,
     *          consecutive_errors, read_count, timestamps, enabled, slave_address)
     * @tparam T Driver state struct type (must have the 7 standard counter fields)
     */
    template<typename T>
    void syncBaseStats(T& state) const {
        state.error_count = getErrorCount();
        state.consecutive_errors = getConsecutiveErrors();
        state.read_count = getPollCount();
        state.last_read_time_ms = getLastReadTime();
        state.last_error_time_ms = getLastErrorTime();
        state.enabled = isEnabled();
        state.slave_address = getSlaveAddress();
    }

    // Helper to send data (wraps rs485Send)
    bool send(const uint8_t* data, uint8_t len);

    // Internal state
    rs485_device_t _device;
    uint32_t _baud_rate;
    uint32_t _last_read_time_ms;
    uint32_t _last_error_time_ms;

private:
    // Static trampoline functions for C-registry callbacks
    static bool staticPoll(void* ctx);
    static bool staticOnResponse(void* ctx, const uint8_t* data, uint16_t len);
};

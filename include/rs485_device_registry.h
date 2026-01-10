/**
 * @file rs485_device_registry.h
 * @brief RS-485 Device Registration and Scheduling System
 * @project BISSO E350 Controller
 * @details Manages multiple Modbus RTU devices on shared RS-485 bus.
 *          Supports priority-based scheduling and per-device statistics.
 */

#ifndef RS485_DEVICE_REGISTRY_H
#define RS485_DEVICE_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

#define RS485_MAX_DEVICES           8       // Maximum registered devices
#define RS485_DEFAULT_BAUD_RATE     9600    // Default baud rate
#define RS485_INTER_FRAME_DELAY_MS  5       // Delay between device switches

// ============================================================================
// DEVICE TYPES (for identification)
// ============================================================================

typedef enum {
    RS485_DEVICE_TYPE_ENCODER = 0,      // WJ66 encoder
    RS485_DEVICE_TYPE_CURRENT_SENSOR,   // JXK-10 current sensor
    RS485_DEVICE_TYPE_VFD,              // Altivar 31 VFD
    RS485_DEVICE_TYPE_RPM_SENSOR,       // YH-TC05 RPM sensor
    RS485_DEVICE_TYPE_GENERIC           // Unknown/custom device
} rs485_device_type_t;

// ============================================================================
// DEVICE CALLBACK TYPES
// ============================================================================

/**
 * @brief Device poll callback - initiates a Modbus transaction
 * @param ctx User data context (optional)
 * @return true if request sent successfully
 */
typedef bool (*rs485_poll_fn)(void* ctx);

/**
 * @brief Device response callback - parses received data
 * @param ctx User data context (optional)
 * @param data Received data buffer
 * @param len Data length
 * @return true if response parsed successfully
 */
typedef bool (*rs485_response_fn)(void* ctx, const uint8_t* data, uint16_t len);

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

typedef struct {
    const char* name;               // Device name ("JXK-10", "Altivar31", etc.)
    rs485_device_type_t type;       // Device type enum
    uint8_t slave_address;          // Modbus slave address (1-247)
    uint16_t poll_interval_ms;      // How often to poll (50-5000ms)
    uint8_t priority;               // 0=lowest, 255=highest
    bool enabled;                   // Device enabled flag
    
    // Callbacks
    rs485_poll_fn poll;             // Initiate transaction
    rs485_response_fn on_response;  // Process response
    void* user_data;                // User context (passed to callbacks)
    
    // Runtime statistics (managed by registry)
    uint32_t last_poll_time_ms;     // Timestamp of last poll
    uint32_t poll_count;            // Successful polls
    uint32_t error_count;           // Failed polls
    uint32_t consecutive_errors;    // Consecutive failures
    bool pending_response;          // Waiting for response
} rs485_device_t;

// ============================================================================
// REGISTRY STATE
// ============================================================================

typedef struct {
    rs485_device_t* devices[RS485_MAX_DEVICES]; // Registered devices
    uint8_t device_count;                       // Number of registered devices
    uint8_t current_device_index;               // Currently active device
    uint32_t last_switch_time_ms;               // Last device switch timestamp
    uint32_t baud_rate;                         // Current baud rate
    bool bus_busy;                              // Transaction in progress
    uint32_t total_transactions;                // Total transactions
    uint32_t total_errors;                      // Total errors
} rs485_registry_state_t;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize RS-485 device registry
 * @param baud_rate UART baud rate (typically 9600 or 19200)
 * @return true if successful
 */
bool rs485RegistryInit(uint32_t baud_rate);

// ============================================================================
// DEVICE REGISTRATION
// ============================================================================

/**
 * @brief Register a device with the RS-485 bus
 * @param device Pointer to device descriptor (caller must keep valid)
 * @return true if registered successfully
 */
bool rs485RegisterDevice(rs485_device_t* device);

/**
 * @brief Unregister a device from the RS-485 bus
 * @param device Pointer to device to remove
 * @return true if removed successfully
 */
bool rs485UnregisterDevice(rs485_device_t* device);

/**
 * @brief Find a registered device by type
 * @param type Device type to find
 * @return Pointer to device, or NULL if not found
 */
rs485_device_t* rs485FindDevice(rs485_device_type_t type);

/**
 * @brief Find a registered device by slave address
 * @param slave_address Modbus slave address
 * @return Pointer to device, or NULL if not found
 */
rs485_device_t* rs485FindDeviceByAddress(uint8_t slave_address);

// ============================================================================
// BUS OPERATIONS
// ============================================================================

/**
 * @brief Update RS-485 bus - call frequently from main loop or task
 * @details Handles device scheduling, polling, and response processing
 * @return true if a transaction was completed
 */
bool rs485Update(void);

/**
 * @brief Central bus handler - performs both update and response processing
 * @details Call this from a dedicated higher-frequency task (e.g. Encoder task)
 */
void rs485HandleBus(void);

// ============================================================================
// BUS I/O API (Abstraction for registered devices)
// ============================================================================

/**
 * @brief Send raw data to the RS-485 bus
 * @param data Data buffer
 * @param len Length to send
 * @return true if successful
 */
bool rs485Send(const uint8_t* data, uint8_t len);

/**
 * @brief Check available bytes on the RS-485 bus
 * @return number of bytes available
 */
int rs485Available(void);

/**
 * @brief Receive data from the RS-485 bus
 * @param data Buffer to fill
 * @param len Output: bytes received
 * @return true if any data received
 */
bool rs485Receive(uint8_t* data, uint8_t* len);

/**
 * @brief Clear the RX buffer
 */
void rs485ClearBuffer(void);

/**
 * @brief Process received response for current device
 * @param data Received data buffer
 * @param len Data length
 * @return true if response was valid
 */
bool rs485ProcessResponse(const uint8_t* data, uint16_t len);

/**
 * @brief Check if bus is available for new transaction
 * @return true if bus is idle
 */
bool rs485IsBusAvailable(void);

/**
 * @brief Request immediate poll of specific device (bypasses scheduler)
 * @param device Device to poll
 * @return true if request accepted
 */
bool rs485RequestImmediatePoll(rs485_device_t* device);

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * @brief Set baud rate (all devices must support same baud)
 * @param baud_rate New baud rate
 * @return true if successful
 */
bool rs485SetBaudRate(uint32_t baud_rate);

/**
 * @brief Get current baud rate
 * @return Current baud rate in bps
 */
uint32_t rs485GetBaudRate(void);

/**
 * @brief Enable/disable a device
 * @param device Device to modify
 * @param enabled New enabled state
 */
void rs485SetDeviceEnabled(rs485_device_t* device, bool enabled);

// ============================================================================
// DIAGNOSTICS
// ============================================================================

/**
 * @brief Get registry state
 * @return Pointer to registry state
 */
const rs485_registry_state_t* rs485GetState(void);

/**
 * @brief Get all registered devices
 * @param count Output: number of devices
 * @return Array of device pointers
 */
rs485_device_t** rs485GetDevices(uint8_t* count);

/**
 * @brief Reset error counters for all devices
 */
void rs485ResetErrorCounters(void);

/**
 * @brief Print diagnostics for all registered devices
 */
void rs485PrintDiagnostics(void);

#ifdef __cplusplus
}
#endif

#endif // RS485_DEVICE_REGISTRY_H

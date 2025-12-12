/**
 * @file encoder_hal.h
 * @brief Encoder Hardware Abstraction Layer (HAL)
 * @details Allows flexible switching between RS232 and RS485 encoder interfaces
 *          without changing the encoder driver logic
 * @project Gemini v4.0.0 - WJ66 Encoder Abstraction
 */

#ifndef ENCODER_HAL_H
#define ENCODER_HAL_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// ENCODER INTERFACE TYPES
// ============================================================================

typedef enum {
    ENCODER_INTERFACE_RS232_HT = 0,     // RS232 on HT1/HT2 (GPIO14/33) - Standard
    ENCODER_INTERFACE_RS485_RXD2 = 1,   // RS485 on RXD2/TXD2 (GPIO17/18) - Alternative
    ENCODER_INTERFACE_UART2_GPIO = 2,   // UART2 on custom GPIO pins (future)
    ENCODER_INTERFACE_CUSTOM = 255      // Custom configuration (user-defined)
} encoder_interface_t;

// ============================================================================
// ENCODER HAL CONFIGURATION STRUCTURE
// ============================================================================

/**
 * @brief Encoder HAL configuration
 * @details Contains all hardware-specific settings for encoder communication
 */
typedef struct {
    encoder_interface_t interface;      // Which interface to use
    uint32_t baud_rate;                 // Serial baud rate (9600, 19200, etc.)
    uint8_t rx_pin;                     // RX pin for custom configuration
    uint8_t tx_pin;                     // TX pin for custom configuration
    uint32_t read_interval_ms;          // How often to poll encoder
    uint32_t timeout_ms;                // Response timeout
} encoder_hal_config_t;

// ============================================================================
// ENCODER HAL INITIALIZATION
// ============================================================================

/**
 * @brief Initialize encoder HAL with default configuration
 * @param interface Interface type (RS232_HT, RS485_RXD2, etc.)
 * @param baud_rate Baud rate (9600, 19200, etc.) - 0 = auto-detect
 * @return true if initialization successful
 *
 * @details Initializes the selected serial interface with specified baud rate.
 *          If baud_rate is 0, performs auto-detection and saves result.
 */
bool encoderHalInit(encoder_interface_t interface, uint32_t baud_rate);

/**
 * @brief Initialize encoder HAL with custom configuration
 * @param config Custom configuration structure
 * @return true if initialization successful
 *
 * @details Allows full control over encoder HAL settings including custom pins
 */
bool encoderHalInitCustom(const encoder_hal_config_t* config);

/**
 * @brief Get current HAL configuration
 * @return Pointer to current configuration structure
 */
const encoder_hal_config_t* encoderHalGetConfig(void);

// ============================================================================
// ENCODER HAL COMMUNICATION
// ============================================================================

/**
 * @brief Send data to encoder
 * @param data Pointer to data buffer
 * @param len Number of bytes to send
 * @return true if send successful
 *
 * @details Sends raw data to encoder via configured interface.
 *          For ASCII protocol: send "#00\r" to request positions
 */
bool encoderHalSend(const uint8_t* data, uint8_t len);

/**
 * @brief Send string to encoder
 * @param str Null-terminated string to send
 * @return true if send successful
 */
bool encoderHalSendString(const char* str);

/**
 * @brief Receive data from encoder (non-blocking)
 * @param data Pointer to buffer for received data
 * @param len Pointer to length (in: buffer size, out: bytes received)
 * @return true if data received, false if no data available
 *
 * @details Non-blocking receive - returns immediately if no data available.
 *          Typical usage: call repeatedly in a loop to fill buffer
 */
bool encoderHalReceive(uint8_t* data, uint8_t* len);

/**
 * @brief Check if data available without reading
 * @return Number of bytes available to read
 */
int encoderHalAvailable(void);

/**
 * @brief Clear receive buffer
 */
void encoderHalClearBuffer(void);

// ============================================================================
// ENCODER HAL STATUS & CONTROL
// ============================================================================

/**
 * @brief Get number of bytes waiting in serial buffer
 * @return Bytes available (0-64)
 */
int encoderHalRxAvailable(void);

/**
 * @brief Get transmission queue space
 * @return Bytes of free space in TX buffer
 */
int encoderHalTxSpace(void);

/**
 * @brief Flush TX buffer (wait for completion)
 * @return true if successful
 */
bool encoderHalFlush(void);

/**
 * @brief Shutdown encoder HAL and free resources
 */
void encoderHalEnd(void);

// ============================================================================
// ENCODER HAL INTERFACE NAMING & INFO
// ============================================================================

/**
 * @brief Get friendly name for interface type
 * @param interface Interface type
 * @return String describing the interface (e.g., "RS232-HT1/HT2")
 */
const char* encoderHalGetInterfaceName(encoder_interface_t interface);

/**
 * @brief Get brief description of interface
 * @param interface Interface type
 * @return String with technical details (e.g., "GPIO14/33, RS232 3.3V")
 */
const char* encoderHalGetInterfaceDescription(encoder_interface_t interface);

/**
 * @brief Switch encoder interface at runtime
 * @param interface New interface to switch to
 * @param baud_rate Baud rate for new interface (0 = keep current)
 * @return true if switch successful
 *
 * @details Can switch between interfaces without reboot.
 *          Useful for hot-swapping hardware variants.
 */
bool encoderHalSwitchInterface(encoder_interface_t interface, uint32_t baud_rate);

// ============================================================================
// DIAGNOSTIC & DEBUG FUNCTIONS
// ============================================================================

/**
 * @brief Get last error code from HAL
 * @return Error code or 0 if no error
 */
uint32_t encoderHalGetLastError(void);

/**
 * @brief Print HAL status to serial console
 */
void encoderHalPrintStatus(void);

#endif // ENCODER_HAL_H

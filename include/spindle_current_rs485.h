/**
 * @file spindle_current_rs485.h
 * @brief RS485 Multiplexer for shared encoder/spindle bus
 * @project BISSO E350 Controller - Phase 5.0
 * @details Manages shared RS485 interface between WJ66 encoder and JXK-10 spindle current sensor
 *          with proper timing and device switching to prevent bus collisions.
 */

#ifndef SPINDLE_CURRENT_RS485_H
#define SPINDLE_CURRENT_RS485_H

#include <stdint.h>
#include <stdbool.h>
#include "encoder_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// RS485 device types on shared bus
typedef enum {
    RS485_DEVICE_ENCODER = 0,    // WJ66 encoder (Modbus RTU)
    RS485_DEVICE_SPINDLE = 1,    // JXK-10 current sensor (Modbus RTU)
} rs485_device_t;

// RS485 multiplexer state
typedef struct {
    rs485_device_t current_device;          // Currently selected device
    rs485_device_t pending_device;          // Device waiting to be selected
    uint32_t last_switch_time_ms;           // Timestamp of last device switch
    uint32_t inter_frame_delay_ms;          // Delay between device switches (default 10ms)
    bool need_switch;                       // Flag indicating device switch is pending
    uint32_t tx_count;                      // Statistics: transmissions
    uint32_t rx_count;                      // Statistics: receptions
    uint32_t error_count;                   // Statistics: errors/timeouts
} rs485_mux_state_t;

/**
 * @brief Initialize RS485 multiplexer
 * @details Sets up shared RS485 bus management with default 10ms inter-frame delay
 * @return true if successful, false on error
 */
bool rs485MuxInit(void);

/**
 * @brief Request device switch on RS485 bus
 * @details Schedules a device switch with proper timing delay
 * @param device Target device to switch to
 * @return true if switch scheduled, false if already on that device or error
 */
bool rs485MuxSwitchDevice(rs485_device_t device);

/**
 * @brief Check if device switch is allowed (delay expired)
 * @details Non-blocking check for inter-frame delay
 * @return true if switch is allowed, false if still waiting
 */
bool rs485MuxCanSwitch(void);

/**
 * @brief Get currently selected device
 * @return Current active device on RS485 bus
 */
rs485_device_t rs485MuxGetCurrentDevice(void);

/**
 * @brief Process device switching
 * @details Call periodically to manage RS485 device switching
 *          Must be called frequently (e.g., from main loop or task)
 * @return true if switch completed, false if waiting for delay
 */
bool rs485MuxUpdate(void);

/**
 * @brief Set inter-frame delay for RS485 switching
 * @param delay_ms Delay in milliseconds (typical: 10-50ms)
 * @return true if successful
 */
bool rs485MuxSetInterFrameDelay(uint32_t delay_ms);

/**
 * @brief Get current inter-frame delay setting
 * @return Delay in milliseconds
 */
uint32_t rs485MuxGetInterFrameDelay(void);

/**
 * @brief Get multiplexer statistics
 * @return Pointer to mux state (statistics)
 */
const rs485_mux_state_t* rs485MuxGetState(void);

/**
 * @brief Print RS485 multiplexer diagnostics
 */
void rs485MuxPrintDiagnostics(void);

#ifdef __cplusplus
}
#endif

#endif // SPINDLE_CURRENT_RS485_H

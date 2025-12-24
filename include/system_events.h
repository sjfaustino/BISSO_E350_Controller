/**
 * @file system_events.h
 * @brief Centralized FreeRTOS Event Group System for BISSO E350
 * @project BISSO E350 Controller
 * @details Provides event-driven architecture to replace polling in tasks
 *
 * Event groups allow tasks to wait for multiple events efficiently instead of
 * polling, reducing CPU usage and improving responsiveness.
 *
 * Architecture:
 * - Safety events: Emergency stop, alarms, button presses
 * - Motion events: State changes, command completion, errors
 * - System events: Configuration changes, errors, warnings
 */

#ifndef SYSTEM_EVENTS_H
#define SYSTEM_EVENTS_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SAFETY EVENT BITS (Event Group 1)
// ============================================================================

#define EVENT_SAFETY_ESTOP_PRESSED      (1 << 0)  // Physical E-STOP button pressed
#define EVENT_SAFETY_ESTOP_RELEASED     (1 << 1)  // Physical E-STOP button released
#define EVENT_SAFETY_PAUSE_PRESSED      (1 << 2)  // Physical PAUSE button pressed
#define EVENT_SAFETY_RESUME_PRESSED     (1 << 3)  // Physical RESUME button pressed
#define EVENT_SAFETY_ALARM_RAISED       (1 << 4)  // Safety alarm triggered
#define EVENT_SAFETY_ALARM_CLEARED      (1 << 5)  // Safety alarm cleared
#define EVENT_SAFETY_SOFT_LIMIT_HIT     (1 << 6)  // Soft limit violation
#define EVENT_SAFETY_ENCODER_DEVIATION  (1 << 7)  // Encoder deviation detected

#define EVENT_SAFETY_ALL_BITS           0xFF      // Mask for all safety events

// ============================================================================
// MOTION EVENT BITS (Event Group 2)
// ============================================================================

#define EVENT_MOTION_IDLE               (1 << 0)  // Motion entered IDLE state
#define EVENT_MOTION_STARTED            (1 << 1)  // Motion started (any axis)
#define EVENT_MOTION_COMPLETED          (1 << 2)  // Motion completed successfully
#define EVENT_MOTION_STOPPED            (1 << 3)  // Motion stopped (commanded)
#define EVENT_MOTION_ERROR              (1 << 4)  // Motion error occurred
#define EVENT_MOTION_HOMING_START       (1 << 5)  // Homing sequence started
#define EVENT_MOTION_HOMING_COMPLETE    (1 << 6)  // Homing sequence completed
#define EVENT_MOTION_BUFFER_READY       (1 << 7)  // Motion buffer has space
#define EVENT_MOTION_STATE_CHANGE       (1 << 8)  // Any state change occurred

#define EVENT_MOTION_ALL_BITS           0x1FF     // Mask for all motion events

// ============================================================================
// SYSTEM EVENT BITS (Event Group 3)
// ============================================================================

#define EVENT_SYSTEM_CONFIG_CHANGED     (1 << 0)  // Configuration updated
#define EVENT_SYSTEM_I2C_ERROR          (1 << 1)  // I2C communication error
#define EVENT_SYSTEM_MODBUS_ERROR       (1 << 2)  // Modbus communication error
#define EVENT_SYSTEM_NETWORK_CONNECTED  (1 << 3)  // Network connection established
#define EVENT_SYSTEM_NETWORK_LOST       (1 << 4)  // Network connection lost
#define EVENT_SYSTEM_LOW_MEMORY         (1 << 5)  // Low memory warning
#define EVENT_SYSTEM_WATCHDOG_ALERT     (1 << 6)  // Watchdog timeout warning
#define EVENT_SYSTEM_OTA_REQUESTED      (1 << 7)  // OTA update requested

#define EVENT_SYSTEM_ALL_BITS           0xFF      // Mask for all system events

// ============================================================================
// EVENT GROUP HANDLES
// ============================================================================

/**
 * @brief Get handle to safety event group
 * @return Handle to safety events, or NULL if not initialized
 */
EventGroupHandle_t systemEventsGetSafety(void);

/**
 * @brief Get handle to motion event group
 * @return Handle to motion events, or NULL if not initialized
 */
EventGroupHandle_t systemEventsGetMotion(void);

/**
 * @brief Get handle to system event group
 * @return Handle to system events, or NULL if not initialized
 */
EventGroupHandle_t systemEventsGetSystem(void);

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize all system event groups
 * @return true if successful, false on failure
 */
bool systemEventsInit(void);

/**
 * @brief Clean up and delete all event groups
 */
void systemEventsCleanup(void);

// ============================================================================
// SAFETY EVENT FUNCTIONS
// ============================================================================

/**
 * @brief Signal safety event
 * @param event_bits Event bits to set (use EVENT_SAFETY_* defines)
 */
void systemEventsSafetySet(EventBits_t event_bits);

/**
 * @brief Clear safety event
 * @param event_bits Event bits to clear (use EVENT_SAFETY_* defines)
 */
void systemEventsSafetyClear(EventBits_t event_bits);

/**
 * @brief Wait for safety event(s)
 * @param event_bits Events to wait for
 * @param clear_on_exit Clear bits after waking up
 * @param wait_all Wait for all bits (true) or any bit (false)
 * @param ticks_to_wait Maximum ticks to wait (portMAX_DELAY for infinite)
 * @return Event bits that were set
 */
EventBits_t systemEventsSafetyWait(EventBits_t event_bits, bool clear_on_exit,
                                    bool wait_all, TickType_t ticks_to_wait);

// ============================================================================
// MOTION EVENT FUNCTIONS
// ============================================================================

/**
 * @brief Signal motion event
 * @param event_bits Event bits to set (use EVENT_MOTION_* defines)
 */
void systemEventsMotionSet(EventBits_t event_bits);

/**
 * @brief Clear motion event
 * @param event_bits Event bits to clear (use EVENT_MOTION_* defines)
 */
void systemEventsMotionClear(EventBits_t event_bits);

/**
 * @brief Wait for motion event(s)
 * @param event_bits Events to wait for
 * @param clear_on_exit Clear bits after waking up
 * @param wait_all Wait for all bits (true) or any bit (false)
 * @param ticks_to_wait Maximum ticks to wait (portMAX_DELAY for infinite)
 * @return Event bits that were set
 */
EventBits_t systemEventsMotionWait(EventBits_t event_bits, bool clear_on_exit,
                                    bool wait_all, TickType_t ticks_to_wait);

// ============================================================================
// SYSTEM EVENT FUNCTIONS
// ============================================================================

/**
 * @brief Signal system event
 * @param event_bits Event bits to set (use EVENT_SYSTEM_* defines)
 */
void systemEventsSystemSet(EventBits_t event_bits);

/**
 * @brief Clear system event
 * @param event_bits Event bits to clear (use EVENT_SYSTEM_* defines)
 */
void systemEventsSystemClear(EventBits_t event_bits);

/**
 * @brief Wait for system event(s)
 * @param event_bits Events to wait for
 * @param clear_on_exit Clear bits after waking up
 * @param wait_all Wait for all bits (true) or any bit (false)
 * @param ticks_to_wait Maximum ticks to wait (portMAX_DELAY for infinite)
 * @return Event bits that were set
 */
EventBits_t systemEventsSystemWait(EventBits_t event_bits, bool clear_on_exit,
                                    bool wait_all, TickType_t ticks_to_wait);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Get current safety event status (non-blocking)
 * @return Current safety event bits
 */
EventBits_t systemEventsGetSafetyStatus(void);

/**
 * @brief Get current motion event status (non-blocking)
 * @return Current motion event bits
 */
EventBits_t systemEventsGetMotionStatus(void);

/**
 * @brief Get current system event status (non-blocking)
 * @return Current system event bits
 */
EventBits_t systemEventsGetSystemStatus(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_EVENTS_H

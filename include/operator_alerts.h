/**
 * @file operator_alerts.h
 * @brief Audible Buzzer and Tower Light Control
 * @project BISSO E350 Controller
 */

#ifndef OPERATOR_ALERTS_H
#define OPERATOR_ALERTS_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// SYSTEM STATE (for status light)
// =============================================================================

typedef enum {
    SYSTEM_STATE_IDLE = 0,      // Green solid
    SYSTEM_STATE_RUNNING,       // Yellow solid
    SYSTEM_STATE_PAUSED,        // Green solid + Yellow blink
    SYSTEM_STATE_ESTOP,         // Red solid
    SYSTEM_STATE_FAULT,         // Red blink
    SYSTEM_STATE_HOMING         // Yellow blink
} system_display_state_t;

// =============================================================================
// BUZZER PATTERNS
// =============================================================================

typedef enum {
    BUZZER_OFF = 0,
    BUZZER_BEEP_SHORT,          // Single short beep (100ms)
    BUZZER_BEEP_LONG,           // Single long beep (500ms)
    BUZZER_BEEP_DOUBLE,         // Two short beeps
    BUZZER_BEEP_TRIPLE,         // Three short beeps (warning)
    BUZZER_ALARM_CONTINUOUS,    // Continuous (fault/estop)
    BUZZER_JOB_COMPLETE         // Distinctive pattern for job done
} buzzer_pattern_t;

// =============================================================================
// API - STATUS LIGHT
// =============================================================================

/**
 * @brief Initialize status light system
 * Reads config for enabled state and pin assignments
 */
void statusLightInit(void);

/**
 * @brief Set status light state based on system condition
 * @param state Current system state
 */
void statusLightSetState(system_display_state_t state);

/**
 * @brief Update status light (call from main loop for blink handling)
 * Should be called every 100ms
 */
void statusLightUpdate(void);

/**
 * @brief Test status light outputs
 * Cycles through all colors
 */
void statusLightTest(void);

/**
 * @brief Get current status light state
 */
system_display_state_t statusLightGetState(void);

// =============================================================================
// API - BUZZER
// =============================================================================

/**
 * @brief Initialize buzzer system
 * Reads config for enabled state and pin assignment
 */
void buzzerInit(void);

/**
 * @brief Play a buzzer pattern
 * @param pattern Pattern to play
 */
void buzzerPlay(buzzer_pattern_t pattern);

/**
 * @brief Stop buzzer immediately
 */
void buzzerStop(void);

/**
 * @brief Update buzzer (call from main loop for pattern timing)
 * Should be called every 50ms
 */
void buzzerUpdate(void);

/**
 * @brief Check if buzzer is currently active
 */
bool buzzerIsActive(void);

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

/**
 * @brief Called when job completes successfully
 */
void alertJobComplete(void);

/**
 * @brief Called on fault condition
 */
void alertFault(void);

/**
 * @brief Called on E-Stop
 */
void alertEstop(void);

/**
 * @brief Called when warning occurs
 */
void alertWarning(void);

/**
 * @brief Print alert system status to CLI
 */
void alertsPrintStatus(void);

#endif // OPERATOR_ALERTS_H

/**
 * @file lcd_sleep.cpp
 * @brief LCD Backlight Sleep/Timeout Management Implementation
 */

#include "lcd_sleep.h"
#include "lcd_interface.h"
#include "serial_logger.h"
#include <Arduino.h>

// ============================================================================
// LCD SLEEP STATE
// ============================================================================

static struct {
    bool enabled;                       // Is sleep timeout enabled?
    uint32_t timeout_sec;               // Timeout in seconds (0 = disabled)
    uint32_t timeout_ms;                // Timeout in milliseconds
    uint32_t last_activity_ms;          // Last activity timestamp
    bool is_asleep;                     // Current backlight state
} lcdSleepState = {
    false,      // Sleep disabled initially
    0,          // No timeout
    0,          // No timeout
    0,          // Activity time
    false       // Backlight is on
};

// ============================================================================
// INITIALIZATION
// ============================================================================

void lcdSleepInit() {
    lcdSleepState.enabled = false;
    lcdSleepState.timeout_sec = 0;
    lcdSleepState.timeout_ms = 0;
    lcdSleepState.last_activity_ms = millis();
    lcdSleepState.is_asleep = false;
    logInfo("[LCD-SLEEP] Initialized");
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool lcdSleepSetTimeout(uint32_t timeout_sec) {
    if (timeout_sec == 0) {
        // Disable sleep timeout
        lcdSleepState.enabled = false;
        lcdSleepState.timeout_sec = 0;
        lcdSleepState.timeout_ms = 0;
        lcdSleepReset();  // Wake up if sleeping
        logInfo("[LCD-SLEEP] Disabled (M255 S0)");
        return true;
    }

    // Enable with new timeout
    lcdSleepState.enabled = true;
    lcdSleepState.timeout_sec = timeout_sec;
    lcdSleepState.timeout_ms = timeout_sec * 1000;
    lcdSleepState.last_activity_ms = millis();
    lcdSleepState.is_asleep = false;

    // Turn backlight on when enabling timeout
    lcdInterfaceBacklight(true);

    logInfo("[LCD-SLEEP] Enabled - Timeout: %lu seconds (%lu ms)",
            (unsigned long)timeout_sec, (unsigned long)lcdSleepState.timeout_ms);
    return true;
}

uint32_t lcdSleepGetTimeout() {
    return lcdSleepState.timeout_sec;
}

void lcdSleepReset() {
    // Reset activity timer and wake up display
    lcdSleepState.last_activity_ms = millis();

    if (lcdSleepState.is_asleep) {
        // Wake up the display
        lcdInterfaceBacklight(true);
        lcdSleepState.is_asleep = false;
        logInfo("[LCD-SLEEP] Woken by activity");
    }
}

void lcdSleepUpdate() {
    if (!lcdSleepState.enabled || lcdSleepState.timeout_ms == 0) return;

    uint32_t now = millis();
    uint32_t elapsed = now - lcdSleepState.last_activity_ms;

    // Check if timeout has elapsed
    if (elapsed >= lcdSleepState.timeout_ms && !lcdSleepState.is_asleep) {
        // Put display to sleep
        lcdInterfaceBacklight(false);
        lcdSleepState.is_asleep = true;
        logInfo("[LCD-SLEEP] Timeout elapsed - Display sleeping");
    }
}

void lcdSleepWakeup() {
    if (!lcdSleepState.is_asleep) return;

    lcdInterfaceBacklight(true);
    lcdSleepState.is_asleep = false;
    lcdSleepState.last_activity_ms = millis();
    logInfo("[LCD-SLEEP] Forced wakeup");
}

void lcdSleepSleep() {
    if (lcdSleepState.is_asleep) return;

    lcdInterfaceBacklight(false);
    lcdSleepState.is_asleep = true;
    logInfo("[LCD-SLEEP] Forced sleep");
}

bool lcdSleepIsAsleep() {
    return lcdSleepState.is_asleep;
}

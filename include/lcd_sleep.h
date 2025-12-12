/**
 * @file lcd_sleep.h
 * @brief LCD Backlight Sleep/Timeout Management (M255 G-code support)
 * @details Non-blocking backlight timeout with automatic sleep
 */

#ifndef LCD_SLEEP_H
#define LCD_SLEEP_H

#include <stdint.h>

/**
 * @brief Initialize LCD sleep system
 */
void lcdSleepInit();

/**
 * @brief Set LCD backlight timeout
 * @param timeout_sec Timeout in seconds (0 = never sleep, disable timeout)
 * @return true if successful
 */
bool lcdSleepSetTimeout(uint32_t timeout_sec);

/**
 * @brief Get current backlight timeout
 * @return Timeout in seconds (0 = never sleep)
 */
uint32_t lcdSleepGetTimeout();

/**
 * @brief Reset backlight activity timer (wake up display)
 * Called automatically on user input or activity
 */
void lcdSleepReset();

/**
 * @brief Update LCD sleep state (called from main loop)
 * Non-blocking check: turns off backlight if timeout elapsed
 */
void lcdSleepUpdate();

/**
 * @brief Force backlight on immediately
 */
void lcdSleepWakeup();

/**
 * @brief Force backlight off immediately
 */
void lcdSleepSleep();

/**
 * @brief Check if display is currently in sleep mode
 * @return true if backlight is off, false if on
 */
bool lcdSleepIsAsleep();

#endif // LCD_SLEEP_H

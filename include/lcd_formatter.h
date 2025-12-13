/**
 * @file lcd_formatter.h
 * @brief LCD String Formatter - Background Task (PHASE 5.4)
 * @details Formats LCD strings on Core 0 to reduce Core 1 snprintf overhead
 */

#ifndef LCD_FORMATTER_H
#define LCD_FORMATTER_H

#include <stdint.h>
#include <stddef.h>

// Pre-formatted LCD line buffers (20 chars each for typical 20x4 LCD)
typedef struct {
    char line0[21];  // Axis positions line
    char line1[21];  // Status line
    char line2[21];  // Motion/Alarm/Message line
    char line3[21];  // Detail line
    uint32_t last_update_ms;
} lcd_format_buffer_t;

/**
 * @brief Initialize LCD formatter (called once at startup)
 */
void lcdFormatterInit();

/**
 * @brief Update formatter with latest motion state (called by Core 0 task)
 */
void lcdFormatterUpdate();

/**
 * @brief Get the latest formatted LCD strings (called by Core 1 LCD task)
 */
const lcd_format_buffer_t* lcdFormatterGetBuffer();

#endif // LCD_FORMATTER_H

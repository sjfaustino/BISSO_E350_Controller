#ifndef LCD_MESSAGE_H
#define LCD_MESSAGE_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// PHASE 3.2: LCD MESSAGE SYSTEM (G-CODE M117 SUPPORT)
// ============================================================================

// Maximum length for custom LCD messages (fits on 20-char line)
#define LCD_MESSAGE_MAX_LEN 20

// Message types
typedef enum {
  LCD_MSG_NONE = 0,
  LCD_MSG_CUSTOM = 1,      // Custom message from M117 G-code
  LCD_MSG_MOTION = 2,      // Automatic motion display (default)
} lcd_message_type_t;

typedef struct {
  lcd_message_type_t type;
  char text[LCD_MESSAGE_MAX_LEN + 1];
  uint32_t timestamp_ms;
  uint32_t duration_ms;    // How long to display (0 = until overwritten)
} lcd_message_t;

// ============================================================================
// LCD MESSAGE API
// ============================================================================

// Initialize message system
void lcdMessageInit();

// Set a custom message (typically from G-code M117)
// duration_ms: 0 = display until next message, >0 = display for N milliseconds
void lcdMessageSet(const char* message, uint32_t duration_ms);

// Get current message (called by LCD task)
bool lcdMessageGet(lcd_message_t* out_msg);

// Check if message has expired
bool lcdMessageIsExpired();

// Revert to automatic motion display
void lcdMessageResetToAuto();

#endif

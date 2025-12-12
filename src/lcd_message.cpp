/**
 * @file lcd_message.cpp
 * @brief PHASE 3.2 LCD Message System
 *
 * Manages custom LCD messages from G-code M117 command.
 * Allows temporary display override while still showing motion info.
 */

#include "lcd_message.h"
#include "serial_logger.h"
#include <string.h>

// ============================================================================
// MESSAGE STATE
// ============================================================================

static lcd_message_t current_message = {
  LCD_MSG_NONE,
  "",
  0,
  0
};

static bool message_initialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

void lcdMessageInit() {
  if (message_initialized) return;

  memset(&current_message, 0, sizeof(current_message));
  current_message.type = LCD_MSG_NONE;
  message_initialized = true;

  logInfo("[LCD_MSG] Message system initialized");
}

// ============================================================================
// MESSAGE CONTROL
// ============================================================================

void lcdMessageSet(const char* message, uint32_t duration_ms) {
  if (!message_initialized) lcdMessageInit();
  if (!message) return;

  // Copy message and truncate to LCD width
  strncpy(current_message.text, message, LCD_MESSAGE_MAX_LEN);
  current_message.text[LCD_MESSAGE_MAX_LEN] = '\0';

  current_message.type = LCD_MSG_CUSTOM;
  current_message.timestamp_ms = millis();
  current_message.duration_ms = duration_ms;

  logInfo("[LCD_MSG] Message set: '%s' (duration: %lu ms)", message, (unsigned long)duration_ms);
}

void lcdMessageResetToAuto() {
  if (!message_initialized) return;

  current_message.type = LCD_MSG_NONE;
  memset(current_message.text, 0, sizeof(current_message.text));
  current_message.duration_ms = 0;

  logInfo("[LCD_MSG] Reverted to automatic display");
}

// ============================================================================
// MESSAGE QUERIES
// ============================================================================

bool lcdMessageGet(lcd_message_t* out_msg) {
  if (!message_initialized || !out_msg) return false;

  // Check if message has expired
  if (lcdMessageIsExpired()) {
    lcdMessageResetToAuto();
    return false;
  }

  // Return current message if it's custom
  if (current_message.type == LCD_MSG_CUSTOM) {
    memcpy(out_msg, &current_message, sizeof(lcd_message_t));
    return true;
  }

  return false;
}

bool lcdMessageIsExpired() {
  if (current_message.type != LCD_MSG_CUSTOM) return false;
  if (current_message.duration_ms == 0) return false;  // Never expires

  uint32_t elapsed = millis() - current_message.timestamp_ms;
  return elapsed >= current_message.duration_ms;
}

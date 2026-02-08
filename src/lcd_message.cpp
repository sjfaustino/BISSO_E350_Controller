/**
 * @file lcd_message.cpp
 * @brief LCD Message System
 *
 * Manages custom LCD messages from G-code M117 command.
 * Allows temporary display override while still showing motion info.
 */

#include "lcd_message.h"
#include "serial_logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "string_safety.h"

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
static portMUX_TYPE message_mux = portMUX_INITIALIZER_UNLOCKED;

// Helper to acquire and release lock
#define LOCK_MESSAGE()   portENTER_CRITICAL(&message_mux)
#define UNLOCK_MESSAGE() portEXIT_CRITICAL(&message_mux)

// ============================================================================
// INITIALIZATION
// ============================================================================

void lcdMessageInit() {
  bool initialized_now = false;
  // Use spinlock to ensure initialization is thread-safe
  LOCK_MESSAGE();
  if (!message_initialized) {
    memset(&current_message, 0, sizeof(current_message));
    current_message.type = LCD_MSG_NONE;
    message_initialized = true;
    initialized_now = true;
  }
  UNLOCK_MESSAGE();

  if (initialized_now) {
    logInfo("[LCD_MSG] Message system initialized");
  }
}

// ============================================================================
// MESSAGE CONTROL
// ============================================================================

void lcdMessageSet(const char* message, uint32_t duration_ms) {
  if (!message_initialized) lcdMessageInit();
  if (!message) return;

  LOCK_MESSAGE();
  // Copy message and truncate to LCD width
  SAFE_STRCPY(current_message.text, message, sizeof(current_message.text));

  current_message.type = LCD_MSG_CUSTOM;
  current_message.timestamp_ms = millis();
  current_message.duration_ms = duration_ms;
  UNLOCK_MESSAGE();

  logInfo("[LCD_MSG] Message set: '%s' (duration: %lu ms)", message, (unsigned long)duration_ms);
}

void lcdMessageResetToAuto() {
  if (!message_initialized) return;

  LOCK_MESSAGE();
  current_message.type = LCD_MSG_NONE;
  memset(current_message.text, 0, sizeof(current_message.text));
  current_message.duration_ms = 0;
  UNLOCK_MESSAGE();

  logInfo("[LCD_MSG] Reverted to automatic display");
}

// ============================================================================
// MESSAGE QUERIES
// ============================================================================

bool lcdMessageGet(lcd_message_t* out_msg) {
  if (!message_initialized || !out_msg) return false;

  bool has_msg = false;
  LOCK_MESSAGE();
  
  // Check if message has expired while holding lock
  if (current_message.type == LCD_MSG_CUSTOM) {
    if (current_message.duration_ms > 0) {
      uint32_t elapsed = millis() - current_message.timestamp_ms;
      if (elapsed >= current_message.duration_ms) {
        // Expired - clear it
        current_message.type = LCD_MSG_NONE;
        memset(current_message.text, 0, sizeof(current_message.text));
        current_message.duration_ms = 0;
        
        // Use a separate flag to log outside the lock to avoid deadlock if logging is slow
        has_msg = false;
      } else {
        // Still valid - copy it
        memcpy(out_msg, &current_message, sizeof(lcd_message_t));
        has_msg = true;
      }
    } else {
      // Never expires - copy it
      memcpy(out_msg, &current_message, sizeof(lcd_message_t));
      has_msg = true;
    }
  }
  
  UNLOCK_MESSAGE();

  // Log reversion outside the lock if it just expired
  static bool was_custom = false;
  if (!has_msg && was_custom) {
      logInfo("[LCD_MSG] Reverted to automatic display (expired)");
  }
  was_custom = has_msg;

  return has_msg;
}

bool lcdMessageIsExpired() {
  if (!message_initialized) return true;
  
  LOCK_MESSAGE();
  bool expired = false;
  if (current_message.type != LCD_MSG_CUSTOM) {
      expired = true;
  } else if (current_message.duration_ms > 0) {
      uint32_t elapsed = millis() - current_message.timestamp_ms;
      expired = (elapsed >= current_message.duration_ms);
  }
  UNLOCK_MESSAGE();
  
  return expired;
}

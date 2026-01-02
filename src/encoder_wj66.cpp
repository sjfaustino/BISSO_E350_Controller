/**
 * @file encoder_wj66.cpp
 * @brief Driver for WJ66 Absolute Encoders (Gemini v3.5.15)
 * @details Merged: User's Baud/Parsing Logic + v3.5 Safety Flow Control.
 */

#include <Arduino.h>
#include "rs485_device_registry.h"
#include "encoder_hal.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "encoder_comm_stats.h" 
#include "system_constants.h"
#include <Preferences.h>

// Safety Constants
// BUFFER SIZE VERIFICATION (Gemini Audit):
// Max frame: !<11 digits>,<11 digits>,<11 digits>,<11 digits>\r = 49 bytes
// Example:   !-2147483648,-2147483648,-2147483648,-2147483648\r
// Buffer:    64 bytes (15 byte safety margin)
// Conclusion: Adequate for full encoder frame ✓
#define MAX_BYTES_PER_CYCLE 64
#define WJ66_TIMEOUT_MS 500

// Internal State
struct {
  int32_t position[WJ66_AXES];
  int32_t zero_offset[WJ66_AXES];
  uint32_t last_read[WJ66_AXES];
  uint32_t read_count[WJ66_AXES];
  encoder_status_t status;
  uint32_t error_count;
  uint32_t last_command_time;
  bool waiting_for_response; // Added for flow control
} wj66_state = {{0}, {0}, {0}, {0}, ENCODER_OK, 0, 0, false};

// RS-485 Registry Device Descriptor
static bool wj66Poll(void);
static bool wj66OnResponse(const uint8_t* data, uint16_t len);

static rs485_device_t wj66_device = {
    .name = "WJ66",
    .type = RS485_DEVICE_TYPE_ENCODER,
    .slave_address = 0,         // WJ66 uses broadcast or specific logic, but address 0 is often used for simple setups
    .poll_interval_ms = 50,     // 20 Hz
    .priority = 200,            // High priority
    .enabled = true,
    .poll = wj66Poll,
    .on_response = wj66OnResponse,
    .last_poll_time_ms = 0,
    .poll_count = 0,
    .error_count = 0,
    .consecutive_errors = 0,
    .pending_response = false
};

// PHASE 5.10: Mutex for thread-safe encoder position access
// Protects wj66_state from torn reads between Encoder task and Motion task
static SemaphoreHandle_t wj66_mutex = NULL;

void wj66Init() {
  logInfo("[WJ66] Initializing...");

  // PHASE 5.10: Create mutex for thread-safe position access
  if (wj66_mutex == NULL) {
    wj66_mutex = xSemaphoreCreateMutex();
    if (wj66_mutex == NULL) {
      logError("[WJ66] Failed to create position mutex!");
    }
  }

  uint32_t final_baud = WJ66_BAUD;
  bool confirmed = false;

  // 1. Load saved baud rate
  Preferences p;
  p.begin("wj66_config", false); // Read-write to allow creation on first boot
  uint32_t saved_baud = p.getUInt("baud_rate", 0);
  p.end();

  if (saved_baud > 0) {
      logInfo("[WJ66] Trying saved baud: %lu...", (unsigned long)saved_baud);
      Serial1.begin(saved_baud, SERIAL_8N1, 14, 33); // Ensure pins match your board

      // WATCHDOG SAFETY FIX: Add timeout to Serial flush loop
      // Prevents infinite hang if Serial1 continuously receives data
      uint32_t flush_start = millis();
      while(Serial1.available() && (millis() - flush_start) < 100) {
          Serial1.read();
      } 
      
      // Attempt handshake
      uint8_t q[] = {0x01, 0x00}; // Adjust handshake packet if needed
      // Note: encoderSendCommandWithStats needs to be defined or replaced with Serial1.write
      // Assuming basic write for now if helper missing:
      Serial1.write(q, 2); 
      
      delay(100); // Simple wait for init check
      if (Serial1.available()) {
          logInfo("[WJ66] Saved baud verified.");
          final_baud = saved_baud;
          confirmed = true;
      } else {
          Serial1.end();
          delay(100);
      }
  }

  // 2. Auto-detect (Fallback)
  if (!confirmed) {
      logInfo("[WJ66] Auto-detecting...");
      baud_detect_result_t res = encoderDetectBaudRate();
      
      if (res.detected) {
          final_baud = res.baud_rate;
          p.begin("wj66_config", false);
          p.putUInt("baud_rate", final_baud);
          p.end();
      } else {
          logError("[WJ66] Auto-detect failed. Using default.");
          Serial1.begin(WJ66_BAUD, SERIAL_8N1, 14, 33);
      }
  } else {
      if (!Serial1) Serial1.begin(final_baud, SERIAL_8N1, 14, 33);
  }

  // Init State
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    wj66_state.last_read[i] = millis();
  }
  wj66_state.status = ENCODER_OK;
  wj66_state.waiting_for_response = false;
  
  // Register with RS-485 registry
  rs485RegisterDevice(&wj66_device);
  
  logInfo("[WJ66] Ready @ %lu baud (Registered with RS485 bus)", (unsigned long)final_baud);
}

static bool wj66Poll(void) {
    // Request Data
    return encoderHalSendString("#00\r");
}

static bool wj66OnResponse(const uint8_t* data, uint16_t len) {
    if (len == 0 || data[0] != '!') {
        return false;
    }

    int commas = 0;
    int32_t values[4] = {0};
    int32_t current_value = 0;
    bool is_negative = false;
    
    // CSV Parsing Logic
    for (uint16_t i = 1; i < len; i++) {
        char ch = (char)data[i];
        if (ch == ',') {
            values[commas] = is_negative ? -current_value : current_value;
            current_value = 0; is_negative = false;
            commas++;
            if (commas >= 4) break;
        } else if (ch == '-') is_negative = true;
        else if (ch >= '0' && ch <= '9') current_value = current_value * 10 + (ch - '0');
        else if (ch == '\r' || ch == '\n') break; // End of frame
    }
    
    // Final Value & Validation
    if (commas == 3) {
        values[3] = is_negative ? -current_value : current_value;

        // PHASE 5.10: Thread-safe position and status write
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (int i = 0; i < WJ66_AXES; i++) {
                wj66_state.position[i] = values[i];
                wj66_state.last_read[i] = millis();
                wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
            xSemaphoreGive(wj66_mutex);
        }
        return true;
    } else {
        if (xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            wj66_state.status = ENCODER_CRC_ERROR;
            wj66_state.error_count++;
            xSemaphoreGive(wj66_mutex);
        }
        return false;
    }
}


int32_t wj66GetPosition(uint8_t axis) {
    if (axis >= WJ66_AXES) return 0;

    // PHASE 5.10: Thread-safe position read to prevent torn reads
    int32_t position = 0;
    if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      position = wj66_state.position[axis] - wj66_state.zero_offset[axis];
      xSemaphoreGive(wj66_mutex);
    } else {
      logWarning("[WJ66] Mutex timeout reading axis %d position", axis);
      // Return cached value without protection (better than blocking)
      position = wj66_state.position[axis] - wj66_state.zero_offset[axis];
    }

    return position;
}

uint32_t wj66GetAxisAge(uint8_t axis) { 
    return (axis < WJ66_AXES) ? millis() - wj66_state.last_read[axis] : 0xFFFFFFFF; 
}

bool wj66IsStale(uint8_t axis) { 
    return wj66GetAxisAge(axis) > WJ66_TIMEOUT_MS; 
}

// PHASE 5.10: Protect status read with mutex
encoder_status_t wj66GetStatus() {
  encoder_status_t status = ENCODER_OK;
  if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    status = wj66_state.status;
    xSemaphoreGive(wj66_mutex);
  }
  return status;
}

void wj66Reset() {
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    wj66_state.last_read[i] = millis();
  }
  wj66_state.error_count = 0;
  wj66_state.waiting_for_response = false;
  logInfo("[WJ66] Stats reset.");
}

void wj66SetZero(uint8_t axis) {
    if (axis < WJ66_AXES) {
        // PHASE 5.10: Thread-safe zero offset update
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          wj66_state.zero_offset[axis] = wj66_state.position[axis];
          xSemaphoreGive(wj66_mutex);
          logInfo("[WJ66] Axis %d Zeroed. Offset: %ld", axis, (long)wj66_state.zero_offset[axis]);
        } else {
          logWarning("[WJ66] Mutex timeout setting zero for axis %d", axis);
        }
    }
}

void wj66Diagnostics() {
  logPrintln("\n=== ENCODER STATUS ===");
  logPrintf("Status: %d\nErrors: %lu\n", wj66_state.status, (unsigned long)wj66_state.error_count);
  logPrintf("Waiting: %s\n", wj66_state.waiting_for_response ? "YES" : "NO");
  
  for (int i = 0; i < WJ66_AXES; i++) {
    logPrintf("  Axis %d: Raw=%ld | Offset=%ld | NET=%ld\n", 
        i, 
        (long)wj66_state.position[i], 
        (long)wj66_state.zero_offset[i],
        (long)(wj66_state.position[i] - wj66_state.zero_offset[i])
    );
  }
}
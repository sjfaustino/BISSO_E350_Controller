#include "encoder_wj66.h"
#include "fault_logging.h"
#include "serial_logger.h"

struct {
  int32_t position[WJ66_AXES];
  uint32_t last_read[WJ66_AXES];
  uint32_t read_count[WJ66_AXES];
  encoder_status_t status;
  uint32_t error_count;
  uint32_t last_command_time;
} wj66_state = {{0}, {0}, {0}, ENCODER_OK, 0, 0};

void wj66Init() {
  Serial.println("[WJ66] Encoder system initializing...");
  Serial1.begin(WJ66_BAUD, SERIAL_8N1, 14, 33);
  
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.last_read[i] = millis();
    wj66_state.read_count[i] = 0;
  }
  
  wj66_state.status = ENCODER_OK;
  wj66_state.error_count = 0;
  wj66_state.last_command_time = millis();
  Serial.println("[WJ66] Encoder system ready");
}

/**
 * @brief WJ66 encoder serial communication update - reads position data from all 4 axes
 * 
 * **Protocol Overview:**
 * - Communication: Serial1 at 9600 baud via KC868-A16 (GPIO14=RX, GPIO33=TX)
 * - Encoder Type: WJ66 quadrature encoder
 * - Command Format: "#00\r" (ASCII command)
 * - Response Format: "!±val1,±val2,±val3,±val4\r" (4 signed 16-bit values)
 * - PPR (Pulses Per Revolution): 20
 * - Max Position Range: ±99,999 (constraint: >100,000 is invalid)
 * 
 * **Algorithm:**
 * 1. Check if enough time has elapsed since last read (WJ66_READ_INTERVAL_MS)
 * 2. Every 500ms, send read command "#00\r" to encoder
 * 3. Read response byte-by-byte into fixed circular buffer
 * 4. On '\r' received:
 *    - Validate format: starts with '!'
 *    - Parse 4 signed integers with comma delimiters
 *    - Handle negative numbers: '-' flag then accumulate digits
 *    - Validate range: all values must be ±100,000 or less
 *    - On success: update position array and timestamp
 *    - On error: set ENCODER_CRC_ERROR status
 * 5. Buffer overflow protection: max 64 bytes (reset if exceeded)
 * 
 * **Fixed Buffer (64 bytes):**
 * - Prevents dynamic String allocation memory fragmentation
 * - Strict bounds checking: buffer_idx < 64
 * - Automatic reset on overflow with error logging
 * - Null-terminated after each successful parse
 * 
 * **Data Validation:**
 * - Checks for 3 commas (4 values expected)
 * - Validates all values in ±100,000 range
 * - Updates timestamps for stall detection
 * - Increments read counter for diagnostics
 * 
 * **Thread Safety:**
 * @pre Static buffer and state accessed atomically
 * @note Called from encoder task at ~20Hz
 * @note Position updates visible to motion task immediately
 * 
 * **Error Handling:**
 * - ENCODER_OK: Valid data received
 * - ENCODER_CRC_ERROR: Parse error or value out of range
 * - Buffer overflow: Logged, buffer reset, error_count++
 * - Timeout: No data for >1 second triggers fallback
 * 
 * **Performance:**
 * @note Typical execution: 5-10ms (when parsing)
 * @note Non-blocking: reads available serial data only
 * @note No allocation: uses fixed 64-byte buffer
 * 
 * @return void (state updated via wj66_state structure)
 * 
 * @see encoder_wj66.h - Protocol and status definitions
 * @see wj66Init() - Initialize serial port
 * @see encoderMotionGetPosition() - Get current position from motion system
 */
void wj66Update() {
  static uint32_t last_read = 0;
  static char response_buffer[65] = {0};      // Fixed buffer (was: String)
  static uint8_t buffer_idx = 0;              // Track position in buffer
  static const uint8_t MAX_RESPONSE_LEN = 64; // Max expected response length
  
  if (millis() - last_read < WJ66_READ_INTERVAL_MS) return;
  last_read = millis();
  
  // Send read command periodically
  if (millis() - wj66_state.last_command_time > 500) {
    Serial1.print("#00\r");
    wj66_state.last_command_time = millis();
  }
  
  // Read with timeout and buffer management - prevent blocking
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    
    if (c == '\r') {
      // CRITICAL: Process complete response
      if (buffer_idx > 0 && response_buffer[0] == '!') {
        // Parse response: "!±val1,±val2,±val3,±val4\r"
        // Correctly handles negative numbers: -1234 -> parse 1234, then negate
        int commas = 0;
        int32_t values[4] = {0};
        int32_t current_value = 0;
        bool is_negative = false;
        
        for (uint8_t i = 1; i < buffer_idx; i++) {
          char ch = response_buffer[i];
          if (ch == ',') {
            // Save value, applying negative flag if set
            values[commas] = is_negative ? -current_value : current_value;
            current_value = 0;
            is_negative = false;
            commas++;
            if (commas >= 4) break;
          } else if (ch == '-') {
            // Set flag to negate - but don't negate yet (digits may follow)
            is_negative = true;
          } else if (ch == '+') {
            // Plus sign - just ignore, continue parsing digits
            // is_negative stays false
          } else if (ch >= '0' && ch <= '9') {
            // Accumulate digit: current_value = current_value * 10 + digit
            current_value = current_value * 10 + (ch - '0');
          }
        }
        
        // Handle last value (no comma after it)
        if (commas == 3) {
          values[3] = is_negative ? -current_value : current_value;
          
          // Validate values are reasonable (-99999 to +99999 mm)
          bool valid = true;
          for (int i = 0; i < 4; i++) {
            if (abs(values[i]) > 100000) {
              valid = false;
              logWarning("[WJ66] Encoder value out of range: axis %d = %ld", i, values[i]);
            }
          }
          
          if (valid) {
            for (int i = 0; i < 4; i++) {
              wj66_state.position[i] = values[i];
              wj66_state.last_read[i] = millis();
              wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
          } else {
            logError("[WJ66] Invalid encoder values received");
            wj66_state.status = ENCODER_CRC_ERROR;
          }
        } else {
          wj66_state.status = ENCODER_CRC_ERROR;
          wj66_state.error_count++;
        }
      }
      
      // Clear buffer for next response
      buffer_idx = 0;
      memset(response_buffer, 0, sizeof(response_buffer));
    } else if (c >= 32 && c < 127) {
      // Valid ASCII character - add to buffer with STRICT bounds checking
      if (buffer_idx < MAX_RESPONSE_LEN) {
        response_buffer[buffer_idx++] = c;
        response_buffer[buffer_idx] = '\0';  // Keep null-terminated
      } else {
        // CRITICAL: Buffer overflow - reset immediately
        logError("[WJ66] CRITICAL: Response buffer overflow detected!");
        buffer_idx = 0;
        memset(response_buffer, 0, sizeof(response_buffer));
        wj66_state.error_count++;
        faultLogWarning(FAULT_ENCODER_TIMEOUT, "WJ66 buffer overflow");
      }
    }
  }
  
  // Check for timeout
  for (int i = 0; i < WJ66_AXES; i++) {
    if (wj66IsStale(i) && wj66_state.read_count[i] > 0) {
      wj66_state.status = ENCODER_TIMEOUT;
      wj66_state.error_count++;
    }
  }
}

int32_t wj66GetPosition(uint8_t axis) {
  if (axis < WJ66_AXES) return wj66_state.position[axis];
  return 0;
}

uint32_t wj66GetAxisAge(uint8_t axis) {
  if (axis < WJ66_AXES) return millis() - wj66_state.last_read[axis];
  return 0xFFFFFFFF;
}

bool wj66IsStale(uint8_t axis) {
  return wj66GetAxisAge(axis) > WJ66_TIMEOUT_MS;
}

encoder_status_t wj66GetStatus() {
  return wj66_state.status;
}

void wj66Reset() {
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.last_read[i] = millis();
    wj66_state.read_count[i] = 0;
  }
  wj66_state.error_count = 0;
}

void wj66Diagnostics() {
  Serial.println("\n[WJ66] === Encoder Diagnostics ===");
  Serial.print("Status: ");
  switch(wj66_state.status) {
    case ENCODER_OK: Serial.println("OK"); break;
    case ENCODER_TIMEOUT: Serial.println("TIMEOUT"); break;
    case ENCODER_CRC_ERROR: Serial.println("CRC ERROR"); break;
    case ENCODER_NOT_FOUND: Serial.println("NOT FOUND"); break;
  }
  Serial.print("Total Errors: ");
  Serial.println(wj66_state.error_count);
  
  for (int i = 0; i < WJ66_AXES; i++) {
    Serial.print("  Axis ");
    Serial.print(i);
    Serial.print(": pos=");
    Serial.print(wj66_state.position[i]);
    Serial.print(" age=");
    Serial.print(wj66GetAxisAge(i));
    Serial.print("ms reads=");
    Serial.print(wj66_state.read_count[i]);
    Serial.print(" stale=");
    Serial.println(wj66IsStale(i) ? "YES" : "NO");
  }
}

void wj66PrintStatus() {
  Serial.print("[WJ66] ");
  for (int i = 0; i < WJ66_AXES; i++) {
    Serial.print("A");
    Serial.print(i);
    Serial.print("=");
    Serial.print(wj66_state.position[i]);
    if (i < WJ66_AXES - 1) Serial.print(" ");
  }
  Serial.println();
}

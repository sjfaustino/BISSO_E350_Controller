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

void wj66Update() {
  static uint32_t last_read = 0;
  static String response_buffer = "";
  
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
      // Process complete response
      if (response_buffer.startsWith("!")) {
        // Parse response: "!±val1,±val2,±val3,±val4\r"
        // Correctly handles negative numbers: -1234 -> parse 1234, then negate
        int commas = 0;
        int32_t values[4] = {0};
        int32_t current_value = 0;
        bool is_negative = false;
        
        for (int i = 1; i < response_buffer.length(); i++) {
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
              logWarning("Encoder value out of range: axis %d = %ld", i, values[i]);
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
            logError("Invalid encoder values received");
            wj66_state.status = ENCODER_CRC_ERROR;
          }
        } else {
          wj66_state.status = ENCODER_CRC_ERROR;
          wj66_state.error_count++;
        }
      }
      
      response_buffer = ""; // Clear for next response
    } else if (c >= 32 && c < 127) {
      // Valid ASCII character
      response_buffer += c;
      
      // Buffer overflow protection - prevent unbounded growth
      if (response_buffer.length() > 64) {
        response_buffer = "";
        wj66_state.error_count++;
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

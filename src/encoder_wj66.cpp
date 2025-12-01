#include "encoder_wj66.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "encoder_comm_stats.h" // Needed for baud rate detection

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
  
  // *** FIX: Integrate Baud Rate Auto-Detection ***
  Serial.println("[WJ66] Starting baud rate auto-detection...");
  baud_detect_result_t result = encoderDetectBaudRate();
  
  uint32_t baud_rate = WJ66_BAUD; // Default to 9600
  if (result.detected) {
      baud_rate = result.baud_rate;
      Serial.printf("[WJ66] Detected baud rate: %lu\n", baud_rate);
  } else {
      Serial.printf("[WJ66] WARNING: Auto-detection failed. Defaulting to %lu baud.\n", WJ66_BAUD);
      faultLogWarning(FAULT_ENCODER_TIMEOUT, "WJ66 Baud rate auto-detection failed");
  }

  // Initialize Serial1 with the detected or default baud rate
  Serial1.begin(baud_rate, SERIAL_8N1, 14, 33);
  
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
 */
void wj66Update() {
  static uint32_t last_read = 0;
  static char response_buffer[65] = {0};
  static uint8_t buffer_idx = 0;
  static const uint8_t MAX_RESPONSE_LEN = 64; 
  
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
        int commas = 0;
        int32_t values[4] = {0};
        int32_t current_value = 0;
        bool is_negative = false;
        
        for (uint8_t i = 1; i < buffer_idx; i++) {
          char ch = response_buffer[i];
          if (ch == ',') {
            values[commas] = is_negative ? -current_value : current_value;
            current_value = 0;
            is_negative = false;
            commas++;
            if (commas >= 4) break;
          } else if (ch == '-') {
            is_negative = true;
          } else if (ch == '+') {
          } else if (ch >= '0' && ch <= '9') {
            current_value = current_value * 10 + (ch - '0');
          }
        }
        
        // Handle last value (no comma after it)
        if (commas == 3) {
          values[3] = is_negative ? -current_value : current_value;
          
          // Validate values are reasonable (-99999 to +99999 mm)
          bool valid = true;
          for (int i = 0; i < 4; i++) {
            if (abs(values[i]) > 100000) { // Check against +/- 100,000 threshold
              valid = false;
              logWarning("[WJ66] Encoder value out of range: axis %d = %ld", i, values[i]);
            }
          }
          
          if (valid) {
            for (int i = 0; i < WJ66_AXES; i++) {
              wj66_state.position[i] = values[i];
              wj66_state.last_read[i] = millis();
              wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
          } else {
            logError("[WJ66] Invalid encoder values received (out of range)");
            wj66_state.status = ENCODER_CRC_ERROR;
            // --- NEW FAULT LOGGING ---
            faultLogEntry(FAULT_WARNING, FAULT_ENCODER_SPIKE, -1, values[0], 
                          "WJ66 value out of range (Example X=%ld)", values[0]);
            // -------------------------
          }
        } else {
          wj66_state.status = ENCODER_CRC_ERROR;
          wj66_state.error_count++;
          // --- NEW FAULT LOGGING ---
          faultLogEntry(FAULT_WARNING, FAULT_ENCODER_TIMEOUT, -1, commas, 
                        "WJ66 response missing values/commas (%d found)", commas);
          // -------------------------
        }
      }
      
      // Clear buffer for next response
      buffer_idx = 0;
      memset(response_buffer, 0, sizeof(response_buffer));
    } else if (c >= 32 && c < 127) {
      // Valid ASCII character - add to buffer with STRICT bounds checking
      if (buffer_idx < MAX_RESPONSE_LEN) {
        response_buffer[buffer_idx++] = c;
        response_buffer[buffer_idx] = '\0';
      } else {
        // CRITICAL: Buffer overflow - reset immediately
        logError("[WJ66] CRITICAL: Response buffer overflow detected!");
        buffer_idx = 0;
        memset(response_buffer, 0, sizeof(response_buffer));
        wj66_state.error_count++;
        // --- NEW FAULT LOGGING ---
        faultLogEntry(FAULT_WARNING, FAULT_ENCODER_TIMEOUT, -1, MAX_RESPONSE_LEN, 
                      "WJ66 buffer overflow (Max size %d)", MAX_RESPONSE_LEN);
        // -------------------------
      }
    }
  }
  
  // Check for timeout
  for (int i = 0; i < WJ66_AXES; i++) {
    if (wj66IsStale(i) && wj66_state.read_count[i] > 0) {
      wj66_state.status = ENCODER_TIMEOUT;
      wj66_state.error_count++;
      // --- NEW FAULT LOGGING ---
      faultLogEntry(FAULT_WARNING, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), 
                    "WJ66 Axis %d data stale/timeout (Age: %lu ms)", i, wj66GetAxisAge(i));
      // -------------------------
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
  logInfo("[WJ66] Position and stats reset.");
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
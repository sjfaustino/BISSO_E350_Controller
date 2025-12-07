/**
 * @file encoder_wj66.cpp
 * @brief Driver for WJ66 Absolute Encoders
 */

#include "encoder_wj66.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "encoder_comm_stats.h" 
#include "system_constants.h"
#include <Preferences.h>
#include <Arduino.h>

// Internal State
struct {
  int32_t position[WJ66_AXES];      
  int32_t zero_offset[WJ66_AXES];   
  uint32_t last_read[WJ66_AXES];
  uint32_t read_count[WJ66_AXES];
  encoder_status_t status;
  uint32_t error_count;
  uint32_t last_command_time;
} wj66_state = {{0}, {0}, {0}, {0}, ENCODER_OK, 0, 0};

void wj66Init() {
  logInfo("[WJ66] Initializing...");
  
  uint32_t final_baud = WJ66_BAUD;
  bool confirmed = false;

  // 1. Load saved baud rate
  Preferences p;
  p.begin("wj66_config", true);
  uint32_t saved_baud = p.getUInt("baud_rate", 0);
  p.end();

  if (saved_baud > 0) {
      logInfo("[WJ66] Trying saved baud: %lu...", (unsigned long)saved_baud);
      Serial1.begin(saved_baud, SERIAL_8N1, 14, 33);
      while(Serial1.available()) Serial1.read(); 
      
      uint8_t q[] = {0x01, 0x00}; 
      if (encoderSendCommandWithStats(q, 2)) {
          uint8_t d[32];
          if (encoderReadFrameWithStats(d, 32, 200)) {
              logInfo("[WJ66] Saved baud verified.");
              final_baud = saved_baud;
              confirmed = true;
          }
      }
      
      if (!confirmed) {
          Serial1.end();
          delay(100);
      }
  }

  // 2. Auto-detect
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
      else encoderSetBaudRate(final_baud);
  }

  // Init State
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    wj66_state.last_read[i] = millis();
  }
  wj66_state.status = ENCODER_OK;
  logInfo("[WJ66] Ready @ %lu baud", (unsigned long)final_baud);
}

void wj66Update() {
  static uint32_t last_read = 0;
  static char response_buffer[65] = {0};
  static uint8_t buffer_idx = 0;
  static const uint8_t MAX_RESPONSE_LEN = 64; 
  
  if (millis() - last_read < WJ66_READ_INTERVAL_MS) return;
  last_read = millis();
  
  if (millis() - wj66_state.last_command_time > 200) { 
    Serial1.print("#00\r");
    wj66_state.last_command_time = millis();
  }
  
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    
    if (c == '\r') {
      if (buffer_idx > 0 && response_buffer[0] == '!') {
        int commas = 0;
        int32_t values[4] = {0};
        int32_t current_value = 0;
        bool is_negative = false;
        
        for (uint8_t i = 1; i < buffer_idx; i++) {
          char ch = response_buffer[i];
          if (ch == ',') {
            values[commas] = is_negative ? -current_value : current_value;
            current_value = 0; is_negative = false;
            commas++;
            if (commas >= 4) break;
          } else if (ch == '-') is_negative = true;
          else if (ch >= '0' && ch <= '9') current_value = current_value * 10 + (ch - '0');
        }
        
        if (commas == 3) {
          values[3] = is_negative ? -current_value : current_value;
          bool valid = true;
          
          if (valid) {
            for (int i = 0; i < WJ66_AXES; i++) {
              wj66_state.position[i] = values[i];
              wj66_state.last_read[i] = millis();
              wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
          }
        } else {
          wj66_state.status = ENCODER_CRC_ERROR;
          wj66_state.error_count++;
        }
      }
      buffer_idx = 0;
      memset(response_buffer, 0, sizeof(response_buffer));
    } 
    else if (c >= 32 && c < 127) {
      if (buffer_idx < MAX_RESPONSE_LEN) response_buffer[buffer_idx++] = c;
      else {
        buffer_idx = 0;
        wj66_state.error_count++;
      }
    }
  }
}

int32_t wj66GetPosition(uint8_t axis) { 
    if (axis >= WJ66_AXES) return 0;
    return wj66_state.position[axis] - wj66_state.zero_offset[axis]; 
}

uint32_t wj66GetAxisAge(uint8_t axis) { 
    return (axis < WJ66_AXES) ? millis() - wj66_state.last_read[axis] : 0xFFFFFFFF; 
}

bool wj66IsStale(uint8_t axis) { 
    return wj66GetAxisAge(axis) > WJ66_TIMEOUT_MS; 
}

encoder_status_t wj66GetStatus() { return wj66_state.status; }

void wj66Reset() {
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    wj66_state.last_read[i] = millis();
  }
  wj66_state.error_count = 0;
  logInfo("[WJ66] Stats reset.");
}

void wj66SetZero(uint8_t axis) {
    if (axis < WJ66_AXES) {
        wj66_state.zero_offset[axis] = wj66_state.position[axis];
        logInfo("[WJ66] Axis %d Zeroed. Offset: %ld", axis, (long)wj66_state.zero_offset[axis]);
    }
}

void wj66Diagnostics() {
  Serial.println("\n=== ENCODER STATUS ===");
  Serial.printf("Status: %d\nErrors: %lu\n", wj66_state.status, (unsigned long)wj66_state.error_count);
  for (int i = 0; i < WJ66_AXES; i++) {
    Serial.printf("  Axis %d: Raw=%ld | Offset=%ld | NET=%ld\n", 
        i, 
        (long)wj66_state.position[i], 
        (long)wj66_state.zero_offset[i],
        (long)(wj66_state.position[i] - wj66_state.zero_offset[i])
    );
  }
}
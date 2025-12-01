#include "encoder_comm_stats.h"
#include "fault_logging.h"

// WJ66 uses Serial1 on ESP32-S3
#define ENCODER_SERIAL_RX 14
#define ENCODER_SERIAL_TX 33

static encoder_stats_t stats = {0};
static uint32_t current_baud_rate = 9600;
static HardwareSerial* encoder_serial = &Serial1;

// Supported baud rates for detection
static const uint32_t supported_bauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
static const uint8_t num_bauds = sizeof(supported_bauds) / sizeof(supported_bauds[0]);

// WJ66 frame format:
// [0xFD] [CMD] [DATA...] [CHECKSUM] [0x0D 0x0A]

void encoderStatsInit() {
  Serial.println("[ENC_STATS] Encoder communication statistics initialized");
  memset(&stats, 0, sizeof(stats));
  stats.baud_rate_current = 9600;
  current_baud_rate = 9600;
}

uint8_t encoderCalculateChecksum(const uint8_t* data, uint8_t len) {
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < len; i++) {
    checksum ^= data[i];  // XOR checksum
  }
  return checksum;
}

bool encoderValidateChecksum(const uint8_t* data, uint8_t len) {
  if (len < 3) return false;  // Need at least [CMD] [DATA] [CHECKSUM]
  
  // Calculate checksum of all but last byte
  uint8_t calculated = encoderCalculateChecksum(data, len - 1);
  uint8_t received = data[len - 1];
  
  if (calculated != received) {
    stats.checksum_errors++;
    return false;
  }
  
  return true;
}

bool encoderValidateFrame(const uint8_t* frame, uint8_t len) {
  // Check minimum length
  if (len < 4) {
    stats.parse_errors++;
    return false;
  }
  
  // Check start marker (0xFD)
  if (frame[0] != 0xFD) {
    stats.parse_errors++;
    return false;
  }
  
  // Check end markers (0x0D 0x0A)
  if (frame[len-2] != 0x0D || frame[len-1] != 0x0A) {
    stats.parse_errors++;
    return false;
  }
  
  // Validate checksum
  if (!encoderValidateChecksum(&frame[1], len - 3)) {
    return false;
  }
  
  return true;
}

bool encoderReadFrameWithStats(uint8_t* data, uint8_t max_len, uint32_t timeout_ms) {
  stats.frames_sent++;
  
  uint32_t start_time = millis();
  uint8_t frame[max_len];
  uint8_t pos = 0;
  
  while (millis() - start_time < timeout_ms) {
    if (encoder_serial->available()) {
      uint8_t byte = encoder_serial->read();
      
      if (pos == 0 && byte != 0xFD) {
        continue;  // Wait for start marker
      }
      
      frame[pos++] = byte;
      
      // Check for frame end
      if (pos >= 2 && frame[pos-2] == 0x0D && frame[pos-1] == 0x0A) {
        // Validate complete frame
        if (encoderValidateFrame(frame, pos)) {
          // Copy data (excluding markers and checksum)
          uint8_t data_len = pos - 4;  // Remove [FD] [CHECKSUM] [0D 0A]
          if (data_len <= max_len) {
            memcpy(data, &frame[1], data_len);
            stats.frames_received++;
            stats.last_frame_time = millis();
            return true;
          }
        }
        
        pos = 0;  // Reset for next frame
      }
      
      if (pos >= max_len) {
        pos = 0;  // Buffer overflow, reset
        stats.parse_errors++;
      }
    }
    
    delay(1);
  }
  
  stats.frames_failed++;
  stats.timeout_errors++;
  return false;
}

bool encoderSendCommandWithStats(const uint8_t* cmd, uint8_t len) {
  // Build frame: [0xFD] [CMD] [DATA...] [CHECKSUM] [0x0D 0x0A]
  uint8_t frame[len + 4];
  
  frame[0] = 0xFD;  // Start marker
  memcpy(&frame[1], cmd, len);  // Command and data
  frame[len + 1] = encoderCalculateChecksum(cmd, len);  // Checksum
  frame[len + 2] = 0x0D;  // End marker
  frame[len + 3] = 0x0A;
  
  // Send frame
  encoder_serial->write(frame, len + 4);
  encoder_serial->flush();
  
  stats.frames_sent++;
  return true;
}

baud_detect_result_t encoderDetectBaudRate() {
  Serial.println("[ENC_STATS] Starting baud rate auto-detection...");
  
  baud_detect_result_t result = {0, false, 0};
  uint32_t start_time = millis();
  
  // Try each baud rate
  for (uint8_t i = 0; i < num_bauds; i++) {
    uint32_t baud = supported_bauds[i];
    
    // Close and reopen at new baud rate
    encoder_serial->end();
    delay(100);
    encoder_serial->begin(baud, SERIAL_8N1, ENCODER_SERIAL_RX, ENCODER_SERIAL_TX);
    
    Serial.print("[ENC_STATS] Trying ");
    Serial.print(baud);
    Serial.println(" baud...");
    
    // Send query command and wait for response
    uint8_t query_cmd[] = {0x01, 0x00};  // Query command
    encoderSendCommandWithStats(query_cmd, 2);
    
    // Try to read response
    uint8_t response[32];
    if (encoderReadFrameWithStats(response, sizeof(response), 500)) {
      Serial.print("[ENC_STATS] ✅ Detected baud rate: ");
      Serial.println(baud);
      
      result.baud_rate = baud;
      result.detected = true;
      result.detection_time_ms = millis() - start_time;
      current_baud_rate = baud;
      stats.baud_rate_current = baud;
      stats.baud_rate_changes++;
      
      return result;
    }
  }
  
  Serial.println("[ENC_STATS] ❌ Could not detect baud rate - using default 9600");
  encoder_serial->end();
  delay(100);
  encoder_serial->begin(9600, SERIAL_8N1, ENCODER_SERIAL_RX, ENCODER_SERIAL_TX);
  
  result.baud_rate = 9600;
  result.detected = false;
  result.detection_time_ms = millis() - start_time;
  
  return result;
}

bool encoderSetBaudRate(uint32_t baud_rate) {
  Serial.print("[ENC_STATS] Setting baud rate to ");
  Serial.println(baud_rate);
  
  encoder_serial->end();
  delay(100);
  encoder_serial->begin(baud_rate, SERIAL_8N1, ENCODER_SERIAL_RX, ENCODER_SERIAL_TX);
  
  current_baud_rate = baud_rate;
  stats.baud_rate_current = baud_rate;
  stats.baud_rate_changes++;
  
  return true;
}

uint32_t encoderGetCurrentBaudRate() {
  return current_baud_rate;
}

void encoderResetStats() {
  memset(&stats, 0, sizeof(stats));
  Serial.println("[ENC_STATS] Statistics reset");
}

encoder_stats_t encoderGetStats() {
  if (stats.frames_sent > 0) {
    stats.success_rate = (float)stats.frames_received / stats.frames_sent * 100.0f;
  } else {
    stats.success_rate = 0.0f;
  }
  return stats;
}

float encoderGetHealthScore() {
  return stats.success_rate;
}

const char* encoderGetHealthStatus() {
  float health = encoderGetHealthScore();
  
  if (health >= 99.0f) return "EXCELLENT";
  if (health >= 95.0f) return "GOOD";
  if (health >= 90.0f) return "WARNING";
  return "CRITICAL";
}

void encoderShowStats() {
  encoder_stats_t current = encoderGetStats();
  
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║         ENCODER COMMUNICATION STATISTICS & DIAGNOSTICS         ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("[ENC_STATS] Communication Summary:");
  Serial.print("  Total Frames: ");
  Serial.println(current.frames_sent);
  
  Serial.print("  Successful: ");
  Serial.print(current.frames_received);
  Serial.print(" (");
  Serial.print(current.success_rate);
  Serial.println("%)");
  
  Serial.print("  Failed: ");
  Serial.println(current.frames_failed);
  
  Serial.println("\n[ENC_STATS] Error Analysis:");
  Serial.print("  Checksum Errors: ");
  Serial.println(current.checksum_errors);
  
  Serial.print("  Timeout Errors: ");
  Serial.println(current.timeout_errors);
  
  Serial.print("  Parse Errors: ");
  Serial.println(current.parse_errors);
  
  Serial.println("\n[ENC_STATS] Baud Rate Information:");
  Serial.print("  Current Baud Rate: ");
  Serial.println(current.baud_rate_current);
  
  Serial.print("  Baud Rate Changes: ");
  Serial.println(current.baud_rate_changes);
  
  if (current.last_frame_time > 0) {
    Serial.print("  Last Frame: ");
    Serial.print(millis() - current.last_frame_time);
    Serial.println(" ms ago");
  }
  
  Serial.println("\n[ENC_STATS] Communication Health:");
  Serial.print("  Health Score: ");
  Serial.print(current.success_rate);
  Serial.println("%");
  
  Serial.print("  Status: ");
  Serial.println(encoderGetHealthStatus());
  
  Serial.println("\n[ENC_STATS] Checksum Validation:");
  Serial.println("  XOR checksum on all frames");
  Serial.println("  Frame format: [0xFD] [CMD] [DATA...] [CHECKSUM] [0x0D 0x0A]");
  
  Serial.println("\n[ENC_STATS] Supported Baud Rates for Auto-Detection:");
  Serial.print("  ");
  for (uint8_t i = 0; i < num_bauds; i++) {
    Serial.print(supported_bauds[i]);
    if (i < num_bauds - 1) Serial.print(", ");
  }
  Serial.println();
  
  Serial.println();
}

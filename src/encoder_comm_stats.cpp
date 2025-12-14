#include "encoder_comm_stats.h"
#include "fault_logging.h"

// ESP32-WROOM-32E Pin Configuration
// For KC868-A16: Configure encoder serial pins via KCS config
// Default: UART1 on GPIO9 (RX) / GPIO10 (TX) if not remapped
// Alternative: Any GPIO pair can be remapped via serial port config
#define ENCODER_SERIAL_RX 9      // GPIO9 (default UART1 RX)
#define ENCODER_SERIAL_TX 10     // GPIO10 (default UART1 TX)

// Fully initialize struct to prevent missing field warnings
static encoder_stats_t stats = {0, 0, 0, 0, 0, 0, 9600, 0, 0.0f, 0};
static uint32_t current_baud_rate = 9600;
static HardwareSerial* encoder_serial = &Serial1;

static const uint32_t supported_bauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
static const uint8_t num_bauds = sizeof(supported_bauds) / sizeof(supported_bauds[0]);

void encoderStatsInit() {
  Serial.println("[ENC_STATS] Initialized");
  memset(&stats, 0, sizeof(stats));
  stats.baud_rate_current = 9600;
  current_baud_rate = 9600;
}

uint8_t encoderCalculateChecksum(const uint8_t* data, uint8_t len) {
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < len; i++) checksum ^= data[i];
  return checksum;
}

bool encoderValidateChecksum(const uint8_t* data, uint8_t len) {
  if (len < 1) return false;
  uint8_t calculated = encoderCalculateChecksum(data, len - 1);
  if (calculated != data[len - 1]) {
      stats.checksum_errors++;
      return false;
  }
  return true;
}

bool encoderValidateFrame(const uint8_t* frame, uint8_t len) {
  if (len < 4) { stats.parse_errors++; return false; }
  if (frame[0] != 0xFD) { stats.parse_errors++; return false; } 
  return encoderValidateChecksum(&frame[1], len - 3); 
}

bool encoderReadFrameWithStats(uint8_t* data, uint8_t max_len, uint32_t timeout_ms) {
  stats.frames_sent++;
  uint32_t start = millis();
  uint8_t pos = 0;
  uint8_t frame_buf[64];

  while (millis() - start < timeout_ms) {
      if (encoder_serial->available()) {
          uint8_t b = encoder_serial->read();
          if (pos == 0 && b != 0xFD) continue; 
          
          frame_buf[pos++] = b;
          if (pos >= 2 && frame_buf[pos-2] == 0x0D && frame_buf[pos-1] == 0x0A) {
              if (encoderValidateFrame(frame_buf, pos)) {
                  stats.frames_received++;
                  stats.last_frame_time = millis();
                  memcpy(data, frame_buf, (pos < max_len ? pos : max_len));
                  return true;
              }
              pos = 0; 
          }
          if (pos >= 64) pos = 0; 
      }
      delay(1);
  }
  
  stats.frames_failed++;
  stats.timeout_errors++;
  return false;
}

bool encoderSendCommandWithStats(const uint8_t* cmd, uint8_t len) {
  uint8_t frame[len + 4];
  frame[0] = 0xFD;
  memcpy(&frame[1], cmd, len);
  frame[len+1] = encoderCalculateChecksum(cmd, len);
  frame[len+2] = 0x0D;
  frame[len+3] = 0x0A;
  
  encoder_serial->write(frame, len+4);
  encoder_serial->flush();
  return true;
}

baud_detect_result_t encoderDetectBaudRate() {
  Serial.println("[ENC_STATS] Auto-detecting baud rate...");
  baud_detect_result_t result = {0, false, 0};
  
  for (uint8_t i = 0; i < num_bauds; i++) {
    uint32_t baud = supported_bauds[i];
    
    encoder_serial->end();
    delay(10);
    encoder_serial->begin(baud, SERIAL_8N1, ENCODER_SERIAL_RX, ENCODER_SERIAL_TX);
    
    // FIX: Cast to unsigned long
    Serial.printf("[ENC_STATS] Probing %lu baud...\n", (unsigned long)baud);
    
    uint8_t query[] = {0x01, 0x00}; 
    encoderSendCommandWithStats(query, 2);
    
    uint8_t resp[32];
    if (encoderReadFrameWithStats(resp, 32, 200)) {
        // FIX: Cast to unsigned long
        Serial.printf("[ENC_STATS] [OK] Detected: %lu\n", (unsigned long)baud);
        result.baud_rate = baud;
        result.detected = true;
        current_baud_rate = baud;
        return result;
    }
  }
  
  Serial.println("[ENC_STATS] [FAIL] Detection failed. Defaulting to 9600.");
  encoder_serial->begin(9600, SERIAL_8N1, ENCODER_SERIAL_RX, ENCODER_SERIAL_TX);
  result.baud_rate = 9600;
  return result;
}

bool encoderSetBaudRate(uint32_t baud_rate) {
  // FIX: Cast to unsigned long
  Serial.printf("[ENC_STATS] Setting baud to %lu\n", (unsigned long)baud_rate);
  encoder_serial->updateBaudRate(baud_rate);
  current_baud_rate = baud_rate;
  stats.baud_rate_changes++;
  return true;
}

void encoderShowStats() {
  Serial.println("\n=== ENCODER STATISTICS ===");
  // FIX: Casts to unsigned long
  Serial.printf("Sent: %lu | Recv: %lu | Fail: %lu\n", 
    (unsigned long)stats.frames_sent, (unsigned long)stats.frames_received, (unsigned long)stats.frames_failed);
  Serial.printf("Errors: Cksum=%lu Time=%lu Parse=%lu\n", 
    (unsigned long)stats.checksum_errors, (unsigned long)stats.timeout_errors, (unsigned long)stats.parse_errors);
  Serial.printf("Baud: %lu | Success: %.1f%%\n", (unsigned long)current_baud_rate, stats.success_rate);
}

void encoderResetStats() { memset(&stats, 0, sizeof(stats)); }
encoder_stats_t encoderGetStats() { return stats; }
uint32_t encoderGetCurrentBaudRate() { return current_baud_rate; }
float encoderGetHealthScore() { return stats.success_rate; }
const char* encoderGetHealthStatus() { 
    if (stats.success_rate > 99.0) return "EXCELLENT";
    if (stats.success_rate > 90.0) return "GOOD";
    return "POOR";
}
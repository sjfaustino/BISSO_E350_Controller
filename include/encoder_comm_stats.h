#ifndef ENCODER_COMM_STATS_H
#define ENCODER_COMM_STATS_H

#include <Arduino.h>

// Encoder communication statistics
typedef struct {
  uint32_t frames_sent;              // Total frames sent
  uint32_t frames_received;          // Successful frames received
  uint32_t frames_failed;            // Failed frames
  uint32_t checksum_errors;          // Checksum failures
  uint32_t timeout_errors;           // Read timeouts
  uint32_t parse_errors;             // Parse failures
  uint32_t baud_rate_current;        // Current baud rate
  uint32_t baud_rate_changes;        // Number of baud rate changes
  float success_rate;                // Success percentage
  uint32_t last_frame_time;          // Timestamp of last successful frame (ms)
} encoder_stats_t;

// Baud rate detection result
typedef struct {
  uint32_t baud_rate;                // Detected baud rate
  bool detected;                     // Detection successful
  uint32_t detection_time_ms;        // Time taken to detect
} baud_detect_result_t;

// Initialize encoder statistics
void encoderStatsInit();

// Communication functions with statistics
bool encoderReadFrameWithStats(uint8_t* data, uint8_t max_len, uint32_t timeout_ms);
bool encoderSendCommandWithStats(const uint8_t* cmd, uint8_t len);

// Data validation
uint8_t encoderCalculateChecksum(const uint8_t* data, uint8_t len);
bool encoderValidateChecksum(const uint8_t* data, uint8_t len);
bool encoderValidateFrame(const uint8_t* frame, uint8_t len);

// Automatic baud rate detection
baud_detect_result_t encoderDetectBaudRate();
bool encoderSetBaudRate(uint32_t baud_rate);
uint32_t encoderGetCurrentBaudRate();

// Statistics
void encoderResetStats();
encoder_stats_t encoderGetStats();
void encoderShowStats();

// Communication health
float encoderGetHealthScore();
const char* encoderGetHealthStatus();

#endif

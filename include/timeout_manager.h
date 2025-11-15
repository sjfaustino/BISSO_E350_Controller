#ifndef TIMEOUT_MANAGER_H
#define TIMEOUT_MANAGER_H

#include <Arduino.h>

// Standard timeout values (milliseconds)
#define TIMEOUT_ENCODER_READ         500    // WJ66 serial read
#define TIMEOUT_PLC_RESPONSE         1000   // PLC I2C handshake
#define TIMEOUT_LCD_RESPONSE         200    // LCD update response
#define TIMEOUT_MOTION_EXECUTE       120000 // Motion completion
#define TIMEOUT_CALIBRATION_MOVE     30000  // Calibration move
#define TIMEOUT_EMERGENCY_STOP       5000   // E-stop safety hold
#define TIMEOUT_CONFIG_SAVE          500    // NVS write
#define TIMEOUT_SAFETY_CHECK         100    // Safety loop interval
#define TIMEOUT_CLI_COMMAND          5000   // CLI command execution

typedef enum {
  TIMEOUT_ENCODER = 0,
  TIMEOUT_PLC = 1,
  TIMEOUT_LCD = 2,
  TIMEOUT_MOTION = 3,
  TIMEOUT_CALIBRATION = 4,
  TIMEOUT_ESTOP = 5,
  TIMEOUT_CONFIG = 6,
  TIMEOUT_SAFETY = 7,
  TIMEOUT_CLI = 8,
  TIMEOUT_MAX = 9
} timeout_type_t;

typedef struct {
  timeout_type_t type;
  uint32_t start_time;
  uint32_t timeout_ms;
  bool active;
  bool triggered;
} timeout_handle_t;

// Initialize timeout manager
void timeoutManagerInit();

// Create and manage timeouts
timeout_handle_t* timeoutStart(timeout_type_t type);
timeout_handle_t* timeoutStartCustom(timeout_type_t type, uint32_t custom_ms);
bool timeoutCheck(timeout_handle_t* handle);
bool timeoutExpired(timeout_handle_t* handle);
void timeoutStop(timeout_handle_t* handle);
uint32_t timeoutElapsed(timeout_handle_t* handle);
uint32_t timeoutRemaining(timeout_handle_t* handle);

// Get standard timeout value
uint32_t timeoutGetStandard(timeout_type_t type);

// Reset all timeouts
void timeoutResetAll();

// Diagnostics
void timeoutShowDiagnostics();

#endif

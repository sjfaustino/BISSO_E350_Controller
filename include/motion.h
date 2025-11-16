#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <stdint.h>
#include "system_constants.h"

// Motion constants
#define MOTION_AXES 4
#define MOTION_ACCELERATION 5.0f
#define MOTION_STALL_TIMEOUT_MS 2000
#define MOTION_UPDATE_INTERVAL_MS 10
// Note: MOTION_MAX_SPEED defined in system_constants.h

// Speed Profile Mapping (Controller -> PLC notification via PCF8574 I2C expander)
// KC868-A16 uses PCF8574 I2C expander for digital outputs
typedef enum {
  SPEED_PROFILE_1 = 0,  // P0:0, P1:0 (Slow:   0-30 mm/s)
  SPEED_PROFILE_2 = 1,  // P0:1, P1:0 (Medium: 31-80 mm/s)
  SPEED_PROFILE_3 = 2   // P0:0, P1:1 (Fast:   81-200 mm/s)
} speed_profile_t;

// Motion states
typedef enum {
  MOTION_IDLE = 0,
  MOTION_ACCELERATING = 1,
  MOTION_CONSTANT_SPEED = 2,
  MOTION_DECELERATING = 3,
  MOTION_PAUSED = 4,
  MOTION_ERROR = 5
} motion_state_t;

// Axis structure
typedef struct {
  int32_t position;
  int32_t target_position;
  float current_speed;
  float target_speed;
  float acceleration;
  motion_state_t state;
  uint32_t last_update_ms;
  bool homed;
  bool enabled;
  
  // Soft limit protection
  int32_t soft_limit_min;
  int32_t soft_limit_max;
  bool soft_limit_enabled;
  uint32_t limit_violation_count;
} motion_axis_t;

// Motion system initialization
void motionInit();

// Motion updates (call in main loop)
void motionUpdate();

// Motion commands
void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s);
void motionSetAxisTarget(uint8_t axis, int32_t target_pos);
void motionStop();
void motionPause();
void motionResume();
void motionEmergencyStop();
bool motionClearEmergencyStop();  // NEW: Recovery with safety checks
bool motionIsEmergencyStopped();   // NEW: Query E-stop status

// Motion queries
int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis);
float motionGetSpeed(uint8_t axis);
motion_state_t motionGetState(uint8_t axis);
bool motionIsMoving();
bool motionIsStalled(uint8_t axis);

// Motion diagnostics
void motionDiagnostics();
void motionPrintStatus();

// Soft limit management
void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos);
void motionEnableSoftLimits(uint8_t axis, bool enable);
bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos);
uint32_t motionGetLimitViolations(uint8_t axis);

// State machine validation
bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state);
bool motionSetState(uint8_t axis, motion_state_t new_state);

// Speed Profile Control (Controller notifies PLC via GPIO)
speed_profile_t motionMapSpeedToProfile(float speed_mm_s);
void motionSetPLCSpeedProfile(speed_profile_t profile);
const char* motionStateToString(motion_state_t state);

// Encoder feedback integration
void motionEnableEncoderFeedback(bool enable);
bool motionIsEncoderFeedbackEnabled();

#endif

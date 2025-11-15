#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <stdint.h>

// Motion constants
#define MOTION_AXES 4
#define MOTION_MAX_SPEED 100.0f
#define MOTION_ACCELERATION 5.0f
#define MOTION_STALL_TIMEOUT_MS 2000
#define MOTION_UPDATE_INTERVAL_MS 10

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
const char* motionStateToString(motion_state_t state);

// Encoder feedback integration
void motionEnableEncoderFeedback(bool enable);
bool motionIsEncoderFeedbackEnabled();

#endif

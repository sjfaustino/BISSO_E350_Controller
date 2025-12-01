#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <stdint.h>
#include "system_constants.h"
#include "plc_iface.h" // Necessary for ELBO_I73 constants used in the arrays below

#define MOTION_AXES 4
#define MOTION_UPDATE_INTERVAL_MS 10
#define MOTION_CONSENSO_TIMEOUT_MS 5000 // 5 Seconds timeout for PLC handshake

typedef enum {
  SPEED_PROFILE_1 = 0,
  SPEED_PROFILE_2 = 1,
  SPEED_PROFILE_3 = 2
} speed_profile_t;

typedef enum {
  MOTION_IDLE = 0,
  MOTION_WAIT_CONSENSO = 1, // Waiting for PLC Q73 confirmation
  MOTION_EXECUTING = 2,
  MOTION_STOPPING = 3,
  MOTION_PAUSED = 4,        // Motion paused by operator
  MOTION_ERROR = 5
} motion_state_t;

// Streamlined structure for PLC/Encoder driven control
typedef struct {
  int32_t position;                   // Current position (synchronized to encoder counts)
  int32_t target_position;            // Target position in encoder counts
  motion_state_t state;               // Current state of the motion controller
  uint32_t last_update_ms;            // Timestamp of last motion update
  uint32_t state_entry_ms;            // Timestamp when current state was entered (for timeouts)
  bool enabled;                       
  
  // Soft limit protection
  int32_t soft_limit_min;             // Minimum soft limit (counts)
  int32_t soft_limit_max;             // Maximum soft limit (counts)
  bool soft_limit_enabled;            // Soft limits active
  uint32_t limit_violation_count;     // Counter for soft limit hits

  // State management
  int32_t position_at_stop;           // Encoder position when PLC stop command was issued
  speed_profile_t saved_speed_profile; // Saved profile for Resume operations
} motion_axis_t;

// Motion system initialization
void motionInit();

// Motion updates (call in motion task loop)
void motionUpdate();

// Motion commands
void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s);
void motionStop();
void motionPause();
void motionResume();
void motionEmergencyStop();
bool motionClearEmergencyStop();
bool motionIsEmergencyStopped();

// Motion queries
int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis);
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

// Speed Profile Control
speed_profile_t motionMapSpeedToProfile(uint8_t axis, float requested_speed_mm_s);
void motionSetPLCSpeedProfile(speed_profile_t profile);
void motionSetVSMode(bool active);
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction); 

// Encoder feedback integration
void motionEnableEncoderFeedback(bool enable);
bool motionIsEncoderFeedbackEnabled();

// --- PLC I/O Mapping (Extern Declarations, defined in motion_core.cpp) ---
extern const uint8_t AXIS_TO_I73_BIT[]; 
extern const uint8_t AXIS_TO_CONSENSO_BIT[];

#endif
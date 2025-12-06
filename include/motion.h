#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <stdint.h>
#include "system_constants.h"
#include "plc_iface.h" 

#define MOTION_AXES 4
#define MOTION_UPDATE_INTERVAL_MS 10
#define MOTION_CONSENSO_TIMEOUT_MS 5000 

typedef enum {
  SPEED_PROFILE_1 = 0,
  SPEED_PROFILE_2 = 1,
  SPEED_PROFILE_3 = 2
} speed_profile_t;

typedef enum {
  MOTION_IDLE = 0,
  MOTION_WAIT_CONSENSO = 1, 
  MOTION_EXECUTING = 2,
  MOTION_STOPPING = 3,
  MOTION_PAUSED = 4,        
  MOTION_ERROR = 5
} motion_state_t;

typedef struct {
  int32_t position;                   
  int32_t target_position;            
  motion_state_t state;               
  uint32_t last_update_ms;            
  uint32_t state_entry_ms;            
  bool enabled;                       
  
  int32_t soft_limit_min;             
  int32_t soft_limit_max;             
  bool soft_limit_enabled;            
  uint32_t limit_violation_count;     

  int32_t position_at_stop;           
  speed_profile_t saved_speed_profile; 
} motion_axis_t;

void motionInit();
void motionUpdate();

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s);
void motionStop();
void motionPause();
void motionResume();
void motionEmergencyStop();
bool motionClearEmergencyStop();
bool motionIsEmergencyStopped();

int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis);

// --- NEW: Helper to get position in physical units (MM/Deg) ---
float motionGetPositionMM(uint8_t axis); 

motion_state_t motionGetState(uint8_t axis);
bool motionIsMoving();
bool motionIsStalled(uint8_t axis);
uint8_t motionGetActiveAxis(); 

void motionDiagnostics();
void motionPrintStatus();

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos);
void motionEnableSoftLimits(uint8_t axis, bool enable);
bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos);
uint32_t motionGetLimitViolations(uint8_t axis);

bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state);
bool motionSetState(uint8_t axis, motion_state_t new_state);
const char* motionStateToString(motion_state_t state);

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float requested_speed_mm_s);
void motionSetPLCSpeedProfile(speed_profile_t profile);
void motionSetVSMode(bool active);
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction); 

void motionEnableEncoderFeedback(bool enable);
bool motionIsEncoderFeedbackEnabled();

extern const uint8_t AXIS_TO_I73_BIT[]; 
extern const uint8_t AXIS_TO_CONSENSO_BIT[];

#endif
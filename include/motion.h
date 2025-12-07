/**
 * @file motion.h
 * @brief Core Motion Engine Definitions & API (Gemini v3.5.16)
 * @project Gemini v3.5.16
 */

#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <stdint.h>
#include "plc_iface.h"

#define MOTION_AXES 4
#define MOTION_CONSENSO_TIMEOUT_MS 5000 
#define HOMING_SETTLE_MS 1000

// Speed Profiles
typedef enum { SPEED_PROFILE_1=0, SPEED_PROFILE_2=1, SPEED_PROFILE_3=2 } speed_profile_t;

// Motion States
typedef enum { 
  MOTION_IDLE = 0,
  MOTION_WAIT_CONSENSO = 1, 
  MOTION_EXECUTING = 2,
  MOTION_STOPPING = 3,
  MOTION_PAUSED = 4,        
  MOTION_ERROR = 5,
  MOTION_HOMING_APPROACH_FAST = 6,
  MOTION_HOMING_BACKOFF = 7,
  MOTION_HOMING_APPROACH_FINE = 8,
  MOTION_HOMING_SETTLE = 9 
} motion_state_t;

// --- THE AXIS CLASS ---
class Axis {
public:
    Axis(); 
    void init(uint8_t axis_id);
    void updateState(int32_t current_pos, int32_t global_target);
    bool checkSoftLimits(bool strict_mode); 

    // Public State
    uint8_t id;
    int32_t position;                   
    int32_t target_position;            
    motion_state_t state;               
    
    // Configuration
    bool enabled;                       
    int32_t soft_limit_min;             
    int32_t soft_limit_max;             
    bool soft_limit_enabled;            
    
    // Internals
    float commanded_speed_mm_s;
    speed_profile_t saved_speed_profile; 
    int32_t position_at_stop;           
    uint32_t state_entry_ms;            
    int32_t homing_trigger_pos;

private:
    bool _error_logged; 
};

// Global Array
extern Axis axes[MOTION_AXES];

// --- CORE CONTROL API ---
void motionInit();
void motionUpdate();
void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s); 
void motionStop();
void motionPause();   
void motionResume();  
void motionEmergencyStop();
bool motionClearEmergencyStop();
void motionHome(uint8_t axis); 

// --- CONFIGURATION WRAPPERS ---
void motionSetFeedOverride(float factor);
float motionGetFeedOverride();
void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos);
void motionEnableSoftLimits(uint8_t axis, bool enable);
bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos);
void motionSetStrictLimits(bool enable); // <-- Added Setter
void motionEnableEncoderFeedback(bool enable);
bool motionIsEncoderFeedbackEnabled();

// --- DIAGNOSTICS ---
void motionDiagnostics(); 

// --- HELPERS ---
speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed);
void motionSetPLCSpeedProfile(speed_profile_t profile);
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus);

// Hardware Map Arrays
extern const uint8_t AXIS_TO_I73_BIT[];
extern const uint8_t AXIS_TO_CONSENSO_BIT[];

#endif
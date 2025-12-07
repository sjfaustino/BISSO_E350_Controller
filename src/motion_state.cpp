/**
 * @file motion_state.cpp
 * @brief Implementation of Read-Only Motion Accessors (Gemini v3.5.0)
 * @details Adapted for Object-Oriented Axis Architecture.
 */

#include "motion_state.h"
#include "motion.h" // Provides definition of class Axis
#include "encoder_wj66.h"
#include "config_unified.h"
#include "encoder_calibration.h" 
#include "system_constants.h"
#include "safety.h" 
#include <math.h>

// --- Data Ownership Declaration ---
extern Axis axes[MOTION_AXES];
extern uint8_t active_axis;
extern bool global_enabled;

// ============================================================================
// ACCESSOR IMPLEMENTATIONS (Read-Only)
// ============================================================================

int32_t motionGetPosition(uint8_t axis) { 
    return (axis < MOTION_AXES) ? wj66GetPosition(axis) : 0; 
}

int32_t motionGetTarget(uint8_t axis) { 
    return (axis < MOTION_AXES) ? axes[axis].target_position : 0; 
}

motion_state_t motionGetState(uint8_t axis) { 
    return (axis < MOTION_AXES) ? axes[axis].state : MOTION_ERROR; 
}

float motionGetPositionMM(uint8_t axis) {
    if (axis >= MOTION_AXES) return 0.0f;
    int32_t counts = motionGetPosition(axis);
    float scale = 1.0f;
    
    if (axis == 0) scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 1) scale = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 2) scale = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 3) scale = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG;
    
    return (float)counts / scale;
}

bool motionIsMoving() {
  return active_axis != 255 && (axes[active_axis].state == MOTION_EXECUTING || axes[active_axis].state == MOTION_WAIT_CONSENSO);
}

bool motionIsStalled(uint8_t axis) {
  return false; 
}

bool motionIsEmergencyStopped() { 
    return !global_enabled; 
}

uint8_t motionGetActiveAxis() { 
    return active_axis; 
}

// --- MISSING FUNCTION IMPLEMENTATION ---
const char* motionStateToString(motion_state_t state) {
  switch (state) {
    case MOTION_IDLE: return "IDLE";
    case MOTION_WAIT_CONSENSO: return "WAIT";
    case MOTION_EXECUTING: return "RUN";
    case MOTION_STOPPING: return "STOP";
    case MOTION_PAUSED: return "PAUSE";
    case MOTION_ERROR: return "ERR";
    case MOTION_HOMING_APPROACH_FAST: return "HOME:FAST";
    case MOTION_HOMING_BACKOFF: return "HOME:BACK";
    case MOTION_HOMING_APPROACH_FINE: return "HOME:FINE";
    case MOTION_HOMING_SETTLE: return "HOME:SETTLE";
    default: return "UNK";
  }
}
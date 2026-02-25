/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (PosiPro)
 * @details Final Polish: Encapsulation, Configurable Timeouts, Stall Logic.
 * @author Sergio Faustino
 */

#include "auto_report.h"  // PHASE 4.0: M154 auto-report support
#include "board_inputs.h" // PHASE 4.0: M226 board input reading
#include "config_keys.h"
#include "config_cache.h"
#include "encoder_calibration.h"
#include "encoder_motion_integration.h"
#include "encoder_wj66.h"
#include "encoder_deviation.h"
#include "fault_logging.h"
#include "lcd_sleep.h" // PHASE 4.0: M255 LCD sleep support
#include "motion.h"
#include "axis.h"
#include "motion_planner.h"
#include "motion_state.h"
#include "string_safety.h"
#include "motion_buffer.h"       // PHASE 5.11: Check buffer state in motionIsMoving
#include "motion_state_machine.h" // PHASE 5.10: Formal state machine
#include "plc_iface.h"
#include "safety.h"
#include "operator_alerts.h"
#include "serial_logger.h"
#include "spindle_current_monitor.h" // PHASE 5.0: Spindle current monitoring
#include "system_constants.h"
#include "task_manager.h"
#include "web_server.h"
#include "hardware_config.h"
#include "system_tuning.h"
#include "axis_utilities.h" // PHASE 4.1: Centralized units
#include "system_utils.h" // PHASE 8.1
#include "engineering_menu.h" // BOOT button menu
#include "altivar31_modbus.h" // Dual VFD Analog Control
#include "dac_interface.h" // Dual VFD Analog Control
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// STATE OWNERSHIP
// ============================================================================

// FIX: Made static to enforce encapsulation
static Axis axes[MOTION_AXES];

static struct {
  uint8_t active_axis;
  int32_t active_start_position;
  bool global_enabled;
  int strict_limits;
  // Execution Tracking
  char current_command[64];
  float progress_percent;
  float remaining_seconds;
  bool coordinated_mode; // Enable X/Y simultaneous motion
} m_state = {255, 0, true, 1, "", 0.0f, 0.0f, false};

// PHASE 5.10: Non-static to allow external access from motion_state_machine.cpp
portMUX_TYPE motionSpinlock = portMUX_INITIALIZER_UNLOCKED;

// PERFORMANCE AUDIT: Spinlock critical section duration tracking
// Extracted to spinlock_timing.h for maintainability
#include "spinlock_timing.h"

// Convenience macros that bind to motionSpinlock
#define MOTION_SPINLOCK_ENTER(location) SPINLOCK_ENTER(motionSpinlock, location)
#define MOTION_SPINLOCK_EXIT(location) SPINLOCK_EXIT(motionSpinlock, location)

// --- JITTER TRACKING STATE ---
static uint32_t m_max_jitter_us = 0;

// PHASE 6.7: Uses Global Typed Cache (g_config) instead of local cache

// Protocol Mapping: 
// S5 Q 73.2 (Bit 2) = Composite Limit Hit (Correct for PosiPro protocol)
// S5 Q 72.5 (Bit 13) = Backward Limit X
// S5 Q 72.6 (Bit 14) = Left Limit Y
const uint8_t AXIS_TO_I73_BIT[]      = {13, 14, 2, 2}; // X (Rev Limit), Y (Rev Limit), Z (Limit), A (Limit)
const uint8_t AXIS_TO_CONSENSO_BIT[]  = {0, 0, 0, 0};   // Use Q 73.0 (Ready) as global Consensus

// Forward Declarations
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus);
void motionSetPLCSpeedProfile(speed_profile_t profile);
speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed_mm_s);
float calculateHzForSpeed(uint8_t axis, float speed_mm_s);

// ============================================================================
// PUBLIC ACCESSORS FOR AXIS CLASS
// ============================================================================

void motionUpdateExecutionMetrics(float progress, float remaining_seconds) {
  portENTER_CRITICAL(&motionSpinlock);
  m_state.progress_percent = progress;
  m_state.remaining_seconds = remaining_seconds;
  portEXIT_CRITICAL(&motionSpinlock);
}

// PHASE 22: Coordinated Motion Support (C+T Mode)
void motionSetCoordinatedMode(bool enable) {
  portENTER_CRITICAL(&motionSpinlock);
  m_state.coordinated_mode = enable;
  portEXIT_CRITICAL(&motionSpinlock);
}

bool motionIsCoordinatedEnabled() {
  return m_state.coordinated_mode;
}

// ============================================================================
// MAIN CONTROL LOOP
// ============================================================================

void motionInit() {
  logModuleInit("MOTION");

  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].init(i);
    
    // Load soft limits from configuration
    char min_key[20], max_key[20];
    snprintf(min_key, sizeof(min_key), "ax_%d_min", i);
    snprintf(max_key, sizeof(max_key), "ax_%d_max", i);
    axes[i].soft_limit_min = configGetInt(min_key, -1000000);
    axes[i].soft_limit_max = configGetInt(max_key, 1000000);
    
    // Load accumulated distance from NVS
    char d_key[16];
    snprintf(d_key, sizeof(d_key), "dist_%d_m", i);
    axes[i].accumulated_distance_units = (double)configGetFloat(d_key, 0.0f) * 1000.0; // Store as mm internally
    axes[i].maintenance_alert_logged = false;
  }
  motionPlanner.init();
  MotionStateMachine::init(); // PHASE 5.10: Initialize formal state machine
  autoReportInit(); // PHASE 4.0: Initialize auto-report system
  lcdSleepInit();   // PHASE 4.0: Initialize LCD sleep system

  motionSetPLCAxisDirection(255, false, false);
}

void motionUpdate() {
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (!enabled)
    return;

  // PHASE 5.1: Exponential backoff with safety escalation
  static uint32_t consecutive_skips = 0;
  static uint32_t last_timeout_warning_ms = 0;
  static uint8_t backoff_level = 0; // 0=100ms, 1=200ms, 2=400ms, 3=escalate

  // Calculate timeout with exponential backoff
  uint32_t timeout_ms = 100; // Base: 100ms (was 5ms - too aggressive)
  if (backoff_level > 0) {
    timeout_ms = 100 << backoff_level; // 100, 200, 400ms
    if (timeout_ms > 400)
      timeout_ms = 400; // Cap at 400ms
  }

  // PHASE 5.7 Fix: Read consensus input OUTSIDE the mutex to prevent deadlock
  // if I2C bus hangs. This ensures motionUpdate doesn't hold the mutex for
  // 1000ms+.
  uint8_t current_axis = 255;

  // PHASE 5.10: Use spinlock instead of mutex for consistent state protection
  // All writes to m_state.active_axis use spinlock, so reads must too
  portENTER_CRITICAL(&motionSpinlock);
  current_axis = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  // Read I/O if we have an active axis
  if (current_axis < MOTION_AXES) {
    // PHASE 17: I2C Optimization - Read full input byte once per loop
    // This reduces bus traffic from 2-3 reads to 1 read per 10ms
    elboI73Refresh();
  }

  if (!taskLockMutex(taskGetMotionMutex(), timeout_ms)) {
    consecutive_skips++;

    // Increase backoff level after repeated failures
    if (consecutive_skips >= 3) {
      backoff_level = 1;
    }
    if (consecutive_skips >= 10) {
      backoff_level = 2;
    }

    // Log warning after threshold
    uint32_t now = millis();
    if ((uint32_t)(now - last_timeout_warning_ms) >=
        5000) { // Log once per 5 seconds
      logWarning(
          "[MOTION] Mutex timeout (%lums): Skipped %lu times, backoff level %u",
          (unsigned long)timeout_ms, (unsigned long)consecutive_skips,
          backoff_level);

      // Format fault message with actual values (faultLogWarning doesn't
      // support printf-style formatting)
      char fault_msg[128];
      snprintf(
          fault_msg, sizeof(fault_msg),
          "Motion mutex timeout: %lu consecutive failures, backoff level %u",
          (unsigned long)consecutive_skips, backoff_level);
      faultLogWarning(FAULT_MOTION_TIMEOUT, fault_msg);
      last_timeout_warning_ms = now;
    }

    // Escalate to emergency stop if critical timeout threshold reached
    if (consecutive_skips >= 20) {
      logError("[MOTION] CRITICAL: Motion mutex timeout escalation!");
      faultLogCritical(
          FAULT_MOTION_TIMEOUT,
          "Motion mutex critical failure - escalating to emergency stop");
      motionEmergencyStop();
      consecutive_skips = 0;
      backoff_level = 0;
    }

    return;
  }

  // Successfully acquired mutex - reset backoff
  if (consecutive_skips > 0) {
    consecutive_skips = 0;
    backoff_level = 0;
  }

  // PHASE 6.7: Multi-key lookups replaced by atomic struct access
  int strict_mode = g_config.strict_limits;
  float target_margin_mm = g_config.target_margin_mm;

  for (int i = 0; i < MOTION_AXES; i++) {
    int32_t raw_pos = wj66GetPosition(i);
    axes[i].position = raw_pos;
    
    // Extrapolate position based on last known velocity and time since last RS485 update
    if (axes[i].last_actual_update_ms > 0) {
        uint32_t now = millis();
        uint32_t dt_since_last_actual = (now >= axes[i].last_actual_update_ms) 
            ? (now - axes[i].last_actual_update_ms) 
            : (UINT32_MAX - axes[i].last_actual_update_ms + now + 1);
        
        // Extended encoder silence warning (diagnostic aid)
        // Check BEFORE clamping so the 2s threshold works correctly
        if (dt_since_last_actual >= 2000) {
            if (!axes[i].prediction_stale_logged) {
                logWarning("[MOTION] Axis %d: Encoder silence >2s - prediction capped", i);
                axes[i].prediction_stale_logged = true;
            }
        } else {
            axes[i].prediction_stale_logged = false;  // Reset when data resumes
        }
        
        // Clamp extrapolation time to 200ms to prevent runaway if bus is lost
        if (dt_since_last_actual > 200) dt_since_last_actual = 200;
        
        int32_t offset = (int32_t)(axes[i].velocity_counts_ms * (float)dt_since_last_actual);
        
        // Safety: Limit prediction offset to the system's allowed target margin.
        // Convert mm threshold to axis-specific counts using active scale.
        float scale = getAxisScale(i);
        int32_t max_offset = (int32_t)(target_margin_mm * scale);
        
        // Ensure max_offset is at least 1 count to allow any prediction
        if (max_offset < 1) max_offset = 1;

        if (offset > max_offset) offset = max_offset;
        if (offset < -max_offset) offset = -max_offset;
        
        axes[i].predicted_position = axes[i].last_actual_position + offset;
    } else {
        axes[i].predicted_position = raw_pos;
    }

    if (axes[i].checkSoftLimits(strict_mode)) {
      motionEmergencyStop();
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
  }

  motionPlanner.update(axes, m_state.active_axis,
                       m_state.active_start_position);

  bool any_axis_active = false;
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state != MOTION_IDLE) {
        any_axis_active = true;
        bool axis_consensus = elboI73GetInput(AXIS_TO_CONSENSO_BIT[i]);
        axes[i].updateState(axes[i].position, axes[i].target_position, axis_consensus);
    }
  }

  if (!any_axis_active) {
    portENTER_CRITICAL(&motionSpinlock);
    m_state.active_axis = 255;
    portEXIT_CRITICAL(&motionSpinlock);
  }

  taskUnlockMutex(taskGetMotionMutex());

  // PHASE 5.3: Periodic Distance Persistence & Maintenance Checks
  // Optimization: Only save if distance delta > 100m or 1 hour has passed to preserve Flash health
  static uint32_t last_dist_save_ms = 0;
  static double last_saved_dist[3] = {0, 0, 0};
  static bool first_run = true;

  if (first_run) {
      for (int i = 0; i < 3; i++) last_saved_dist[i] = axes[i].accumulated_distance_units;
      first_run = false;
  }

  uint32_t now = millis();
  bool should_save = (now - last_dist_save_ms > 3600000); // Hourly save fallback
  

  
  if (!should_save) {
      for (int i = 0; i < 3; i++) {
          if (abs(axes[i].accumulated_distance_units - last_saved_dist[i]) > 100000.0) { // 100,000mm = 100m
              should_save = true;
              break;
          }
      }
  }

  if (should_save) {
      last_dist_save_ms = now;
      bool changed = false;
      for (int i = 0; i < 3; i++) {
          float dist_m = (float)(axes[i].accumulated_distance_units / 1000.0);
          
          // Only update config if there's a practical change
          if (abs(axes[i].accumulated_distance_units - last_saved_dist[i]) > 1000.0) { // 1m resolution for NVS
              char d_key[16];
              snprintf(d_key, sizeof(d_key), "dist_%d_m", i);
              configSetFloat(d_key, dist_m);
              last_saved_dist[i] = axes[i].accumulated_distance_units;
              changed = true;
          }

          // Maintenance Check: Default 50km (50000m)
          float maint_threshold = 50000.0f; 
          bool warned = (dist_m > maint_threshold);
          webServer.setAxisMaintenanceWarning(i, warned);
          
          if (warned && !axes[i].maintenance_alert_logged) {
              logWarning("[MAINTENANCE] Axis %d reached service interval (%.1f km)", i, dist_m / 1000.0f);
              axes[i].maintenance_alert_logged = true;
              statusLightSetState(SYSTEM_STATE_PAUSED);
              faultLogWarning(FAULT_SOFT_LIMIT_EXCEEDED, "Service Interval Reached");
          }
      }
      if (changed) {
          configUnifiedSave();
      }
  }

  // PHASE 4.0: Check if auto-report interval elapsed (non-blocking)
  autoReportUpdate();

  // PHASE 4.0: Check if LCD sleep timeout elapsed (non-blocking)
  lcdSleepUpdate();

  // PHASE 5.0: Update spindle current monitoring (non-blocking, includes safety
  // shutdown)
  spindleMonitorUpdate();

  // SAFETY & LOCKOUT (PHASE 8.6)
  // If PosiPro is IDLE, ensure VFDs return control back to the PLC Analog Terminal
  if (m_state.active_axis == 255) {
      AltivarX.setModbusPriority(false);
      AltivarYZA.setModbusPriority(false);
      
      // Also ensure DAC is at 0V just in case relay logic fails
      if (dacIsAnalogEnabled()) {
          dacSetFrequency(0, 0.0f);
          dacSetFrequency(1, 0.0f);
      }
  }
}

// ============================================================================
// PUBLIC ACCESSORS
// ============================================================================

// FIX: Read-only Accessor Implementation
const Axis *motionGetAxis(uint8_t axis) {
  return (axis < MOTION_AXES) ? &axes[axis] : nullptr;
}

int32_t motionGetPosition(uint8_t axis) {
  if (axis >= MOTION_AXES) return 0;

  // PHASE 5.10: Use spinlock to protect 32-bit position read (prevent torn reads)
  portENTER_CRITICAL(&motionSpinlock);
  // Return the predicted position for UI and external consumers
  int32_t pos = axes[axis].predicted_position;
  portEXIT_CRITICAL(&motionSpinlock);

  return pos;
}

int32_t motionGetTarget(uint8_t axis) {
  if (axis >= MOTION_AXES) return 0;

  // PHASE 5.10: Use spinlock to protect 32-bit target read (prevent torn reads)
  portENTER_CRITICAL(&motionSpinlock);
  int32_t target = axes[axis].target_position;
  portEXIT_CRITICAL(&motionSpinlock);

  return target;
}

motion_state_t motionGetState(uint8_t axis) {
  if (axis >= MOTION_AXES)
    return MOTION_ERROR;

  // CRITICAL FIX: Use spinlock to protect state read
  portENTER_CRITICAL(&motionSpinlock);
  motion_state_t s = axes[axis].state;
  portEXIT_CRITICAL(&motionSpinlock);

  return s;
}

float motionGetAxisScale(uint8_t axis) {
  if (axis >= MOTION_AXES) return 1.0f;

  if (axis == 3) { // Rotary axis
      return (machineCal.axes[axis].pulses_per_degree > 0)
                 ? machineCal.axes[axis].pulses_per_degree
                 : (float)MOTION_POSITION_SCALE_FACTOR_DEG;
  } else { // Linear axes (0, 1, 2)
      return (machineCal.axes[axis].pulses_per_mm > 0)
                 ? machineCal.axes[axis].pulses_per_mm
                 : (float)MOTION_POSITION_SCALE_FACTOR;
  }
}

float motionGetPositionMM(uint8_t axis) {
  if (axis >= MOTION_AXES)
    return 0.0f;

  // PHASE 5.10: Use spinlock to protect position read
  portENTER_CRITICAL(&motionSpinlock);
  int32_t counts = axes[axis].position;
  portEXIT_CRITICAL(&motionSpinlock);

  return (float)counts / motionGetAxisScale(axis);
}

float motionGetVelocity(uint8_t axis) {
  if (axis >= MOTION_AXES)
    return 0.0f;

  // Return current velocity in mm/s (calculated in updateState)
  return axes[axis].current_velocity_mm_s;
}

bool motionIsMoving() {
  // CRITICAL FIX: Use spinlock to protect state read
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t ax = m_state.active_axis;
  motion_state_t s = (ax < MOTION_AXES) ? axes[ax].state : MOTION_ERROR;
  portEXIT_CRITICAL(&motionSpinlock);

  if (ax >= MOTION_AXES)
    return false;
  return (s == MOTION_EXECUTING || s == MOTION_WAIT_CONSENSO ||
          s == MOTION_HOMING_APPROACH_FAST || s == MOTION_HOMING_BACKOFF ||
          s == MOTION_HOMING_APPROACH_FINE);
}

// FIX: Delegating to encoder integration instead of hardcoded false
bool motionIsStalled(uint8_t axis) { return encoderMotionHasError(axis); }

// PHASE 5.10: Use spinlock for thread-safe global_enabled read
bool motionIsEmergencyStopped() {
  portENTER_CRITICAL(&motionSpinlock);
  bool disabled = !m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);
  return disabled;
}

// PHASE 5.10: Use spinlock for thread-safe active_axis read
uint8_t motionGetActiveAxis() {
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t axis = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);
  return axis;
}

// PHASE 5.10: Accessor for active_start_position (for state machine)
int32_t motionGetActiveStartPosition() {
  portENTER_CRITICAL(&motionSpinlock);
  int32_t pos = m_state.active_start_position;
  portEXIT_CRITICAL(&motionSpinlock);
  return pos;
}

// PHASE 5.10: Setter for active_start_position (for state machine)
void motionSetActiveStartPosition(int32_t position) {
  portENTER_CRITICAL(&motionSpinlock);
  m_state.active_start_position = position;
  portEXIT_CRITICAL(&motionSpinlock);
}

// PHASE 5.10: Clear active axis (for state machine IDLE transition)
void motionClearActiveAxis() {
  portENTER_CRITICAL(&motionSpinlock);
  m_state.active_axis = 255;
  m_state.progress_percent = 0.0f;
  m_state.remaining_seconds = 0.0f;
  m_state.current_command[0] = '\0';
  portEXIT_CRITICAL(&motionSpinlock);
}

// Execution Status Accessors
float motionGetExecutionProgress() {
    portENTER_CRITICAL(&motionSpinlock);
    float p = m_state.progress_percent;
    portEXIT_CRITICAL(&motionSpinlock);
    return p;
}

void motionGetCurrentCommand(char* buf, size_t len) {
    if (!buf || len == 0) return;
    portENTER_CRITICAL(&motionSpinlock);
    SAFE_STRCPY(buf, m_state.current_command, len);
    portEXIT_CRITICAL(&motionSpinlock);
}

float motionGetEstimatedTimeRemaining() {
    portENTER_CRITICAL(&motionSpinlock);
    float t = m_state.remaining_seconds;
    portEXIT_CRITICAL(&motionSpinlock);
    return t;
}

void motionSetCurrentCommand(const char* cmd) {
    if(!cmd) return;
    portENTER_CRITICAL(&motionSpinlock);
    SAFE_STRCPY(m_state.current_command, cmd, sizeof(m_state.current_command));
    portEXIT_CRITICAL(&motionSpinlock);
}

const char *motionStateToString(motion_state_t state) {
  switch (state) {
  case MOTION_IDLE:
    return "IDLE";
  case MOTION_WAIT_CONSENSO:
    return "WAIT";
  case MOTION_EXECUTING:
    return "RUN";
  case MOTION_STOPPING:
    return "STOP";
  case MOTION_PAUSED:
    return "PAUSE";
  case MOTION_ERROR:
    return "ERR";
  case MOTION_HOMING_APPROACH_FAST:
    return "H:FAST";
  case MOTION_HOMING_BACKOFF:
    return "H:BACK";
  case MOTION_HOMING_APPROACH_FINE:
    return "H:FINE";
  case MOTION_HOMING_SETTLE:
    return "H:ZERO";
  default:
    return "UNK";
  }
}

// ============================================================================
// CONTROL API
// ============================================================================

bool motionHome(uint8_t axis) {
  if (axis >= MOTION_AXES)
    return false;
  if (!taskLockMutex(taskGetMotionMutex(), 100))
    return false;

  // PHASE 5.10: Use spinlock for m_state access (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t current_active = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  if (current_active != 255) {
    taskUnlockMutex(taskGetMotionMutex());
    return false;
  }

  logInfo("[HOME] Axis %d Start", axis);

  portENTER_CRITICAL(&motionSpinlock);
  m_state.active_axis = axis;
  portEXIT_CRITICAL(&motionSpinlock);

  // PHASE 5.10: Use formal state machine for state transitions
  MotionStateMachine::transitionTo(&axes[axis], MOTION_HOMING_APPROACH_FAST);

  int fast_prof = configGetInt(KEY_HOME_PROFILE_FAST, 2);

  taskUnlockMutex(taskGetMotionMutex());

  // I2C calls moved outside mutex - wrapped in transaction for performance
  plcBeginTransaction();
  motionSetPLCSpeedProfile((speed_profile_t)fast_prof);
  motionSetPLCAxisDirection(axis, true, false);
  plcEndTransaction();

  return true;
}

bool motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (!enabled) {
    logError("[MOTION] Disabled");
    return false;
  }
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] Busy (Mutex)");
    return false;
  }

  float targets[] = {x, y, z, a};

  uint8_t target_axis = 255;
  int32_t target_pos = 0;
  int cnt = 0;

  for (int i = 0; i < MOTION_AXES; i++) {
    int32_t t = (int32_t)(targets[i] * motionGetAxisScale(i));
    if (abs(t - wj66GetPosition(i)) > 1) {
      cnt++;
      target_axis = i;
      target_pos = t;
    }
  }

  // Relax check for coordinated X/Y motion
  bool is_coord_xy = (m_state.coordinated_mode && cnt == 2 && 
                      (targets[0] != motionGetPositionMM(0)) && 
                      (targets[1] != motionGetPositionMM(1)));

  if ((cnt > 1 && !is_coord_xy) || cnt == 0 || m_state.active_axis != 255) {
    taskUnlockMutex(taskGetMotionMutex());
    return false;
  }

  axes[target_axis].commanded_speed_mm_s = speed_mm_s;

  if (axes[target_axis].soft_limit_enabled) {
    if (target_pos < axes[target_axis].soft_limit_min ||
        target_pos > axes[target_axis].soft_limit_max) {
      logError("[MOTION] Target Limit Violation");
      taskUnlockMutex(taskGetMotionMutex());
      return false;
    }
  }

  axes[target_axis].target_position = target_pos;
  axes[target_axis].position_at_stop = motionGetPosition(target_axis);

  float eff_spd = speed_mm_s * motionPlanner.getFeedOverride();
  speed_profile_t prof = motionMapSpeedToProfile(target_axis, eff_spd);
  axes[target_axis].saved_speed_profile = prof;

  // Calculate direction but don't set PLC yet
  bool is_fwd = (target_pos > axes[target_axis].position);

  // PHASE 5.10: Use spinlock for m_state writes (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  m_state.active_axis = target_axis;
  m_state.active_start_position = axes[target_axis].position;
  portEXIT_CRITICAL(&motionSpinlock);

  if (is_coord_xy) {
      MotionStateMachine::transitionTo(&axes[0], MOTION_WAIT_CONSENSO);
      MotionStateMachine::transitionTo(&axes[1], MOTION_WAIT_CONSENSO);
      axes[0].target_position = (int32_t)(targets[0] * motionGetAxisScale(0));
      axes[1].target_position = (int32_t)(targets[1] * motionGetAxisScale(1));
      axes[0].commanded_speed_mm_s = speed_mm_s;
      axes[1].commanded_speed_mm_s = speed_mm_s;
      
      bool is_fwd_x = (axes[0].target_position > axes[0].position);

      plcBeginTransaction();
      // PHASE 8.6: Use precise Modbus velocities for coordinated motion
      float hz_x = calculateHzForSpeed(0, speed_mm_s);
      float hz_y = calculateHzForSpeed(1, speed_mm_s);
      motionSetVFDVelocities(hz_x, hz_y);
      
      plcSetAxisSelect(0xFF); // Combined X+Y
      plcSetDirection(is_fwd_x); 
      plcEndTransaction();
  } else {
      // PHASE 5.10: Use formal state machine for state transitions
      MotionStateMachine::transitionTo(&axes[target_axis], MOTION_WAIT_CONSENSO);

      // PHASE 8.6: Use precise Modbus velocity for single-axis moves
      float target_hz = calculateHzForSpeed(target_axis, eff_spd);
      
      plcBeginTransaction();
      // Write frequency before enabling direction to avoid torque jerks
      if (target_axis == 0) { // X Axis
          motionSetVFDVelocities(target_hz, 0.0f);
      } else { // Y1, Z2, A3
          motionSetVFDVelocities(0.0f, target_hz);
      }
      
      // Safety: Also set the legacy PLC speed profile as a fallback
      motionSetPLCSpeedProfile(prof);
      
      motionSetPLCAxisDirection(target_axis, true, is_fwd);
      plcEndTransaction();
  }

  taskSignalMotionUpdate();
  return true;
}

// ============================================================================
// WRAPPERS AND HELPERS
// ============================================================================

void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus) {
  plcBeginTransaction();
  if (!enable || axis >= MOTION_AXES) {
    // Stop all motion and clear handshakes
    plcClearAllOutputs();
  } else {
    // Set Axis and Direction on Bank 2 (I 72)
    plcSetAxisSelect(axis);
    plcSetDirection(is_plus);

    // ELBO PROTOCOL: Must hold Master Enable (I 73.7) and System Ready (I 72.7)
    // Bank 1 (0x24): Bit 7 is I 73.7
    elboQ73SetRelay(PLC_OUT_MASTER_ENABLE, true);
    
    // Bank 2 (0x25): Bit 7 is I 72.7 (Handshake required by PB30/20)
    elboQ73SetRelay(8 + PLC_OUT_SYSTEM_READY, true);
  }
  plcEndTransaction();
}

void motionSetPLCSpeedProfile(speed_profile_t profile) {
  // Legacy discrete profile selection
  plcSetSpeed((uint8_t)profile);
  
  // PHASE 8.5: If analog control enabled, also update VFD DAC frequencies
  if (dacIsAnalogEnabled()) {
      // In single-axis mode (legacy), we use the commanded axis to set BOTH VFDs to the same speed
      // as a safety fallback, or map specifically if we know the axis.
      // We'll improve this in the coordinate motion dispatcher.
      float target_hz = 0.0f;
      if (profile == SPEED_PROFILE_1) target_hz = configGetFloat("vfd_hz_slow", 15.0f);
      else if (profile == SPEED_PROFILE_2) target_hz = configGetFloat("vfd_hz_med", 30.0f);
      else target_hz = configGetFloat("vfd_hz_fast", 50.0f);
      
      dacSetFrequency(0, target_hz); // VFD1 (X)
      dacSetFrequency(1, target_hz); // VFD2 (YZA)
  }
}

/**
 * @brief Sets independent frequencies for Dual VFD configuration
 */
void motionSetVFDVelocities(float freq1_hz, float freq2_hz) {
    // Take Modbus Priority to override the shared analog wire and/or PLC inputs
    AltivarX.setModbusPriority(true);
    AltivarYZA.setModbusPriority(true);
    
    // Write frequencies via Modbus
    AltivarX.writeFrequency(freq1_hz);
    AltivarYZA.writeFrequency(freq2_hz);
    
    // Fallback DAC for X-VFD (Analog monitoring or legacy wiring)
    if (dacIsAnalogEnabled()) {
        dacSetFrequency(0, freq1_hz); 
    }
}

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed_mm_s) {
    if (axis >= MOTION_AXES) return SPEED_PROFILE_1;

    // Convert speed_mm_s to mm_min for comparison with calibration
    float speed_mm_min = speed_mm_s * 60.0f;

    AxisCalibration* cal = &machineCal.axes[axis];

    // Find the profile whose calibrated speed is closest to the requested speed
    float d1 = fabsf(speed_mm_min - cal->speed_slow_mm_min);
    float d2 = fabsf(speed_mm_min - cal->speed_med_mm_min);
    float d3 = fabsf(speed_mm_min - cal->speed_fast_mm_min);

    if (d1 <= d2 && d1 <= d3) return SPEED_PROFILE_1;
    if (d2 <= d1 && d2 <= d3) return SPEED_PROFILE_2;
    return SPEED_PROFILE_3;
}

float motionGetCalibratedFeedRate(uint8_t axis, float speed_mm_s) {
    speed_profile_t prof = motionMapSpeedToProfile(axis, speed_mm_s);
    
    if (axis >= MOTION_AXES) return 0.0f;
    
    AxisCalibration* cal = &machineCal.axes[axis];

    if (prof == SPEED_PROFILE_1) return cal->speed_slow_mm_min;
    if (prof == SPEED_PROFILE_2) return cal->speed_med_mm_min;
    return cal->speed_fast_mm_min;
}

float calculateHzForSpeed(uint8_t axis, float speed_mm_s) {
    if (axis >= MOTION_AXES) return 0.0f;
    
    // Max calibrated speed for this axis (usually Speed Fast)
    float max_mm_min = machineCal.axes[axis].speed_fast_mm_min;
    if (max_mm_min < 1.0f) max_mm_min = 2400.0f;
    
    float target_mm_min = speed_mm_s * 60.0f;
    float max_hz = configGetFloat("vfd_max_hz", 50.0f);
    
    // Freq = (Target / Max) * MaxHz
    float hz = (target_mm_min / max_mm_min) * max_hz;
    
    // Clamp to range
    if (hz < 5.0f && target_mm_min > 0.1f) hz = 5.0f; // Minimum torque freq
    if (hz > max_hz) hz = max_hz;
    
    return hz;
}

bool motionMoveRelative(float dx, float dy, float dz, float da,
                        float speed_mm_s) {
  float cur_x = motionGetPositionMM(0);
  float cur_y = motionGetPositionMM(1);
  float cur_z = motionGetPositionMM(2);
  float cur_a = motionGetPositionMM(3);
  return motionMoveAbsolute(cur_x + dx, cur_y + dy, cur_z + dz, cur_a + da,
                            speed_mm_s);
}

bool motionJog(float dx, float dy, float dz, float da, float speed_mm_s) {
  // Web API jog command - logs intent and calls motionMoveRelative
  logInfo("[JOG] Request: X=%.2f Y=%.2f Z=%.2f A=%.2f @ %.1f mm/s", 
          dx, dy, dz, da, speed_mm_s);
  return motionMoveRelative(dx, dy, dz, da, speed_mm_s);
}

bool motionSetPosition(float x, float y, float z, float a) {
  // G92 - Set current position without moving
  // This is used for calibration and work offset setting
  // IMPORTANT: Only call when axes are idle

  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] Cannot set position - mutex locked");
    return false;
  }

  // Check if any axis is currently moving
  if (m_state.active_axis != 255) {
    taskUnlockMutex(taskGetMotionMutex());
    logError("[MOTION] Cannot set position - axis %d is active",
             m_state.active_axis);
    return false;
  }

  // Convert mm to counts for each axis
  float positions[] = {x, y, z, a};

  // Set positions for all axes
  for (uint8_t i = 0; i < MOTION_AXES; i++) {
    int32_t new_pos = (int32_t)(positions[i] * motionGetAxisScale(i));
    axes[i].position = new_pos;
    axes[i].target_position = new_pos;
    logInfo("[MOTION] Axis %d position set to %.3f mm (%ld counts)", i,
            positions[i], (long)new_pos);
  }

  taskUnlockMutex(taskGetMotionMutex());
  return true;
}

void motionSetFeedOverride(float factor) {
  motionPlanner.setFeedOverride(factor);
}
float motionGetFeedOverride() { return motionPlanner.getFeedOverride(); }

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
  if (axis < MOTION_AXES) {
    axes[axis].soft_limit_min = min_pos;
    axes[axis].soft_limit_max = max_pos;
  }
}

void motionSetStrictLimits(bool enable) {
  m_state.strict_limits = enable ? 1 : 0;
  configSetInt(KEY_MOTION_STRICT_LIMITS, m_state.strict_limits);
  logInfo("[MOTION] Strict Limits: %s", enable ? "ON" : "OFF");
}

void motionEnableSoftLimits(uint8_t axis, bool enable) {
  if (axis < MOTION_AXES) {
    if (axes[axis].state != MOTION_IDLE) {
      logError("[MOTION] Reject Limit Config: Axis %d Busy", axis);
      return;
    }
    // PHASE 5.10: Use spinlock for thread-safe global_enabled check
    portENTER_CRITICAL(&motionSpinlock);
    bool enabled = m_state.global_enabled;
    portEXIT_CRITICAL(&motionSpinlock);

    if (enabled) {
      logError(
          "[MOTION] Reject Limit Config: System must be Disabled (E-Stop)");
      return;
    }
    axes[axis].soft_limit_enabled = enable;
    logInfo("[MOTION] Soft Limits Axis %d: %s", axis, enable ? "ON" : "OFF");
  }
}

bool motionGetSoftLimits(uint8_t axis, int32_t *min_pos, int32_t *max_pos) {
  if (axis >= MOTION_AXES)
    return false;
  *min_pos = axes[axis].soft_limit_min;
  *max_pos = axes[axis].soft_limit_max;
  return axes[axis].soft_limit_enabled;
}

void motionEnableEncoderFeedback(bool enable) {
  encoderMotionEnableFeedback(enable);
}

bool motionIsEncoderFeedbackEnabled() {
  return encoderMotionIsFeedbackActive();
}

bool motionStop() {
  if (!taskLockMutex(taskGetMotionMutex(), 100))
    return false;

  // PHASE 5.10: Use spinlock for m_state read (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t active = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  if (active != 255) {
    // PHASE 5.10: Use formal state machine for state transitions
    axes[active].target_position = axes[active].position;
    axes[active].position_at_stop = axes[active].position;
    MotionStateMachine::transitionTo(&axes[active], MOTION_STOPPING);
  }
  taskUnlockMutex(taskGetMotionMutex());

  // I2C calls moved outside mutex
  motionSetPLCAxisDirection(255, false, false);

  taskSignalMotionUpdate();
  return true;
}

bool motionPause() {
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (!enabled)
    return false;
  if (!taskLockMutex(taskGetMotionMutex(), 100))
    return false;

  // PHASE 5.10: Use spinlock for m_state read (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t active = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  bool valid_pause = false;
  if (active != 255 &&
      (axes[active].state == MOTION_EXECUTING ||
       axes[active].state == MOTION_WAIT_CONSENSO)) {
    // PHASE 5.10: Use formal state machine for state transitions
    MotionStateMachine::transitionTo(&axes[active], MOTION_PAUSED);
    logInfo("[MOTION] Paused axis %d", active);
    valid_pause = true;
  }

  taskUnlockMutex(taskGetMotionMutex());

  // I2C calls moved outside mutex
  if (valid_pause) {
    motionSetPLCAxisDirection(255, false, false);
  }

  taskSignalMotionUpdate();
  return true;
}

bool motionResume() {
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (!enabled)
    return false;
  if (!taskLockMutex(taskGetMotionMutex(), 100))
    return false;

  // PHASE 5.10: Use spinlock for m_state read (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t axis = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  speed_profile_t prof = SPEED_PROFILE_1;
  bool is_fwd = false;

  if (axis != 255 && axes[axis].state == MOTION_PAUSED) {
    float effective_speed =
        axes[axis].commanded_speed_mm_s * motionPlanner.getFeedOverride();
    prof = motionMapSpeedToProfile(axis, effective_speed);
    
    // PHASE 8.6: Calculate precise frequency for resumption
    float target_hz = calculateHzForSpeed(axis, effective_speed);

    is_fwd = (axes[axis].target_position > axes[axis].position);

    // PHASE 5.10: Use formal state machine for state transitions
    MotionStateMachine::transitionTo(&axes[axis], MOTION_WAIT_CONSENSO);
    
    plcBeginTransaction();
    if (axis == 0) {
        motionSetVFDVelocities(target_hz, 0.0f);
    } else {
        motionSetVFDVelocities(0.0f, target_hz);
    }
    motionSetPLCSpeedProfile(prof); // Fallback
    motionSetPLCAxisDirection(axis, true, is_fwd);
    plcEndTransaction();
  }

  taskSignalMotionUpdate();
  return true;
}

bool motionDwell(uint32_t ms) {
  // Non-blocking dwell command for G4 gcode
  // Uses one axis (axis 0) as the dwell controller
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (!enabled)
    return false;
  if (!taskLockMutex(taskGetMotionMutex(), 100))
    return false;

  // PHASE 5.10: Use spinlock for m_state access (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t active = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  // Only execute dwell if no motion is active
  if (active == 255 && axes[0].state == MOTION_IDLE) {
    // PHASE 5.10: Use formal state machine for state transitions
    axes[0].dwell_end_ms = millis() + ms;
    MotionStateMachine::transitionTo(&axes[0], MOTION_DWELL);

    portENTER_CRITICAL(&motionSpinlock);
    m_state.active_axis = 0; // Mark axis 0 as "active" during dwell
    portEXIT_CRITICAL(&motionSpinlock);

    logInfo("[MOTION] Dwell: %lu ms (end at %lu)", (unsigned long)ms,
            (unsigned long)axes[0].dwell_end_ms);
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
    return true;
  }

  taskUnlockMutex(taskGetMotionMutex());
  return false; // Cannot dwell while motion is active
}

bool motionWaitPin(uint8_t pin_id, uint8_t pin_type, uint8_t state,
                   uint32_t timeout_sec) {
  // Non-blocking pin state wait command for M226 gcode
  // Uses axis 0 as the wait controller
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (!enabled)
    return false;
  if (!taskLockMutex(taskGetMotionMutex(), 100))
    return false;

  // PHASE 5.10: Use spinlock for m_state access (consistent lock domain)
  portENTER_CRITICAL(&motionSpinlock);
  uint8_t active = m_state.active_axis;
  portEXIT_CRITICAL(&motionSpinlock);

  // Only execute pin wait if no motion is active
  if (active == 255 && axes[0].state == MOTION_IDLE) {
    // PHASE 5.10: Use formal state machine for state transitions
    axes[0].wait_pin_id = pin_id;
    axes[0].wait_pin_type = pin_type;
    axes[0].wait_pin_state = state;
    axes[0].wait_pin_timeout_ms = (timeout_sec > 0) ? (timeout_sec * 1000) : 0;
    MotionStateMachine::transitionTo(&axes[0], MOTION_WAIT_PIN);

    portENTER_CRITICAL(&motionSpinlock);
    m_state.active_axis = 0; // Mark axis 0 as "active" during wait
    portEXIT_CRITICAL(&motionSpinlock);

    logInfo("[MOTION] Wait for pin: id=%d type=%d state=%d timeout=%lu sec",
            pin_id, pin_type, state, (unsigned long)timeout_sec);
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
    return true;
  }

  taskUnlockMutex(taskGetMotionMutex());
  return false; // Cannot wait while motion is active
}

void motionEmergencyStop() {
  // PHASE 5.7: E-Stop Latency Monitoring (Recommendation)
  // Track E-Stop response time to detect priority inversion issues
  // Target: <50ms (ISO 13849 PLd), Warn if >50ms
  uint32_t estop_start_us = micros();

  // CRITICAL: Deadlock Prevention (Code Audit)
  // Use 10ms timeout to prevent deadlock if Motion task holds mutex while
  // blocked on I2C If timeout occurs, E-stop still succeeds via hardware PLC
  // I/O (independent of mutex) See: docs/PosiPro_FINAL_AUDIT.md for complete
  // deadlock analysis
  bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10);

  // PRIMARY SAFETY: Disable all axes at hardware level (PLC I/O)
  // This does NOT require motion_mutex - uses taskGetI2cPlcMutex() instead
  // Ensures axes stop even if mutex unavailable
  motionSetPLCAxisDirection(255, false, false);

  portENTER_CRITICAL(&motionSpinlock);
  m_state.global_enabled = false;
  m_state.active_axis = 255;
  portEXIT_CRITICAL(&motionSpinlock);

  // PHASE 5.10: Use formal state machine for state transitions
  for (int i = 0; i < MOTION_AXES; i++) {
    MotionStateMachine::transitionTo(&axes[i], MOTION_ERROR);
  }

  motionBuffer.clear();
  autoReportDisable(); // PHASE 4.0: Disable auto-report on E-Stop
  lcdSleepWakeup();    // PHASE 4.0: Wake display on E-Stop for visibility
  if (got_mutex)
    taskUnlockMutex(taskGetMotionMutex());

  // PHASE 5.7: E-Stop Latency Monitoring (Recommendation)
  // Measure and log E-Stop response time
  // Safety limits: IEC 61508 SIL2 (<100ms), ISO 13849 PLd (<50ms)
  uint32_t estop_latency_us = micros() - estop_start_us;
  if (estop_latency_us > 50000) { // >50ms (50,000 microseconds)
    logWarning("[MOTION] [SAFETY] E-Stop latency high: %lu us (%.1f ms) - "
               "Target: <50ms",
               (unsigned long)estop_latency_us, estop_latency_us / 1000.0f);
  }

  logError("[MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED (Latency: %.1f ms)",
           estop_latency_us / 1000.0f);
  faultLogError(FAULT_EMERGENCY_HALT, "E-Stop Activated");
  taskSignalMotionUpdate();
}

bool motionClearEmergencyStop() {
  // PHASE 5.10: Use spinlock for thread-safe global_enabled check
  portENTER_CRITICAL(&motionSpinlock);
  bool enabled = m_state.global_enabled;
  portEXIT_CRITICAL(&motionSpinlock);

  if (enabled) {
    logInfo("[MOTION] E-Stop already cleared");
    return true;
  }
  if (safetyIsAlarmed()) {
    logWarning("[MOTION] Cannot clear - Alarm Active");
    return false;
  }

  portENTER_CRITICAL(&motionSpinlock);
  m_state.global_enabled = true;
  m_state.active_axis = 255;

  // PHASE 5.10: Use formal state machine for state transitions
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state == MOTION_ERROR) {
      MotionStateMachine::transitionTo(&axes[i], MOTION_IDLE);
    }
  }
  portEXIT_CRITICAL(&motionSpinlock);

  emergencyStopSetActive(false);
  logInfo("[MOTION] [OK] Emergency stop cleared");
  taskSignalMotionUpdate();
  return true;
}

void motionDiagnostics() {
  logPrintf("\n[MOTION] State: %s | Active: %d\n",
                m_state.global_enabled ? "ON" : "ESTOP", m_state.active_axis);
  for (int i = 0; i < MOTION_AXES; i++) {
    logPrintf("  Axis %d: Pos=%ld | Tgt=%ld | State=%s\n", i,
                  (long)axes[i].position, (long)axes[i].target_position,
                  motionStateToString(axes[i].state));
  }
}

#if ENABLE_SPINLOCK_TIMING
void motionPrintSpinlockStats() {
  logPrintln("\n[MOTION] === Spinlock Critical Section Timing Report ===");
  logPrintln("Location                       | Count      | Max (us) | >10us | Status");
  logPrintln("-------------------------------|------------|----------|-------|--------");

  uint8_t needs_migration = 0;
  uint32_t total_executions = 0;
  uint32_t total_slow_executions = 0;

  for (uint8_t i = 0; i < spinlock_stats_count; i++) {
    spinlock_stats_t* stats = &spinlock_stats[i];
    total_executions += stats->total_count;
    total_slow_executions += stats->over_10us_count;

    const char* status = "OK";
    if (stats->max_duration_us > 10) {
      status = "MIGRATE";
      needs_migration++;
    }

    logPrintf("%-30s | %10lu | %8lu | %5lu | %s\n",
                  stats->location,
                  (unsigned long)stats->total_count,
                  (unsigned long)stats->max_duration_us,
                  (unsigned long)stats->over_10us_count,
                  status);
  }

  logPrintln("-------------------------------|------------|----------|-------|--------");
  logPrintf("Total locations: %u | Executions: %lu | Slow executions: %lu\n",
                spinlock_stats_count,
                (unsigned long)total_executions,
                (unsigned long)total_slow_executions);

  if (needs_migration > 0) {
    logPrintf("\n⚠️  %u critical sections exceed 10us threshold\n", needs_migration);
    logPrintln("Recommendation: Migrate these to mutexes for better real-time performance");
    logPrintln("See COMPREHENSIVE_AUDIT_REPORT.md Finding 1.3 for migration guide");
  } else {
    logPrintln("\n✓ All critical sections are <10us - spinlock usage is appropriate");
  }

  logPrintln("\nNOTE: This report shows MAXIMUM observed durations.");
  logPrintln("      Run system under load to capture worst-case timing.");
}

void motionResetSpinlockStats() {
  for (uint8_t i = 0; i < spinlock_stats_count; i++) {
    spinlock_stats[i].max_duration_us = 0;
    spinlock_stats[i].total_count = 0;
    spinlock_stats[i].over_10us_count = 0;
  }
  logInfo("[MOTION] Spinlock statistics reset");
}
#else
void motionPrintSpinlockStats() {
  logInfo("[MOTION] Spinlock timing not enabled (compile with ENABLE_SPINLOCK_TIMING=1)");
}
void motionResetSpinlockStats() {
  logInfo("[MOTION] Spinlock timing not enabled");
}
#endif

uint32_t motionGetMaxJitterUS() {
  return m_max_jitter_us;
}

void motionResetMaxJitter() {
  m_max_jitter_us = 0;
}

void motionTrackJitterUS(uint32_t jitter_us) {
  if (jitter_us > m_max_jitter_us) {
    m_max_jitter_us = jitter_us;
  }
}

void motionResetMaintenance() {
    for (int i = 0; i < 4; i++) {
        axes[i].accumulated_distance_units = 0;
        axes[i].maintenance_alert_logged = false;
    }
    logInfo("[MOTION] Axis maintenance distance counters reset");
}

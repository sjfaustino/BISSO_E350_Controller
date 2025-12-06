/**
 * @file cli_motion.cpp
 * @brief Motion CLI Commands
 * @project Gemini v2.4.0
 */

#include "cli.h"
#include "motion.h"
#include "serial_logger.h"
#include "input_validation.h" // Now includes axisCharToIndex
#include <string.h>
#include <stdlib.h>

// ============================================================================
// STATUS & DIAGNOSTICS
// ============================================================================

void cmd_motion_status(int argc, char** argv) {
  motionDiagnostics();
}

void cmd_estop_status(int argc, char** argv) {
  if (motionIsEmergencyStopped()) {
    Serial.println("[MOTION] [WARN] EMERGENCY STOP ACTIVE");
  } else {
    Serial.println("[MOTION] [OK] System Enabled");
  }
}

// ============================================================================
// CONTROL COMMANDS
// ============================================================================

void cmd_motion_stop(int argc, char** argv) {
  motionStop();
  Serial.println("[MOTION] Stop command sent");
}

void cmd_motion_pause(int argc, char** argv) {
  motionPause();
  Serial.println("[MOTION] Pause command sent");
}

void cmd_motion_resume(int argc, char** argv) {
  motionResume();
  Serial.println("[MOTION] Resume command sent");
}

void cmd_estop_on(int argc, char** argv) {
  motionEmergencyStop();
  Serial.println("[MOTION] [CRITICAL] E-STOP TRIGGERED BY USER");
}

void cmd_estop_off(int argc, char** argv) {
  if (motionClearEmergencyStop()) {
    Serial.println("[MOTION] [OK] E-Stop Cleared");
  } else {
    Serial.println("[MOTION] [FAIL] Could not clear E-Stop (Check Safety Alarms)");
  }
}

// ============================================================================
// CONFIGURATION COMMANDS
// ============================================================================

void cmd_soft_limits(int argc, char** argv) {
  if (argc < 4) {
    Serial.println("Usage: limit <axis> <min> <max> [enable]");
    return;
  }
  
  uint8_t axis = axisCharToIndex(argv[1]);
  if (axis == 255) {
    Serial.println("[MOTION] Invalid axis");
    return;
  }
  
  int32_t min_pos = atol(argv[2]);
  int32_t max_pos = atol(argv[3]);
  motionSetSoftLimits(axis, min_pos, max_pos);
  
  if (argc >= 5) {
    bool enable = (atoi(argv[4]) > 0);
    motionEnableSoftLimits(axis, enable);
  }
  
  Serial.printf("[MOTION] Soft limits updated for Axis %d\n", axis);
}

void cmd_feed_override(int argc, char** argv) {
    if (argc < 2) {
        Serial.printf("[CLI] Current Feed: %.0f%%\n", motionGetFeedOverride() * 100.0f);
        return;
    }
    
    float factor = atof(argv[1]);
    
    // Support percentage input (e.g., "150" -> 1.5)
    if (factor > 10.0f) factor /= 100.0f;
    
    motionSetFeedOverride(factor);
    Serial.printf("[CLI] Feed override set to %.2f\n", factor);
}

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterMotionCommands() {
  cliRegisterCommand("status", "Show motion status", cmd_motion_status);
  cliRegisterCommand("stop", "Stop all motion", cmd_motion_stop);
  cliRegisterCommand("pause", "Pause motion", cmd_motion_pause);
  cliRegisterCommand("resume", "Resume motion", cmd_motion_resume);
  
  cliRegisterCommand("estop", "Trigger Emergency Stop", cmd_estop_on);
  cliRegisterCommand("clear", "Clear Emergency Stop", cmd_estop_off);
  cliRegisterCommand("estop_status", "Show E-Stop status", cmd_estop_status);
  
  cliRegisterCommand("limit", "Set soft limits", cmd_soft_limits);
  cliRegisterCommand("feed", "Set Feed Override (0.1 - 2.0)", cmd_feed_override);
}
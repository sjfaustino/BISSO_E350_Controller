/**
 * @file cli_motion.cpp
 * @brief Motion CLI Commands
 * @project PosiPro
 */

#include "cli.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides status accessors
#include "serial_logger.h"
#include "input_validation.h" 
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
    logWarning("[MOTION] EMERGENCY STOP ACTIVE");
  } else {
    logInfo("[MOTION] [OK] System Enabled");
  }
}

// ============================================================================
// CONTROL COMMANDS
// ============================================================================

void cmd_motion_stop(int argc, char** argv) {
  motionStop();
  logInfo("[MOTION] Stop command sent");
}

void cmd_motion_pause(int argc, char** argv) {
  motionPause();
  logInfo("[MOTION] Pause command sent");
}

void cmd_motion_resume(int argc, char** argv) {
  motionResume();
  logInfo("[MOTION] Resume command sent");
}

void cmd_estop_on(int argc, char** argv) {
  motionEmergencyStop();
  logError("[MOTION] CRITICAL: E-STOP TRIGGERED BY USER");
}

void cmd_estop_off(int argc, char** argv) {
  if (motionClearEmergencyStop()) {
    logInfo("[MOTION] [OK] E-Stop Cleared");
  } else {
    logWarning("[MOTION] Could not clear E-Stop (Check Safety Alarms)");
  }
}

void cmd_estop_main(int argc, char** argv) {
  if (argc < 2 || strcasecmp(argv[1], "status") == 0) {
    cmd_estop_status(argc, argv);
    return;
  }
  
  if (strcasecmp(argv[1], "on") == 0) {
    cmd_estop_on(argc, argv);
  } else if (strcasecmp(argv[1], "off") == 0) {
    cmd_estop_off(argc, argv);
  } else {
    logPrintln("Usage: estop [status|on|off]");
  }
}

// ============================================================================
// CONFIGURATION COMMANDS
// ============================================================================

void cmd_soft_limits(int argc, char** argv) {
  if (argc < 4) {
    logPrintln("Usage: limit <axis> <min> <max> [enable]");
    return;
  }
  
  uint8_t axis = axisCharToIndex(argv[1]);
  if (axis == 255) {
    logWarning("[MOTION] Invalid axis");
    return;
  }
  
  int32_t min_pos = atol(argv[2]);
  int32_t max_pos = atol(argv[3]);
  motionSetSoftLimits(axis, min_pos, max_pos);
  
  if (argc >= 5) {
    bool enable = (atoi(argv[4]) > 0);
    motionEnableSoftLimits(axis, enable);
  }
  
  logInfo("[MOTION] Soft limits updated for Axis %d", axis);
}

void cmd_feed_override(int argc, char** argv) {
    if (argc < 2) {
        logPrintf("[CLI] Current Feed: %.0f%%\n", motionGetFeedOverride() * 100.0f);
        return;
    }

    float factor = atof(argv[1]);

    // Support percentage input (e.g., "150" -> 1.5)
    if (factor > 10.0f) factor /= 100.0f;

    motionSetFeedOverride(factor);
    logInfo("[CLI] Feed override set to %.2f", factor);
}

// ============================================================================
// PERFORMANCE DIAGNOSTICS
// ============================================================================

void cmd_spinlock_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[SPINLOCK] === Spinlock Timing Diagnostics ===");
        logPrintln("Usage: spinlock [stats | reset]");
        logPrintln("  stats:  Show critical section timing report");
        logPrintln("  reset:  Reset timing statistics");
        logPrintln("");
        logPrintln("Purpose: Audit spinlock critical section durations");
        logPrintln("         to identify sections >10us that should use mutexes");
        logPrintln("");
        logPrintln("See: COMPREHENSIVE_AUDIT_REPORT.md Finding 1.3");
        return;
    }

    if (strcmp(argv[1], "stats") == 0) {
        motionPrintSpinlockStats();
    } else if (strcmp(argv[1], "reset") == 0) {
        motionResetSpinlockStats();
    } else {
        logWarning("[SPINLOCK] Unknown sub-command: %s", argv[1]);
    }
}

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterMotionCommands() {
  cliRegisterCommand("status", "Show motion status", cmd_motion_status);
  cliRegisterCommand("stop", "Stop all motion", cmd_motion_stop);
  cliRegisterCommand("pause", "Pause motion", cmd_motion_pause);
  cliRegisterCommand("resume", "Resume motion", cmd_motion_resume);

  cliRegisterCommand("estop", "Emergency Stop management (status|on|off)", cmd_estop_main);

  cliRegisterCommand("limit", "Set soft limits", cmd_soft_limits);
  cliRegisterCommand("feed", "Set Feed Override (0.1 - 2.0)", cmd_feed_override);

  cliRegisterCommand("spinlock", "Spinlock timing diagnostics (stats|reset)", cmd_spinlock_main);
}

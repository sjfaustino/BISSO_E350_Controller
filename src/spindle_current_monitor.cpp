/**
 * @file spindle_current_monitor.cpp
 * @brief Spindle Current Monitoring System Implementation
 * @project BISSO E350 Controller - Phase 5.0
 */

#include "spindle_current_monitor.h"
#include "fault_logging.h"
#include "jxk10_modbus.h"
#include "altivar31_modbus.h"
#include "motion.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include "system_tuning.h"
#include <Arduino.h>
#include <string.h>


// Global monitor state (disabled by default for testing without VFD)
// Default thresholds tuned for FIMET M 180L4 motor:
//   - 22kW, 1460 RPM, cos φ 0.85
//   - Rated current: 24.5A @ 400V (Star), 42A @ 230V (Delta)
//   - Overcurrent threshold: 30A (122% of rated, allows startup surge)
//   - Stall threshold: 25A (102% of rated)
static spindle_monitor_state_t monitor_state = {
    .enabled = false,
    .poll_interval_ms = SPINDLE_MONITOR_POLL_DEFAULT_MS,
    .last_poll_time_ms = 0,
    .overcurrent_threshold_amps = SPINDLE_OVERCURRENT_DEFAULT_AMPS,  // 122% of 24.5A rated
    .current_amps = 0.0f,
    .current_peak_amps = 0.0f,
    .current_average_amps = 0.0f,
    .current_previous_amps = 0.0f,
    .tool_breakage_drop_amps = SPINDLE_DROP_DEFAULT_AMPS,
    .stall_threshold_amps = SPINDLE_STALL_DEFAULT_AMPS,
    .stall_timeout_ms = SPINDLE_STALL_TIMEOUT_DEFAULT_MS,
    .auto_pause_enabled = true,
    .auto_pause_threshold_amps = 25.0f,
    .auto_paused = false,
    .auto_pause_count = 0,
    .alarm_tool_breakage = false,
    .alarm_stall = false,
    .alarm_overload = false,
    .overload_start_time_ms = 0,
    .read_count = 0,
    .error_count = 0,
    .overload_count = 0,
    .shutdown_count = 0,
    .tool_breakage_count = 0,
    .stall_count = 0,
    .last_shutdown_time_ms = 0,
    .last_shutdown_current_amps = 0.0f,
    .jxk10_slave_address = 1,
    .jxk10_baud_rate = 9600};

static uint32_t last_check_time_ms = 0;

bool spindleMonitorInit(uint8_t jxk10_address, float threshold_amps) {
  // Load global RS485 baud for all Modbus devices
  uint32_t rs485_baud = configGetInt(KEY_RS485_BAUD, 9600);
  
  // Initialize JXK-10 driver (registry registration happens here)
  if (!jxk10ModbusInit(jxk10_address, rs485_baud)) {
    logError("[SPINDLE] Failed to initialize JXK-10 driver");
    return false;
  }

  // Initialize VFD driver (registry registration happens here)
  uint8_t vfd_address = (uint8_t)configGetInt(KEY_VFD_ADDR, 2);
  altivar31ModbusInit(vfd_address, rs485_baud);

  // Set monitor parameters
  monitor_state.enabled = true;
  monitor_state.jxk10_slave_address = jxk10_address;
  monitor_state.jxk10_baud_rate = rs485_baud;
  monitor_state.overcurrent_threshold_amps = threshold_amps;
  monitor_state.poll_interval_ms = SPINDLE_MONITOR_POLL_DEFAULT_MS;
  monitor_state.last_poll_time_ms = millis();
  
  // Load auto-pause config
  monitor_state.auto_pause_enabled = (configGetInt(KEY_SPINDL_PAUSE_EN, 1) != 0);
  monitor_state.auto_pause_threshold_amps = (float)configGetInt(KEY_SPINDL_PAUSE_THR, 25);
  monitor_state.auto_paused = false;

  logInfo("[SPINDLE] Initialized (JXK-10 ID: %u, Threshold: %.1f A)",
                jxk10_address, threshold_amps);
  if (monitor_state.auto_pause_enabled) {
    logInfo("[SPINDLE] Auto-pause enabled at %.1f A", monitor_state.auto_pause_threshold_amps);
  }
  return true;
}


void spindleMonitorSetEnabled(bool enable) {
  monitor_state.enabled = enable;
  if (enable) {
    logInfo("[SPINDLE] Monitoring ENABLED");
  } else {
    logInfo("[SPINDLE] Monitoring DISABLED");
  }
}

bool spindleMonitorIsEnabled(void) { return monitor_state.enabled; }

void spindleMonitorSetThreshold(float threshold_amps) {
  if (threshold_amps < 0.0f || threshold_amps > 50.0f) {
    return; // Out of range
  }
  monitor_state.overcurrent_threshold_amps = threshold_amps;
}

float spindleMonitorGetThreshold(void) {
  return monitor_state.overcurrent_threshold_amps;
}

void spindleMonitorSetPollInterval(uint32_t interval_ms) {
  if (interval_ms < 100 || interval_ms > 60000) {
    return; // Out of range (100ms to 60s)
  }
  monitor_state.poll_interval_ms = interval_ms;
}

uint32_t spindleMonitorGetPollInterval(void) {
  return monitor_state.poll_interval_ms;
}

bool spindleMonitorUpdate(void) {
  if (!monitor_state.enabled) {
    return false;
  }

  uint32_t now = millis();
  if (now - last_check_time_ms < SPINDLE_MONITOR_RATE_LIMIT_MS) {
      return false; // Rate limit logic check
  }
  last_check_time_ms = now;

  // Read latest current from JXK-10 (polled by registry)
  float current = jxk10GetCurrentAmps();
  monitor_state.current_amps = current;

  // Update statistics and peaks
  if (current > monitor_state.current_peak_amps) {
      monitor_state.current_peak_amps = current;
  }

  // Auto-pause feature: Pause motion before hitting critical threshold
  // This protects blades by pausing instead of triggering E-Stop
  if (monitor_state.auto_pause_enabled && !monitor_state.auto_paused) {
    if (current > monitor_state.auto_pause_threshold_amps && 
        current < monitor_state.overcurrent_threshold_amps) {
      // Overload but not critical - pause and alert
      motionPause();
      monitor_state.auto_paused = true;
      monitor_state.auto_pause_count++;
      
      faultLogWarning(FAULT_SPINDLE_OVERCURRENT, "Spindle Auto-Pause Triggered");
      logWarning("[SPINDLE] AUTO-PAUSE: Current %.1f A exceeds %.1f A threshold",
                 current, monitor_state.auto_pause_threshold_amps);
      logInfo("[SPINDLE] Motion paused. Reduce load and use 'resume' to continue.");
      
      // Alert operator
      extern void alertWarning(void);
      alertWarning();
    }
  }
  
  // Auto-recovery: If we auto-paused and current dropped, log it
  if (monitor_state.auto_paused && current < (monitor_state.auto_pause_threshold_amps * 0.8f)) {
    logInfo("[SPINDLE] Current normalized (%.1f A). Use 'resume' to continue.", current);
    // Don't auto-resume - let operator decide
  }

  // Check for critical overcurrent condition (E-Stop threshold)
  if (current > monitor_state.overcurrent_threshold_amps) {
      monitor_state.overload_count++;
      spindleMonitorTriggerShutdown();
  }

  return true;
}

float spindleMonitorGetCurrent(void) { return monitor_state.current_amps; }

float spindleMonitorGetPeakCurrent(void) {
  return monitor_state.current_peak_amps;
}

bool spindleMonitorIsOvercurrent(void) {
  return (monitor_state.current_amps >
          monitor_state.overcurrent_threshold_amps);
}

// NOTE: Status register not documented in JXK-10 PDF, these always return false
bool spindleMonitorIsOverload(void) { return false; }

bool spindleMonitorIsFault(void) { return false; }

void spindleMonitorTriggerShutdown(void) {
  monitor_state.shutdown_count++;
  monitor_state.last_shutdown_time_ms = millis();
  monitor_state.last_shutdown_current_amps = monitor_state.current_amps;

  // Log to fault history
  faultLogError(FAULT_SPINDLE_OVERCURRENT,
                "Spindle overcurrent - triggering emergency shutdown");

  // Trigger safety shutdown
  logError(
      "[SPINDLE] OVERCURRENT SHUTDOWN! Current: %.1f A (Threshold: %.1f A)",
      monitor_state.current_amps, monitor_state.overcurrent_threshold_amps);

  motionEmergencyStop();
}

const spindle_monitor_state_t *spindleMonitorGetState(void) {
  return &monitor_state;
}

void spindleMonitorResetStats(void) {
  monitor_state.read_count = 0;
  monitor_state.error_count = 0;
  monitor_state.overload_count = 0;
  monitor_state.shutdown_count = 0;
  monitor_state.current_peak_amps = 0.0f;

  jxk10ResetErrorCounters();
}

void spindleMonitorPrintDiagnostics(void) {
  serialLoggerLock();
  Serial.println("\n[SPINDLE] === Current Monitor Diagnostics ===");
  Serial.printf("Status:              %s\n",
                monitor_state.enabled ? "ENABLED" : "DISABLED");
  Serial.printf("Current:             %.2f A\n", monitor_state.current_amps);
  Serial.printf("Peak Current:        %.2f A\n",
                monitor_state.current_peak_amps);
  Serial.printf("Average Current:     %.2f A\n",
                monitor_state.current_average_amps);
  Serial.printf("Overcurrent Threshold: %.1f A\n",
                monitor_state.overcurrent_threshold_amps);
  Serial.printf("Poll Interval:       %lu ms\n",
                (unsigned long)monitor_state.poll_interval_ms);
  Serial.printf("Read Count:          %lu\n",
                (unsigned long)monitor_state.read_count);
  Serial.printf("Error Count:         %lu\n",
                (unsigned long)monitor_state.error_count);
  Serial.printf("Overload Events:     %lu\n",
                (unsigned long)monitor_state.overload_count);
  Serial.printf("Shutdown Events:     %lu\n",
                (unsigned long)monitor_state.shutdown_count);
  if (monitor_state.shutdown_count > 0) {
    Serial.printf(
        "Last Shutdown:       %lu ms ago @ %.1f A\n",
        (unsigned long)(millis() - monitor_state.last_shutdown_time_ms),
        monitor_state.last_shutdown_current_amps);
  }
  
  // Alarm status
  Serial.printf("Tool Breakage Alarm: %s (count: %lu)\n",
                monitor_state.alarm_tool_breakage ? "ACTIVE" : "OK",
                (unsigned long)monitor_state.tool_breakage_count);
  Serial.printf("Stall Alarm:         %s (count: %lu)\n",
                monitor_state.alarm_stall ? "ACTIVE" : "OK",
                (unsigned long)monitor_state.stall_count);
  Serial.printf("Tool Breakage Threshold: %.1f A drop\n",
                monitor_state.tool_breakage_drop_amps);
  Serial.printf("Stall Threshold:     %.1f A for %lu ms\n",
                monitor_state.stall_threshold_amps,
                (unsigned long)monitor_state.stall_timeout_ms);
  serialLoggerUnlock();
}

// === ALARM API FUNCTIONS ===

bool spindleMonitorIsToolBreakage(void) {
  return monitor_state.alarm_tool_breakage;
}

bool spindleMonitorIsStall(void) {
  return monitor_state.alarm_stall;
}

void spindleMonitorClearAlarms(void) {
  monitor_state.alarm_tool_breakage = false;
  monitor_state.alarm_stall = false;
  monitor_state.alarm_overload = false;
  monitor_state.overload_start_time_ms = 0;
  logInfo("[SPINDLE] All alarms cleared");
}

void spindleMonitorSetToolBreakageThreshold(float drop_amps) {
  if (drop_amps >= 1.0f && drop_amps <= 20.0f) {
    monitor_state.tool_breakage_drop_amps = drop_amps;
    logInfo("[SPINDLE] Tool breakage threshold set to %.1f A", drop_amps);
  }
}

void spindleMonitorSetStallParams(float threshold_amps, uint32_t timeout_ms) {
  if (threshold_amps >= 5.0f && threshold_amps <= 50.0f) {
    monitor_state.stall_threshold_amps = threshold_amps;
  }
  if (timeout_ms >= 500 && timeout_ms <= 10000) {
    monitor_state.stall_timeout_ms = timeout_ms;
  }
  logInfo("[SPINDLE] Stall params: %.1f A for %lu ms",
                monitor_state.stall_threshold_amps,
                (unsigned long)monitor_state.stall_timeout_ms);
}

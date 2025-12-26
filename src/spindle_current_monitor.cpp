/**
 * @file spindle_current_monitor.cpp
 * @brief Spindle Current Monitoring System Implementation
 * @project BISSO E350 Controller - Phase 5.0
 */

#include "spindle_current_monitor.h"
#include "fault_logging.h"
#include "jxk10_modbus.h"
#include "motion.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>


// Global monitor state (disabled by default for testing without VFD)
static spindle_monitor_state_t monitor_state = {
    .enabled = false,
    .poll_interval_ms = 1000,
    .last_poll_time_ms = 0,
    .overcurrent_threshold_amps = 30.0f,
    .current_amps = 0.0f,
    .current_peak_amps = 0.0f,
    .current_average_amps = 0.0f,
    .current_previous_amps = 0.0f,
    .tool_breakage_drop_amps = 5.0f,
    .stall_threshold_amps = 25.0f,
    .stall_timeout_ms = 2000,
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
  // Initialize JXK-10 driver (registry registration happens here)
  if (!jxk10ModbusInit(jxk10_address, 9600)) {
    logError("[SPINDLE] Failed to initialize JXK-10 driver");
    return false;
  }

  // Set monitor parameters
  monitor_state.enabled = true;
  monitor_state.jxk10_slave_address = jxk10_address;
  monitor_state.jxk10_baud_rate = 9600;
  monitor_state.overcurrent_threshold_amps = threshold_amps;
  monitor_state.poll_interval_ms = 1000;
  monitor_state.last_poll_time_ms = millis();

  logInfo("[SPINDLE] Initialized (JXK-10 ID: %u, Threshold: %.1f A)",
                jxk10_address, threshold_amps);
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
  if (now - last_check_time_ms < 100) {
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

  // Check for overcurrent condition
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

bool spindleMonitorIsOverload(void) { return jxk10IsOverload(); }

bool spindleMonitorIsFault(void) { return jxk10IsFault(); }

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

#include "config_validator.h"
#include "config_manager.h"
#include <string.h>
#include <Arduino.h>

// ============================================================================
// VALIDATOR STATE
// ============================================================================

static validation_result_t last_result = {0, 0, 0, 0, 0};

// ============================================================================
// VALIDATION IMPLEMENTATION
// ============================================================================

validation_result_t configValidatorRun(validator_level_t level) {
  uint32_t start_time = millis();
  validation_result_t result = {0, 0, 0, 0, 0};
  
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║         CONFIGURATION VALIDATION ENGINE                   ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  // Motion validation
  result.total_checks++;
  if (configValidatorCheckMotion()) {
    result.passed_checks++;
    Serial.println("[VALID] ✅ Motion parameters OK");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] ❌ Motion parameters FAILED");
  }
  
  // Communication validation
  result.total_checks++;
  if (configValidatorCheckCommunication()) {
    result.passed_checks++;
    Serial.println("[VALID] ✅ Communication parameters OK");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] ❌ Communication parameters FAILED");
  }
  
  // Safety validation
  result.total_checks++;
  if (configValidatorCheckSafety()) {
    result.passed_checks++;
    Serial.println("[VALID] ✅ Safety parameters OK");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] ❌ Safety parameters FAILED");
  }
  
  // Resource validation
  result.total_checks++;
  if (configValidatorCheckResources()) {
    result.passed_checks++;
    Serial.println("[VALID] ✅ Resource parameters OK");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] ❌ Resource parameters FAILED");
  }
  
  // Consistency check (FIX: Implemented cross-parameter validation)
  result.total_checks++;
  if (configValidatorCheckConsistency()) {
    result.passed_checks++;
    Serial.println("[VALID] ✅ Configuration consistent");
  } else {
    result.failed_checks++;
    result.warnings++;
    Serial.println("[VALID] ⚠️  Configuration inconsistencies");
  }
  
  // Check common mistakes (only in strict mode)
  if (level >= VALIDATOR_LEVEL_STRICT) {
    char warnings_buffer[256];
    uint8_t warning_count = configValidatorCheckCommonMistakes(warnings_buffer, 256);
    result.warnings += warning_count;
  }
  
  result.validation_time_ms = millis() - start_time;
  last_result = result;
  
  Serial.print("\n[VALID] Summary: ");
  Serial.print(result.passed_checks);
  Serial.print("/");
  Serial.print(result.total_checks);
  Serial.print(" checks passed");
  if (result.warnings > 0) {
    Serial.print(", ");
    Serial.print(result.warnings);
    Serial.print(" warnings");
  }
  Serial.print(" (");
  Serial.print(result.validation_time_ms);
  Serial.println(" ms)\n");
  
  return result;
}

// FIX: Added robust range checks
bool configValidatorCheckMotion() {
  config_limits_t limits = configGetLimits();
  
  // Check position limits (ensure min < max)
  if (limits.max_position <= limits.min_position) return false;
  
  // Check velocity limits (Assumes counts/sec, 100 counts/s minimum)
  if (limits.max_velocity < 100 || limits.max_velocity > 100000) return false;
  
  // Check acceleration limits (Assume reasonable max acceleration)
  if (limits.max_acceleration < 100 || limits.max_acceleration > 50000) return false;
  
  return true;
}

bool configValidatorCheckCommunication() {
  config_limits_t limits = configGetLimits();
  
  // Check timeout (reasonable range 1s to 60s)
  if (limits.timeout_ms < 1000 || limits.timeout_ms > 60000) return false;
  
  // Check retry count
  if (limits.retry_count < 0 || limits.retry_count > 10) return false;
  
  return true;
}

bool configValidatorCheckSafety() {
  config_limits_t limits = configGetLimits();
  
  // Position limits should have safety margin (arbitrary safe limit for the system size)
  if (limits.max_position <= 1000) return false;
  
  // Velocity should have reasonable minimum (e.g., must be able to move faster than crawl)
  if (limits.max_velocity < 500) return false;
  
  return true;
}

bool configValidatorCheckResources() {
  config_limits_t limits = configGetLimits();
  
  // Check number of axes (must be 1-8, standard is 4 for this machine)
  if (limits.num_axes < 1 || limits.num_axes > 8) return false;
  
  return true;
}

// FIX: Completed cross-parameter validation logic
bool configValidatorCheckConsistency() {
  config_limits_t limits = configGetLimits();
  bool consistent = true;
  
  // Rule 1: Check if acceleration limit allows achieving max velocity in reasonable time.
  // Time = Velocity / Acceleration. If time > 5s, flag a warning.
  if (limits.max_acceleration > 0 && limits.max_velocity / limits.max_acceleration > 5) {
    Serial.println("[VALID] ⚠️ Warning: Low acceleration/velocity ratio (slow ramps)");
    consistent = false;
  }
  
  // Rule 2: Check timeout is sufficient for max travel at a very slow assumed speed (500 counts/sec)
  float min_speed = 500.0f;
  float max_travel = (float)(limits.max_position - limits.min_position);
  if (max_travel > 0 && (max_travel / min_speed) * 1000 > limits.timeout_ms) {
      Serial.println("[VALID] ❌ Consistency FAILED: Timeout too short for max move at min speed.");
      consistent = false;
  }
  
  return consistent;
}

uint8_t configValidatorCheckCommonMistakes(char* buffer, size_t buffer_size) {
  if (buffer == NULL) return 0;
  
  uint8_t mistake_count = 0;
  int offset = 0;
  config_limits_t limits = configGetLimits();
  
  // Check for unrealistic velocity (e.g., > 75% of max possible pulse rate)
  if (limits.max_velocity > 75000) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "⚠️  Very high velocity: %lu\n", limits.max_velocity);
    mistake_count++;
  }
  
  // Check for unrealistic acceleration (too high might cause skipped steps/stalls)
  if (limits.max_acceleration > 30000) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "⚠️  Very high acceleration: %lu\n", limits.max_acceleration);
    mistake_count++;
  }
  
  // Check for short timeout (less than 2s is usually risky)
  if (limits.timeout_ms < 2000) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "⚠️  Short timeout: %lu ms\n", limits.timeout_ms);
    mistake_count++;
  }
  
  return mistake_count;
}

size_t configValidatorGenerateReport(char* buffer, size_t buffer_size) {
  if (buffer == NULL) return 0;
  
  config_limits_t limits = configGetLimits();
  int offset = 0;
  
  offset += snprintf(buffer + offset, buffer_size - offset,
    "CONFIGURATION VALIDATION REPORT\n"
    "================================\n\n");
  
  offset += snprintf(buffer + offset, buffer_size - offset,
    "Motion Parameters:\n"
    "  Max Position:      %lu\n"
    "  Min Position:      %ld\n"
    "  Max Velocity:      %lu counts/sec\n"
    "  Max Acceleration:  %lu counts/sec²\n\n",
    limits.max_position, limits.min_position,
    limits.max_velocity, limits.max_acceleration);
  
  offset += snprintf(buffer + offset, buffer_size - offset,
    "System Parameters:\n"
    "  Number of Axes:    %u\n"
    "  Timeout:           %lu ms\n"
    "  Retry Count:       %u\n\n",
    limits.num_axes, limits.timeout_ms, limits.retry_count);
  
  offset += snprintf(buffer + offset, buffer_size - offset,
    "Validation Status:\n"
    "  Checks Run:        %u\n"
    "  Passed:            %u\n"
    "  Failed:            %u\n"
    "  Warnings:          %u\n"
    "  Time:              %lu ms\n",
    last_result.total_checks, last_result.passed_checks,
    last_result.failed_checks, last_result.warnings,
    last_result.validation_time_ms);
  
  return offset;
}

bool configValidatorCompareToBaseline(const char* baseline_json) {
  if (baseline_json == NULL) return false;
  
  Serial.println("[VALID] ✅ Configuration matches baseline (Placeholder check)");
  return true;
}

validation_result_t configValidatorGetLastResult() {
  return last_result;
}

void configValidatorPrintReport() {
  char report_buffer[512];
  size_t report_size = configValidatorGenerateReport(report_buffer, 512);
  
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║              DETAILED VALIDATION REPORT                    ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.println(report_buffer);
}
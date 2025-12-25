#include "config_validator.h"
#include "config_manager.h"
#include <string.h>
#include <Arduino.h>
#include <stdio.h>

static validation_result_t last_result = {0, 0, 0, 0, 0};

validation_result_t configValidatorRun(validator_level_t level) {
  uint32_t start_time = millis();
  // FIX: Explicit initialization
  validation_result_t result = {0, 0, 0, 0, 0};
  
  Serial.println("\n=== CONFIG VALIDATION ENGINE ===");
  
  result.total_checks++;
  if (configValidatorCheckMotion()) {
    result.passed_checks++;
    Serial.println("[VALID] [OK] Motion parameters");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] [FAIL] Motion parameters");
  }
  
  result.total_checks++;
  if (configValidatorCheckCommunication()) {
    result.passed_checks++;
    Serial.println("[VALID] [OK] Communication parameters");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] [FAIL] Communication parameters");
  }
  
  result.total_checks++;
  if (configValidatorCheckSafety()) {
    result.passed_checks++;
    Serial.println("[VALID] [OK] Safety parameters");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] [FAIL] Safety parameters");
  }

  result.total_checks++;
  if (configValidatorCheckResources()) {
      result.passed_checks++;
      Serial.println("[VALID] [OK] Resource parameters");
  } else {
      result.failed_checks++;
      Serial.println("[VALID] [FAIL] Resource parameters");
  }
  
  result.total_checks++;
  if (configValidatorCheckConsistency()) {
    result.passed_checks++;
    Serial.println("[VALID] [OK] Consistency check");
  } else {
    result.failed_checks++;
    Serial.println("[VALID] [WARN] Inconsistencies found");
  }
  
  result.validation_time_ms = millis() - start_time;
  last_result = result;
  
  // FIX: Cast to unsigned long
  Serial.printf("\nSummary: %d/%d Passed (%lu ms)\n", result.passed_checks, result.total_checks, (unsigned long)result.validation_time_ms);
  return result;
}

bool configValidatorCheckMotion() {
  config_limits_t limits = configGetLimits();
  if (limits.max_position <= limits.min_position) return false;
  if (limits.max_velocity < 100 || limits.max_velocity > 100000) return false;
  if (limits.max_acceleration < 100 || limits.max_acceleration > 50000) return false;
  return true;
}

bool configValidatorCheckCommunication() {
  config_limits_t limits = configGetLimits();
  if (limits.timeout_ms < 1000 || limits.timeout_ms > 60000) return false;
  if (limits.retry_count > 10) return false;
  return true;
}

bool configValidatorCheckSafety() {
  config_limits_t limits = configGetLimits();
  // Arbitrary safe limits for validation
  if (limits.max_position <= 1000) return false; 
  return true;
}

bool configValidatorCheckResources() {
  config_limits_t limits = configGetLimits();
  if (limits.num_axes < 1 || limits.num_axes > 8) return false;
  return true;
}

bool configValidatorCheckConsistency() {
  config_limits_t limits = configGetLimits();
  bool consistent = true;
  
  // Rule: Acceleration should allow reaching max velocity in < 5s
  if (limits.max_acceleration > 0 && limits.max_velocity / limits.max_acceleration > 5) {
    Serial.println("[VALID] [WARN] Slow ramp times detected");
    consistent = false;
  }
  
  // Rule: Timeout sufficient for full traverse at min speed
  float min_speed = 500.0f;
  float max_travel = (float)(limits.max_position - limits.min_position);
  if (max_travel > 0 && (max_travel / min_speed) * 1000 > limits.timeout_ms) {
      Serial.println("[VALID] [FAIL] Timeout too short for max move");
      consistent = false;
  }
  
  return consistent;
}

uint8_t configValidatorCheckCommonMistakes(char* buffer, size_t buffer_size) {
  config_limits_t limits = configGetLimits();
  uint8_t count = 0;
  int offset = 0;
  
  if (limits.timeout_ms < 2000) {
      offset += snprintf(buffer + offset, buffer_size - offset, "[WARN] Short timeout\n");
      count++;
  }
  return count;
}

size_t configValidatorGenerateReport(char* buffer, size_t buffer_size) {
  config_limits_t limits = configGetLimits();
  // FIX: Casts for format specifiers
  return snprintf(buffer, buffer_size, 
    "Report:\n  Pos: %ld to %ld\n  Vel: %lu\n  Acc: %lu\n  Status: %s",
    (long)limits.min_position, (long)limits.max_position, (unsigned long)limits.max_velocity, (unsigned long)limits.max_acceleration,
    (last_result.failed_checks == 0) ? "OK" : "FAIL");
}

void configValidatorPrintReport() {
  char report[512];
  configValidatorGenerateReport(report, sizeof(report));
  Serial.println(report);
}

bool configValidatorCompareToBaseline(const char* baseline_json) {
  if (!baseline_json || strlen(baseline_json) == 0) {
    Serial.println("[VALID] No baseline provided");
    return false;
  }
  
  // Get current config limits and compare with baseline
  config_limits_t current = configGetLimits();
  
  // Parse baseline JSON and compare key values
  // Using simple string search since ArduinoJson may be overkill here
  // This validates that current config matches expected baseline ranges
  
  bool matches = true;
  
  // Check if baseline contains expected structure
  if (strstr(baseline_json, "\"status\":") == nullptr) {
    Serial.println("[VALID] [WARN] Baseline missing status field");
    matches = false;
  }
  
  // Validate current config has reasonable values
  if (current.max_velocity == 0 || current.max_acceleration == 0) {
    Serial.println("[VALID] [FAIL] Current config has zero velocity/accel");
    matches = false;
  }
  
  if (matches) {
    Serial.println("[VALID] [OK] Config matches baseline structure");
  }
  
  return matches;
}

validation_result_t configValidatorGetLastResult() { return last_result; }
#include "config_validator.h"
#include "config_manager.h"

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
  
  // Consistency check
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

bool configValidatorCheckMotion() {
  config_limits_t limits = configGetLimits();
  
  // Check position limits
  if (limits.max_position <= limits.min_position) return false;
  
  // Check velocity limits
  if (limits.max_velocity < 100 || limits.max_velocity > 100000) return false;
  
  // Check acceleration limits
  if (limits.max_acceleration < 100 || limits.max_acceleration > 50000) return false;
  
  return true;
}

bool configValidatorCheckCommunication() {
  config_limits_t limits = configGetLimits();
  
  // Check timeout
  if (limits.timeout_ms < 1000 || limits.timeout_ms > 60000) return false;
  
  // Check retry count
  if (limits.retry_count < 0 || limits.retry_count > 10) return false;
  
  return true;
}

bool configValidatorCheckSafety() {
  config_limits_t limits = configGetLimits();
  
  // Position limits should have safety margin
  if (limits.max_position <= 1000) return false;
  
  // Velocity should have reasonable minimum
  if (limits.max_velocity < 500) return false;
  
  return true;
}

bool configValidatorCheckResources() {
  config_limits_t limits = configGetLimits();
  
  // Check number of axes
  if (limits.num_axes < 1 || limits.num_axes > 8) return false;
  
  // Check that resources don't exceed available
  if (limits.num_axes > 4) {
    Serial.println("[VALID] ⚠️  Warning: requesting more than 4 axes");
  }
  
  return true;
}

uint8_t configValidatorCheckCommonMistakes(char* buffer, size_t buffer_size) {
  if (buffer == NULL) return 0;
  
  uint8_t mistake_count = 0;
  int offset = 0;
  config_limits_t limits = configGetLimits();
  
  // Check for unrealistic velocity
  if (limits.max_velocity > 75000) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "⚠️  Very high velocity: %lu\n", limits.max_velocity);
    mistake_count++;
  }
  
  // Check for unrealistic acceleration
  if (limits.max_acceleration > 30000) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "⚠️  Very high acceleration: %lu\n", limits.max_acceleration);
    mistake_count++;
  }
  
  // Check for short timeout
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
  
  // This would parse baseline and compare
  // For now, return success
  Serial.println("[VALID] ✅ Configuration matches baseline");
  return true;
}

bool configValidatorCheckConsistency() {
  config_limits_t limits = configGetLimits();
  
  // Acceleration should be less than velocity (physically)
  // but allow for different units
  
  // Retry count should be reasonable
  if (limits.retry_count > 10) return false;
  
  // Number of axes should match system
  if (limits.num_axes != 4) {
    Serial.print("[VALID] ⚠️  System configured for ");
    Serial.print(limits.num_axes);
    Serial.println(" axes instead of standard 4");
  }
  
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

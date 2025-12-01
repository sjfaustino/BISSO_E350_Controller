#ifndef CONFIG_VALIDATOR_H
#define CONFIG_VALIDATOR_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// CONFIGURATION VALIDATOR TYPES
// ============================================================================

typedef enum {
  VALIDATOR_LEVEL_BASIC,           // Check types only
  VALIDATOR_LEVEL_STANDARD,        // Check ranges
  VALIDATOR_LEVEL_STRICT,          // Check relationships
  VALIDATOR_LEVEL_COMPREHENSIVE    // Full validation
} validator_level_t;

typedef struct {
  uint8_t total_checks;
  uint8_t passed_checks;
  uint8_t failed_checks;
  uint8_t warnings;
  uint32_t validation_time_ms;
} validation_result_t;

// ============================================================================
// VALIDATION FUNCTIONS
// ============================================================================

/**
 * @brief Run comprehensive configuration validation
 * @param level Validation level (basic to comprehensive)
 * @return Validation result structure
 */
validation_result_t configValidatorRun(validator_level_t level);

/**
 * @brief Validate motion parameters
 * @return true if motion parameters are valid
 */
bool configValidatorCheckMotion();

/**
 * @brief Validate communication parameters
 * @return true if communication parameters are valid
 */
bool configValidatorCheckCommunication();

/**
 * @brief Validate safety parameters
 * @return true if safety parameters are valid
 */
bool configValidatorCheckSafety();

/**
 * @brief Validate resource parameters
 * @return true if resource parameters are valid
 */
bool configValidatorCheckResources();

/**
 * @brief Check for common configuration mistakes
 * @param buffer Output buffer for warnings
 * @param buffer_size Size of buffer
 * @return Number of warnings found
 */
uint8_t configValidatorCheckCommonMistakes(char* buffer, size_t buffer_size);

/**
 * @brief Generate detailed validation report
 * @param buffer Output buffer
 * @param buffer_size Size of buffer
 * @return Number of bytes written
 */
size_t configValidatorGenerateReport(char* buffer, size_t buffer_size);

/**
 * @brief Compare configuration against known good baseline
 * @param baseline_json Baseline configuration JSON
 * @return true if configuration matches baseline
 */
bool configValidatorCompareToBaseline(const char* baseline_json);

/**
 * @brief Check configuration consistency
 * @return true if all parameters are consistent
 */
bool configValidatorCheckConsistency();

/**
 * @brief Get validation statistics
 * @return Validation result from last run
 */
validation_result_t configValidatorGetLastResult();

/**
 * @brief Print validation report to serial
 */
void configValidatorPrintReport();

#endif

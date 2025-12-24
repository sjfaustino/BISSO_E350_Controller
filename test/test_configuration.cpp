/**
 * @file test/test_configuration.cpp
 * @brief Unit tests for BISSO E350 Configuration System
 *
 * Tests cover:
 * - Configuration schema validation
 * - Value range constraint enforcement
 * - Configuration persistence (save/load)
 * - Configuration migration (version upgrades)
 * - Default configuration
 * - Checksum integrity
 */

#include "helpers/test_utils.h"
#include <cstdint>
#include <cstring>
#include <unity.h>

/**
 * @brief Mock configuration structure
 * Represents the BISSO E350 system configuration
 */
typedef struct {
  uint16_t version;            // Config version (for migration)
  uint16_t soft_limit_low_mm;  // Lower soft limit
  uint16_t soft_limit_high_mm; // Upper soft limit
  uint16_t max_speed_hz;       // Maximum VFD frequency
  uint16_t min_speed_hz;       // Minimum VFD frequency
  uint16_t vfd_acc_time_100ms; // Acceleration time in 0.1s units
  uint16_t vfd_dec_time_100ms; // Deceleration time in 0.1s units
  uint16_t encoder_ppm[3];     // Pulses per mm for X, Y, Z axes
  uint8_t axis_count;          // Number of axes
  uint32_t checksum;           // Configuration checksum
} config_t;

/**
 * @brief Calculate configuration checksum
 * Simple Fletcher's checksum for integrity verification
 */
static uint32_t config_calculate_checksum(const config_t *config) {
  uint32_t sum1 = 0, sum2 = 0;
  const uint8_t *bytes = (const uint8_t *)config;
  size_t size = sizeof(config_t) - sizeof(uint32_t); // Exclude checksum field

  for (size_t i = 0; i < size; i++) {
    sum1 += bytes[i];
    sum2 += sum1;
  }

  return (sum2 << 16) | (sum1 & 0xFFFF);
}

/**
 * @brief Validate configuration against schema
 */
static uint8_t config_is_valid(const config_t *config) {
  if (!config)
    return 0;

  // Version check
  if (config->version > 2)
    return 0; // Only versions 0-2 supported

  // Soft limit checks
  if (config->soft_limit_low_mm >= config->soft_limit_high_mm)
    return 0;
  if (config->soft_limit_high_mm > 1000)
    return 0; // Reasonable max

  // Speed checks
  if (config->min_speed_hz < 1 || config->min_speed_hz > 10)
    return 0;
  if (config->max_speed_hz < 50 || config->max_speed_hz > 105)
    return 0;
  if (config->min_speed_hz >= config->max_speed_hz)
    return 0;

  // VFD ramp time checks (in 0.1s units)
  if (config->vfd_acc_time_100ms < 2 || config->vfd_acc_time_100ms > 20)
    return 0;
  if (config->vfd_dec_time_100ms < 2 || config->vfd_dec_time_100ms > 20)
    return 0;

  // Encoder PPM checks (valid range: 50-200)
  for (int i = 0; i < 3; i++) {
    if (config->encoder_ppm[i] < 50 || config->encoder_ppm[i] > 200) {
      return 0;
    }
  }

  // Axis count check
  if (config->axis_count != 3)
    return 0; // Only 3-axis supported

  return 1;
}

/**
 * @brief Create default configuration
 */
static config_t config_create_default(void) {
  config_t config;
  std::memset(&config, 0, sizeof(config));

  config.version = 2;
  config.soft_limit_low_mm = 0;
  config.soft_limit_high_mm = 500;
  config.max_speed_hz = 105;     // Altivar 31 HSP
  config.min_speed_hz = 1;       // Altivar 31 LSP
  config.vfd_acc_time_100ms = 6; // 0.6s
  config.vfd_dec_time_100ms = 4; // 0.4s
  config.encoder_ppm[0] = 100;   // X axis
  config.encoder_ppm[1] = 100;   // Y axis
  config.encoder_ppm[2] = 100;   // Z axis
  config.axis_count = 3;

  // Calculate checksum
  config.checksum = config_calculate_checksum(&config);

  return config;
}

/**
 * @brief Test fixtures
 */
static config_test_fixture_t fixture;
static config_t config;

static void local_setUp(void) __attribute__((unused));
static void local_setUp(void) {
  test_init_config_fixture(&fixture);
  config = config_create_default();
}

static void local_tearDown(void) __attribute__((unused));
static void local_tearDown(void) {
  // Reset for next test
}

/**
 * @section Default Configuration Tests
 * Tests for default configuration values
 */

/**
 * @test Default configuration is valid
 */
void test_default_configuration_valid(void) {
  config = config_create_default();
  TEST_ASSERT_TRUE(config_is_valid(&config));
}

/**
 * @test Default soft limits are reasonable
 */
void test_default_soft_limits(void) {
  config = config_create_default();
  TEST_ASSERT_EQUAL(0, config.soft_limit_low_mm);
  TEST_ASSERT_EQUAL(500, config.soft_limit_high_mm);
}

/**
 * @test Default VFD settings match Altivar 31
 */
void test_default_vfd_settings(void) {
  config = config_create_default();
  TEST_ASSERT_EQUAL(105, config.max_speed_hz);     // HSP
  TEST_ASSERT_EQUAL(1, config.min_speed_hz);       // LSP
  TEST_ASSERT_EQUAL(6, config.vfd_acc_time_100ms); // 0.6s
  TEST_ASSERT_EQUAL(4, config.vfd_dec_time_100ms); // 0.4s
}

/**
 * @test Default encoder calibration
 */
void test_default_encoder_calibration(void) {
  config = config_create_default();
  TEST_ASSERT_EQUAL(100, config.encoder_ppm[0]);
  TEST_ASSERT_EQUAL(100, config.encoder_ppm[1]);
  TEST_ASSERT_EQUAL(100, config.encoder_ppm[2]);
}

/**
 * @section Schema Validation Tests
 * Tests for configuration constraint enforcement
 */

/**
 * @test Rejects invalid version
 */
void test_validation_rejects_invalid_version(void) {
  config = config_create_default();
  config.version = 99; // Future/invalid version

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects soft limit low >= high
 */
void test_validation_soft_limits_low_must_be_less(void) {
  config = config_create_default();
  config.soft_limit_low_mm = 500;
  config.soft_limit_high_mm = 500;

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects soft limit high exceeds maximum
 */
void test_validation_soft_limit_high_max(void) {
  config = config_create_default();
  config.soft_limit_high_mm = 1001; // Exceeds 1000mm max

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects min speed below LSP
 */
void test_validation_min_speed_too_low(void) {
  config = config_create_default();
  config.min_speed_hz = 0; // Below LSP minimum of 1

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects max speed above HSP
 */
void test_validation_max_speed_too_high(void) {
  config = config_create_default();
  config.max_speed_hz = 106; // Above HSP of 105

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects min speed >= max speed
 */
void test_validation_min_max_speed_order(void) {
  config = config_create_default();
  config.min_speed_hz = 80;
  config.max_speed_hz = 50; // Lower than min

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects VFD acceleration time too short
 */
void test_validation_vfd_acc_time_minimum(void) {
  config = config_create_default();
  config.vfd_acc_time_100ms = 1; // Less than 0.2s

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects VFD acceleration time too long
 */
void test_validation_vfd_acc_time_maximum(void) {
  config = config_create_default();
  config.vfd_acc_time_100ms = 21; // More than 2.0s

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects invalid encoder PPM
 */
void test_validation_invalid_encoder_ppm(void) {
  config = config_create_default();
  config.encoder_ppm[0] = 25; // Below minimum of 50

  TEST_ASSERT_FALSE(config_is_valid(&config));

  config = config_create_default();
  config.encoder_ppm[1] = 250; // Above maximum of 200

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @test Rejects wrong axis count
 */
void test_validation_axis_count_must_be_three(void) {
  config = config_create_default();
  config.axis_count = 4;

  TEST_ASSERT_FALSE(config_is_valid(&config));
}

/**
 * @section Checksum Tests
 * Tests for configuration integrity verification
 */

/**
 * @test Default configuration checksum is calculated
 */
void test_checksum_calculation(void) {
  config = config_create_default();
  TEST_ASSERT_NOT_EQUAL(0, config.checksum);
}

/**
 * @test Checksum changes when configuration changes
 */
void test_checksum_detects_modification(void) {
  config = config_create_default();
  uint32_t original_checksum = config.checksum;

  // Modify configuration
  config.soft_limit_high_mm = 600;
  uint32_t modified_checksum = config_calculate_checksum(&config);

  TEST_ASSERT_NOT_EQUAL(original_checksum, modified_checksum);
}

/**
 * @test Checksum validation detects corruption
 */
void test_checksum_detects_corruption(void) {
  config = config_create_default();
  uint32_t correct_checksum = config.checksum;

  // Corrupt configuration
  config.soft_limit_high_mm = 600;
  // Don't update checksum

  // Recalculate checksum and compare
  uint32_t calculated = config_calculate_checksum(&config);
  TEST_ASSERT_NOT_EQUAL(correct_checksum, calculated);
}

/**
 * @section Persistence Tests
 * Tests for configuration save/load cycles
 */

/**
 * @brief Simulate configuration storage (in-memory buffer)
 */
static uint8_t storage_buffer[512];
static size_t storage_size = 0;

/**
 * @brief Simulate saving configuration to storage
 */
static uint8_t config_save(const config_t *config) {
  if (!config || !config_is_valid(config))
    return 0;

  std::memcpy(storage_buffer, config, sizeof(config_t));
  storage_size = sizeof(config_t);
  return 1;
}

/**
 * @brief Simulate loading configuration from storage
 */
static uint8_t config_load(config_t *config) {
  if (!config || storage_size == 0)
    return 0;

  std::memcpy(config, storage_buffer, sizeof(config_t));

  // Verify checksum
  uint32_t calculated = config_calculate_checksum(config);
  if (calculated != config->checksum)
    return 0;

  return 1;
}

/**
 * @test Configuration can be saved
 */
void test_configuration_save(void) {
  config = config_create_default();
  uint8_t result = config_save(&config);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL(sizeof(config_t), storage_size);
}

/**
 * @test Configuration can be loaded
 */
void test_configuration_load(void) {
  // Save
  config = config_create_default();
  config_save(&config);

  // Load
  config_t loaded;
  uint8_t result = config_load(&loaded);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL(config.version, loaded.version);
  TEST_ASSERT_EQUAL(config.soft_limit_high_mm, loaded.soft_limit_high_mm);
  TEST_ASSERT_EQUAL(config.max_speed_hz, loaded.max_speed_hz);
}

/**
 * @test Save-load cycle preserves all values
 */
void test_save_load_roundtrip(void) {
  config = config_create_default();
  config.soft_limit_high_mm = 600;
  config.max_speed_hz = 90;
  config.encoder_ppm[0] = 125;

  // Update checksum
  config.checksum = config_calculate_checksum(&config);

  // Save and load
  config_save(&config);
  config_t loaded;
  config_load(&loaded);

  TEST_ASSERT_EQUAL(600, loaded.soft_limit_high_mm);
  TEST_ASSERT_EQUAL(90, loaded.max_speed_hz);
  TEST_ASSERT_EQUAL(125, loaded.encoder_ppm[0]);
}

/**
 * @test Load fails if storage is empty
 */
void test_load_fails_on_empty_storage(void) {
  storage_size = 0;
  config_t loaded;

  uint8_t result = config_load(&loaded);
  TEST_ASSERT_FALSE(result);
}

/**
 * @test Load fails if checksum corrupted
 */
void test_load_fails_on_corrupt_checksum(void) {
  // Save valid config
  config = config_create_default();
  config_save(&config);

  // Corrupt the stored checksum
  uint32_t *stored_checksum =
      (uint32_t *)(&storage_buffer[sizeof(config_t) - sizeof(uint32_t)]);
  *stored_checksum = 0xDEADBEEF;

  // Try to load - should fail
  config_t loaded;
  uint8_t result = config_load(&loaded);
  TEST_ASSERT_FALSE(result);
}

/**
 * @section Migration Tests
 * Tests for configuration version upgrades
 */

/**
 * @test Configuration version 0 can be detected
 */
void test_migration_detect_old_version(void) {
  config = config_create_default();
  config.version = 0; // Old version
  config.checksum = config_calculate_checksum(&config);

  // Should still be valid (no breaking changes in this mock)
  TEST_ASSERT_TRUE(config_is_valid(&config));
}

/**
 * @test Configuration migration preserves data
 */
void test_migration_preserve_values(void) {
  config = config_create_default();
  config.version = 1;
  config.soft_limit_high_mm = 750;
  config.checksum = config_calculate_checksum(&config);

  // Load and migrate
  config_t migrated = config;
  migrated.version = 2; // Upgrade version
  migrated.checksum = config_calculate_checksum(&migrated);

  TEST_ASSERT_TRUE(config_is_valid(&migrated));
  TEST_ASSERT_EQUAL(750, migrated.soft_limit_high_mm);
}

/**
 * @section Fixture Tests
 * Tests for configuration test fixture initialization
 */

/**
 * @test Fixture initializes with valid defaults
 */
void test_fixture_initialization(void) {
  config_test_fixture_t test_fixture;
  test_init_config_fixture(&test_fixture);

  TEST_ASSERT_EQUAL(0, test_fixture.soft_limit_low_mm);
  TEST_ASSERT_EQUAL(500, test_fixture.soft_limit_high_mm);
  TEST_ASSERT_EQUAL(105, test_fixture.max_speed_hz);
  TEST_ASSERT_EQUAL(1, test_fixture.min_speed_hz);
  TEST_ASSERT_EQUAL(3, test_fixture.axis_count);
}

/**
 * @brief Register all configuration tests
 * Called from test_runner.cpp
 */
void run_configuration_tests(void) {
  // Default configuration tests
  RUN_TEST(test_default_configuration_valid);
  RUN_TEST(test_default_soft_limits);
  RUN_TEST(test_default_vfd_settings);
  RUN_TEST(test_default_encoder_calibration);

  // Schema validation tests
  RUN_TEST(test_validation_rejects_invalid_version);
  RUN_TEST(test_validation_soft_limits_low_must_be_less);
  RUN_TEST(test_validation_soft_limit_high_max);
  RUN_TEST(test_validation_min_speed_too_low);
  RUN_TEST(test_validation_max_speed_too_high);
  RUN_TEST(test_validation_min_max_speed_order);
  RUN_TEST(test_validation_vfd_acc_time_minimum);
  RUN_TEST(test_validation_vfd_acc_time_maximum);
  RUN_TEST(test_validation_invalid_encoder_ppm);
  RUN_TEST(test_validation_axis_count_must_be_three);

  // Checksum tests
  RUN_TEST(test_checksum_calculation);
  RUN_TEST(test_checksum_detects_modification);
  RUN_TEST(test_checksum_detects_corruption);

  // Persistence tests
  RUN_TEST(test_configuration_save);
  RUN_TEST(test_configuration_load);
  RUN_TEST(test_save_load_roundtrip);
  RUN_TEST(test_load_fails_on_empty_storage);
  RUN_TEST(test_load_fails_on_corrupt_checksum);

  // Migration tests
  RUN_TEST(test_migration_detect_old_version);
  RUN_TEST(test_migration_preserve_values);

  // Fixture tests
  RUN_TEST(test_fixture_initialization);
}

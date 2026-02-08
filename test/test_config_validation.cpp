/**
 * @file test/test_config_validation.cpp
 * @brief Unit tests for configuration validation logic
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include "config_keys.h"

// Logic from config_unified.cpp (re-implemented for testing in native env)
static int32_t validateInt(const char *key, int32_t value) {
  if (strstr(key, "ppm_") != NULL) {
    if (value <= 0) return 1000; 
  }
  if (strcmp(key, KEY_STALL_TIMEOUT) == 0) {
    return (value < 100) ? 100 : (value > 60000 ? 60000 : value);
  }
  if (strstr(key, "home_prof_") != NULL) {
    return (value < 0) ? 0 : (value > 2 ? 2 : value);
  }
  return value;
}

static float validateFloat(const char *key, float value) {
  if (strstr(key, "default_") != NULL && (strstr(key, "accel") || strstr(key, "speed"))) {
    if (value < 0.1f) return 0.1f;
  }
  return value;
}

static void validateString(const char *key, char *value, size_t len) {
  const size_t MIN_PASSWORD_LENGTH = 8;
  if (strcmp(key, KEY_WEB_USERNAME) == 0 || strcmp(key, KEY_WEB_PASSWORD) == 0) {
    if (value[0] == '\0') {
      if (strcmp(key, KEY_WEB_USERNAME) == 0) strncpy(value, "admin", len - 1);
      else strncpy(value, "password", len - 1);
      value[len - 1] = '\0';
    }
    if (strcmp(key, KEY_WEB_PASSWORD) == 0 && strlen(value) < MIN_PASSWORD_LENGTH) {
      strncpy(value, "password", len - 1);
      value[len - 1] = '\0';
    }
    if (strcmp(key, KEY_WEB_USERNAME) == 0 && strlen(value) < 4) {
      strncpy(value, "admin", len - 1);
      value[len - 1] = '\0';
    }
  }
}

// @test validateInt for PPM values
void test_validate_int_ppm(void) {
    // PPM must be > 0
    TEST_ASSERT_EQUAL(500, validateInt("ppm_x", 500));
    TEST_ASSERT_EQUAL(1000, validateInt("ppm_x", 0));
    TEST_ASSERT_EQUAL(1000, validateInt("ppm_x", -10));
}

// @test validateInt for stall timeout
void test_validate_int_stall_timeout(void) {
    // Range 100-60000
    TEST_ASSERT_EQUAL(100, validateInt(KEY_STALL_TIMEOUT, 50));
    TEST_ASSERT_EQUAL(5000, validateInt(KEY_STALL_TIMEOUT, 5000));
    TEST_ASSERT_EQUAL(60000, validateInt(KEY_STALL_TIMEOUT, 70000));
}

// @test validateInt for homing profiles
void test_validate_int_homing_profile(void) {
    // Range 0-2
    TEST_ASSERT_EQUAL(0, validateInt("home_prof_fast", -1));
    TEST_ASSERT_EQUAL(1, validateInt("home_prof_fast", 1));
    TEST_ASSERT_EQUAL(2, validateInt("home_prof_fast", 5));
}

// @test validateFloat for accel/speed
void test_validate_float_motion(void) {
    // Must be >= 0.1
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, validateFloat("default_accel", 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.1f, validateFloat("default_accel", 0.05f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.1f, validateFloat("default_speed", -1.0f));
}

// @test validateString for security (passwords/usernames)
void test_validate_string_security(void) {
    char buffer[32];

    // Password too short (<8)
    strcpy(buffer, "123");
    validateString(KEY_WEB_PASSWORD, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("password", buffer);

    // Password valid (>=8)
    strcpy(buffer, "secret123");
    validateString(KEY_WEB_PASSWORD, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("secret123", buffer);

    // Username too short (<4)
    strcpy(buffer, "me");
    validateString(KEY_WEB_USERNAME, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("admin", buffer);

    // Username valid (>=4)
    strcpy(buffer, "user1");
    validateString(KEY_WEB_USERNAME, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("user1", buffer);

    // Empty username
    strcpy(buffer, "");
    validateString(KEY_WEB_USERNAME, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("admin", buffer);
}

void run_config_validation_tests(void) {
    RUN_TEST(test_validate_int_ppm);
    RUN_TEST(test_validate_int_stall_timeout);
    RUN_TEST(test_validate_int_homing_profile);
    RUN_TEST(test_validate_float_motion);
    RUN_TEST(test_validate_string_security);
}

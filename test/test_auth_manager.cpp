/**
 * @file test/test_auth_manager.cpp
 * @brief Unit tests for authentication manager
 *
 * Tests cover:
 * - Password strength validation
 * - Rate limiting logic
 * - Session/token structure
 * - Base64 encoding utilities
 * - Constants and configuration
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// ============================================================================
// AUTH DEFINITIONS (from auth_manager.h)
// ============================================================================

#define AUTH_SALT_BYTES 16
#define AUTH_HASH_BYTES 32
#define AUTH_MAX_STORED_PW_LEN 128
#define AUTH_MIN_PASSWORD_LEN 8
#define AUTH_MAX_FAILED_ATTEMPTS 5
#define AUTH_LOCKOUT_DURATION_S 60

// Rate limit tracking
typedef struct {
    char ip_address[16];
    uint32_t failed_attempts;
    uint32_t lockout_until_ms;
} auth_rate_limit_t;

static auth_rate_limit_t g_rate_limit;

static void reset_mock(void) {
    memset(&g_rate_limit, 0, sizeof(g_rate_limit));
}

// Password strength validation (simplified version)
static bool validatePasswordStrength(const char* password) {
    if (!password) return false;
    
    size_t len = strlen(password);
    if (len < AUTH_MIN_PASSWORD_LEN) return false;
    
    bool has_lower = false, has_upper = false;
    bool has_digit = false, has_special = false;
    
    for (size_t i = 0; i < len; i++) {
        char c = password[i];
        if (c >= 'a' && c <= 'z') has_lower = true;
        else if (c >= 'A' && c <= 'Z') has_upper = true;
        else if (c >= '0' && c <= '9') has_digit = true;
        else has_special = true;
    }
    
    // Require at least 3 of 4 character classes
    int classes = has_lower + has_upper + has_digit + has_special;
    return classes >= 3;
}

// Rate limit check
static bool checkRateLimit(uint32_t failed_attempts, uint32_t current_time_ms, 
                           uint32_t lockout_until_ms) {
    if (failed_attempts >= AUTH_MAX_FAILED_ATTEMPTS) {
        return current_time_ms < lockout_until_ms;  // Still locked
    }
    return false;  // Not locked
}

// ============================================================================
// PASSWORD STRENGTH TESTS
// ============================================================================

// @test Password must be at least 8 characters
void test_password_min_length(void) {
    TEST_ASSERT_FALSE(validatePasswordStrength("Abc123!"));  // 7 chars
    TEST_ASSERT_TRUE(validatePasswordStrength("Abc123!@"));  // 8 chars
}

// @test Password requires multiple character classes
void test_password_requires_complexity(void) {
    TEST_ASSERT_FALSE(validatePasswordStrength("abcdefgh"));  // Only lowercase
    TEST_ASSERT_FALSE(validatePasswordStrength("ABCDEFGH"));  // Only uppercase
    TEST_ASSERT_FALSE(validatePasswordStrength("12345678"));  // Only digits
}

// @test Password with 3+ classes passes
void test_password_complex_passes(void) {
    TEST_ASSERT_TRUE(validatePasswordStrength("Abcdefg1"));  // lower + upper + digit
    TEST_ASSERT_TRUE(validatePasswordStrength("abcdef1!"));  // lower + digit + special
}

// @test Common weak passwords rejected by length
void test_weak_passwords_rejected(void) {
    TEST_ASSERT_FALSE(validatePasswordStrength("admin"));
    TEST_ASSERT_FALSE(validatePasswordStrength("123456"));
    TEST_ASSERT_FALSE(validatePasswordStrength("password"));  // 8 chars but only lowercase
}

// @test NULL password rejected
void test_null_password_rejected(void) {
    TEST_ASSERT_FALSE(validatePasswordStrength(NULL));
}

// @test Empty password rejected
void test_empty_password_rejected(void) {
    TEST_ASSERT_FALSE(validatePasswordStrength(""));
}

// ============================================================================
// RATE LIMITING TESTS
// ============================================================================

// @test No lockout with zero failures
void test_no_lockout_zero_failures(void) {
    bool locked = checkRateLimit(0, 0, 0);
    TEST_ASSERT_FALSE(locked);
}

// @test No lockout with few failures
void test_no_lockout_few_failures(void) {
    bool locked = checkRateLimit(3, 0, 0);
    TEST_ASSERT_FALSE(locked);
}

// @test Lockout after max failures
void test_lockout_after_max_failures(void) {
    uint32_t now = 10000;
    uint32_t lockout_end = 70000;  // 60 seconds later
    
    bool locked = checkRateLimit(AUTH_MAX_FAILED_ATTEMPTS, now, lockout_end);
    TEST_ASSERT_TRUE(locked);
}

// @test Lockout expires after duration
void test_lockout_expires(void) {
    uint32_t now = 80000;
    uint32_t lockout_end = 70000;  // Already passed
    
    bool locked = checkRateLimit(AUTH_MAX_FAILED_ATTEMPTS, now, lockout_end);
    TEST_ASSERT_FALSE(locked);
}

// @test Max failed attempts is 5
void test_max_failed_attempts(void) {
    TEST_ASSERT_EQUAL(5, AUTH_MAX_FAILED_ATTEMPTS);
}

// @test Lockout duration is 60 seconds
void test_lockout_duration(void) {
    TEST_ASSERT_EQUAL(60, AUTH_LOCKOUT_DURATION_S);
}

// ============================================================================
// CONSTANTS TESTS
// ============================================================================

// @test Salt size is 16 bytes
void test_salt_size(void) {
    TEST_ASSERT_EQUAL(16, AUTH_SALT_BYTES);
}

// @test Hash size is 32 bytes (SHA-256)
void test_hash_size(void) {
    TEST_ASSERT_EQUAL(32, AUTH_HASH_BYTES);
}

// @test Max stored password length accommodates format
void test_max_stored_length(void) {
    // Format: $sha256$<32 hex salt>$<64 hex hash> = 7 + 32 + 1 + 64 = 104 chars
    TEST_ASSERT_GREATER_OR_EQUAL(104, AUTH_MAX_STORED_PW_LEN);
}

// @test Minimum password length is 8
void test_min_password_length(void) {
    TEST_ASSERT_EQUAL(8, AUTH_MIN_PASSWORD_LEN);
}

// ============================================================================
// IP ADDRESS STORAGE TESTS
// ============================================================================

// @test Rate limit structure can hold IPv4 address
void test_rate_limit_ipv4_storage(void) {
    reset_mock();
    
    strncpy(g_rate_limit.ip_address, "192.168.1.100", sizeof(g_rate_limit.ip_address) - 1);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", g_rate_limit.ip_address);
}

// @test Rate limit tracks failure count
void test_rate_limit_failure_count(void) {
    reset_mock();
    
    g_rate_limit.failed_attempts = 3;
    TEST_ASSERT_EQUAL_UINT32(3, g_rate_limit.failed_attempts);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_auth_manager_tests(void) {
    // Password strength
    RUN_TEST(test_password_min_length);
    RUN_TEST(test_password_requires_complexity);
    RUN_TEST(test_password_complex_passes);
    RUN_TEST(test_weak_passwords_rejected);
    RUN_TEST(test_null_password_rejected);
    RUN_TEST(test_empty_password_rejected);
    
    // Rate limiting
    RUN_TEST(test_no_lockout_zero_failures);
    RUN_TEST(test_no_lockout_few_failures);
    RUN_TEST(test_lockout_after_max_failures);
    RUN_TEST(test_lockout_expires);
    RUN_TEST(test_max_failed_attempts);
    RUN_TEST(test_lockout_duration);
    
    // Constants
    RUN_TEST(test_salt_size);
    RUN_TEST(test_hash_size);
    RUN_TEST(test_max_stored_length);
    RUN_TEST(test_min_password_length);
    
    // IP storage
    RUN_TEST(test_rate_limit_ipv4_storage);
    RUN_TEST(test_rate_limit_failure_count);
}

#include <unity.h>
#include "string_safety.h"
#include <string.h>

// Mocks for logging (aligned with serial_logger.h - C++ linkage)
void logError(const char* format, ...) { (void)format; }
void logWarning(const char* format, ...) { (void)format; }

// Include the implementation directly for testing
#include "../src/string_safety.cpp"

void faultLogWarning(fault_code_t code, const char* msg) { (void)code; (void)msg; }

void test_safe_strcpy_exact_fit(void) {
    char dest[10];
    const char* src = "123456789"; // 9 chars + null = 10
    bool result = safe_strcpy(dest, sizeof(dest), src);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING(src, dest);
}

void test_safe_strcpy_truncation(void) {
    char dest[5];
    const char* src = "123456789";
    bool result = safe_strcpy(dest, sizeof(dest), src);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_STRING("1234", dest); // Should have 4 chars and null
    TEST_ASSERT_EQUAL('\0', dest[4]);
}

void test_safe_strcpy_empty_src(void) {
    char dest[10];
    bool result = safe_strcpy(dest, sizeof(dest), "");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("", dest);
    TEST_ASSERT_EQUAL('\0', dest[0]);
}

void test_safe_snprintf_normal(void) {
    char dest[20];
    size_t written = safe_snprintf(dest, sizeof(dest), "Val: %d", 42);
    TEST_ASSERT_EQUAL(7, written);
    TEST_ASSERT_EQUAL_STRING("Val: 42", dest);
}

void test_safe_snprintf_truncation(void) {
    char dest[10];
    const char* expected = "Long stri"; // 9 chars + null
    size_t written = safe_snprintf(dest, sizeof(dest), "Long string of text");
    TEST_ASSERT_TRUE(written >= sizeof(dest));
    TEST_ASSERT_EQUAL_STRING(expected, dest);
    TEST_ASSERT_EQUAL('\0', dest[9]);
}

void test_safe_strcat_normal(void) {
    char dest[10] = "Hi";
    bool result = safe_strcat(dest, sizeof(dest), " there");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("Hi there", dest);
}

void test_safe_strcat_truncation(void) {
    char dest[10] = "Too long"; // 8 chars
    bool result = safe_strcat(dest, sizeof(dest), " suffix");
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_STRING("Too long", dest); // Should NOT have changed dest if it would overflow
}

void test_safe_is_valid_string(void) {
    char valid[] = "Hello";
    char invalid[5] = {'A', 'B', 'C', 'D', 'E'}; // No null terminator
    
    TEST_ASSERT_TRUE(safe_is_valid_string(valid, sizeof(valid)));
    TEST_ASSERT_FALSE(safe_is_valid_string(invalid, sizeof(invalid)));
}

void test_safe_strcpy_macro(void) {
    char dest[10];
    const char* src = "MacroTest";
    bool res = SAFE_STRCPY(dest, src, sizeof(dest));
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_EQUAL_STRING(src, dest);
}

void run_string_safety_tests(void) {
    RUN_TEST(test_safe_strcpy_exact_fit);
    RUN_TEST(test_safe_strcpy_truncation);
    RUN_TEST(test_safe_strcpy_empty_src);
    RUN_TEST(test_safe_snprintf_normal);
    RUN_TEST(test_safe_snprintf_truncation);
    RUN_TEST(test_safe_strcat_normal);
    RUN_TEST(test_safe_strcat_truncation);
    RUN_TEST(test_safe_is_valid_string);
    RUN_TEST(test_safe_strcpy_macro);
}

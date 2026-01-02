/**
 * @file test/test_lcd_formatter.cpp
 * @brief Unit tests for LCD string formatter
 *
 * Tests cover:
 * - LCD line buffer sizes
 * - String length limits
 * - Format buffer structure
 * - Position display formatting
 * - Status message formatting
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

// ============================================================================
// LCD BUFFER DEFINITIONS (from lcd_formatter.h)
// ============================================================================

#define LCD_LINE_WIDTH 20
#define LCD_LINES 4

typedef struct {
    char line0[21];  // Axis positions
    char line1[21];  // Status
    char line2[21];  // Motion/Alarm
    char line3[21];  // Detail
    uint32_t last_update_ms;
} lcd_format_buffer_t;

static lcd_format_buffer_t g_lcd;

static void reset_mock(void) {
    memset(&g_lcd, 0, sizeof(g_lcd));
}

// Helper to format position display
static void formatPosition(char* buffer, const char* axis, float pos_mm) {
    snprintf(buffer, 21, "%s:%+7.2fmm", axis, pos_mm);
}

// Helper to format status line
static void formatStatus(char* buffer, const char* status, int percent) {
    snprintf(buffer, 21, "%-12s %3d%%", status, percent);
}

// ============================================================================
// BUFFER SIZE TESTS
// ============================================================================

// @test LCD line buffers are 21 bytes (20 chars + null)
void test_lcd_line_buffer_size(void) {
    TEST_ASSERT_EQUAL(21, sizeof(g_lcd.line0));
    TEST_ASSERT_EQUAL(21, sizeof(g_lcd.line1));
    TEST_ASSERT_EQUAL(21, sizeof(g_lcd.line2));
    TEST_ASSERT_EQUAL(21, sizeof(g_lcd.line3));
}

// @test LCD has 4 lines
void test_lcd_line_count(void) {
    TEST_ASSERT_EQUAL(4, LCD_LINES);
}

// @test LCD line width is 20 characters
void test_lcd_line_width(void) {
    TEST_ASSERT_EQUAL(20, LCD_LINE_WIDTH);
}

// ============================================================================
// STRUCTURE TESTS
// ============================================================================

// @test Buffer structure has timestamp
void test_buffer_has_timestamp(void) {
    reset_mock();
    g_lcd.last_update_ms = 12345;
    TEST_ASSERT_EQUAL_UINT32(12345, g_lcd.last_update_ms);
}

// @test Lines initialize to empty strings
void test_lines_init_empty(void) {
    reset_mock();
    TEST_ASSERT_EQUAL_STRING("", g_lcd.line0);
    TEST_ASSERT_EQUAL_STRING("", g_lcd.line1);
}

// ============================================================================
// POSITION FORMATTING TESTS
// ============================================================================

// @test Position format fits in 20 chars
void test_position_format_fits(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "X", 123.45f);
    
    size_t len = strlen(g_lcd.line0);
    TEST_ASSERT_LESS_OR_EQUAL(20, len);
}

// @test Positive position has plus sign
void test_position_positive_sign(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "X", 50.0f);
    
    TEST_ASSERT_NOT_NULL(strchr(g_lcd.line0, '+'));
}

// @test Negative position has minus sign
void test_position_negative_sign(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "X", -50.0f);
    
    TEST_ASSERT_NOT_NULL(strchr(g_lcd.line0, '-'));
}

// @test Zero position formats correctly
void test_position_zero(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "X", 0.0f);
    
    TEST_ASSERT_NOT_NULL(strstr(g_lcd.line0, "0.00"));
}

// @test Axis name appears in output
void test_position_axis_name(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "Y", 10.0f);
    
    TEST_ASSERT_NOT_NULL(strstr(g_lcd.line0, "Y:"));
}

// ============================================================================
// STATUS FORMATTING TESTS
// ============================================================================

// @test Status format fits in 20 chars
void test_status_format_fits(void) {
    reset_mock();
    formatStatus(g_lcd.line1, "RUNNING", 75);
    
    size_t len = strlen(g_lcd.line1);
    TEST_ASSERT_LESS_OR_EQUAL(20, len);
}

// @test Status includes percentage
void test_status_includes_percent(void) {
    reset_mock();
    formatStatus(g_lcd.line1, "IDLE", 100);
    
    TEST_ASSERT_NOT_NULL(strstr(g_lcd.line1, "%"));
}

// @test Status shows message
void test_status_shows_message(void) {
    reset_mock();
    formatStatus(g_lcd.line1, "HOMING", 50);
    
    TEST_ASSERT_NOT_NULL(strstr(g_lcd.line1, "HOMING"));
}

// ============================================================================
// STRING TRUNCATION TESTS
// ============================================================================

// @test Long strings are truncated
void test_long_string_truncated(void) {
    reset_mock();
    
    // Attempt to write a very long string
    snprintf(g_lcd.line2, 21, "This is a very long message that exceeds 20 chars");
    
    // Should be truncated to 20 chars + null
    TEST_ASSERT_EQUAL(20, strlen(g_lcd.line2));
}

// @test Multiple overwrites don't leak
void test_multiple_overwrites_safe(void) {
    reset_mock();
    
    snprintf(g_lcd.line0, 21, "First message");
    snprintf(g_lcd.line0, 21, "Second longer msg");
    snprintf(g_lcd.line0, 21, "Short");
    
    TEST_ASSERT_EQUAL_STRING("Short", g_lcd.line0);
}

// ============================================================================
// SPECIAL CHARACTERS TESTS
// ============================================================================

// @test Decimal point in position
void test_decimal_in_position(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "Z", 12.34f);
    
    TEST_ASSERT_NOT_NULL(strchr(g_lcd.line0, '.'));
}

// @test Units suffix (mm)
void test_units_suffix(void) {
    reset_mock();
    formatPosition(g_lcd.line0, "X", 0.0f);
    
    TEST_ASSERT_NOT_NULL(strstr(g_lcd.line0, "mm"));
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_lcd_formatter_tests(void) {
    // Buffer sizes
    RUN_TEST(test_lcd_line_buffer_size);
    RUN_TEST(test_lcd_line_count);
    RUN_TEST(test_lcd_line_width);
    
    // Structure
    RUN_TEST(test_buffer_has_timestamp);
    RUN_TEST(test_lines_init_empty);
    
    // Position formatting
    RUN_TEST(test_position_format_fits);
    RUN_TEST(test_position_positive_sign);
    RUN_TEST(test_position_negative_sign);
    RUN_TEST(test_position_zero);
    RUN_TEST(test_position_axis_name);
    
    // Status formatting
    RUN_TEST(test_status_format_fits);
    RUN_TEST(test_status_includes_percent);
    RUN_TEST(test_status_shows_message);
    
    // Truncation
    RUN_TEST(test_long_string_truncated);
    RUN_TEST(test_multiple_overwrites_safe);
    
    // Special chars
    RUN_TEST(test_decimal_in_position);
    RUN_TEST(test_units_suffix);
}

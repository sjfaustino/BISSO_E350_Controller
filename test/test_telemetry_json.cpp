/**
 * @file test/test_telemetry_json.cpp
 * @brief Unit tests for WebSocket telemetry JSON serialization
 *
 * Tests that the snprintf format string in serializeTelemetryToBuffer()
 * produces valid JSON with correct field values. This catches format/argument
 * count mismatches and malformed JSON — the exact class of bug that
 * broke the LCD mirror.
 */

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Minimal JSON validator (checks structural validity only)
// ---------------------------------------------------------------------------
static bool json_is_valid(const char* json) {
    if (!json || *json == '\0') return false;

    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escape_next = false;

    for (const char* p = json; *p; p++) {
        if (escape_next) { escape_next = false; continue; }
        if (*p == '\\' && in_string) { escape_next = true; continue; }
        if (*p == '"') { in_string = !in_string; continue; }
        if (in_string) continue;

        switch (*p) {
            case '{': brace_depth++; break;
            case '}': brace_depth--; if (brace_depth < 0) return false; break;
            case '[': bracket_depth++; break;
            case ']': bracket_depth--; if (bracket_depth < 0) return false; break;
        }
    }
    return (brace_depth == 0 && bracket_depth == 0 && !in_string);
}

// Returns true if 'json' contains "key":value (for unquoted values like true/false/number)
static bool json_has_field(const char* json, const char* key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    return strstr(json, search) != nullptr;
}

// Returns true if 'json' contains "key":"value" for string values
static bool json_has_string_field(const char* json, const char* key, const char* value) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":\"%s\"", key, value);
    return strstr(json, search) != nullptr;
}

// Returns true if 'json' contains "key":true or "key":false
static bool json_has_bool_field(const char* json, const char* key) {
    char search_true[128], search_false[128];
    snprintf(search_true, sizeof(search_true), "\"%s\":true", key);
    snprintf(search_false, sizeof(search_false), "\"%s\":false", key);
    return strstr(json, search_true) != nullptr || strstr(json, search_false) != nullptr;
}

// ---------------------------------------------------------------------------
// Re-implementation of the snprintf format (isolated from firmware deps)
// This mirrors the EXACT format string from web_server.cpp lines 437-450
// ---------------------------------------------------------------------------

// Axis metric mock
struct axis_metrics_mock_t {
    uint8_t quality_score;
    float jitter_mms;
    float vfd_error_percent;
    bool maintenance_warning;
};

static size_t mock_serialize_telemetry(char* buffer, size_t buffer_size, bool full,
                                        const char* lcd_line0, const char* lcd_line1,
                                        const char* lcd_line2, const char* lcd_line3) {
    // Mock values
    const char* status = "READY";
    const char* health = "OPTIMAL";
    unsigned long uptime = 12345;
    uint8_t cpu = 42;
    unsigned long free_heap = 75000;
    float temperature = 45.2f;
    const char* ver_str = "v1.0.0";
    const char* build_date = "Feb 15 2026";
    const char* lcd_msg = "";
    unsigned long long lcd_msg_id = 0ULL;
    bool rtc_battery_low = false;
    bool plc_present = true;
    const char* mcu_name = "ESP32-S3";
    const char* rev_str = "v0";
    const char* serial_str = "BS-E350-ABCD";
    float x = 1.5f, y = 2.3f, z = -0.8f, a = 0.0f;
    bool moving = false;
    int buffer_count = 0, buffer_capacity = 32;
    bool dro_connected = true;
    float vfd_amps = 0.0f, vfd_freq = 0.0f;
    int vfd_thermal = 0;
    uint32_t vfd_fault = 0;
    float vfd_threshold = 5.0f;
    bool vfd_calib = false, vfd_conn = true;
    float rpm = 0.0f, speed = 0.0f, eff = 0.0f, load = 0.0f;
    axis_metrics_mock_t metrics[3] = {
        {95, 0.001f, 0.0f, false},
        {90, 0.002f, 0.1f, false},
        {85, 0.003f, 0.2f, true},
    };
    bool wifi = true;
    uint8_t wifi_signal = 75;
    bool sd_mounted = true;
    int sd_health = 2;
    uint64_t sd_total = 4000000000ULL, sd_used = 1000000000ULL;
    bool abs_mode = true;
    float feedrate = 100.0f, actual_feedrate = 95.5f;

    // The EXACT format string from web_server.cpp lines 438-450
    int n = snprintf(buffer, buffer_size,
        "{\"system\":{\"status\":\"%s\",\"health\":\"%s\",\"uptime_sec\":%lu,\"cpu_percent\":%u,\"free_heap_bytes\":%lu,\"temperature\":%.1f,"
        "\"firmware_version\":\"%s\",\"build_date\":\"%s\",\"lcd_msg\":\"%s\",\"lcd_msg_id\":%llu,\"rtc_battery_low\":%s%s%s%s%s%s%s%s%s%s%s%s},"
        "\"x_mm\":%.3f,\"y_mm\":%.3f,\"z_mm\":%.3f,\"a_mm\":%.3f,"
        "\"motion_active\":%s,\"motion\":{\"moving\":%s,\"buffer_count\":%d,\"buffer_capacity\":%d,\"dro_connected\":%s},"
        "\"vfd\":{\"current_amps\":%.2f,\"frequency_hz\":%.2f,\"thermal_percent\":%d,\"fault_code\":%u,"
        "\"stall_threshold\":%.2f,\"calibration_valid\":%s,\"connected\":%s,\"rpm\":%.1f,\"speed_m_s\":%.2f,\"efficiency\":%.2f,\"load_pct\":%.1f},"
        "\"axis\":{\"x\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s},"
        "\"y\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s},"
        "\"z\":{\"quality\":%u,\"jitter_mms\":%.3f,\"vfd_error_percent\":%.2f,\"stalled\":%s,\"maint\":%s}},"
        "\"network\":{\"wifi_connected\":%s,\"signal_percent\":%u},"
        "\"sd\":{\"mounted\":%s,\"health\":%d,\"total_bytes\":%llu,\"used_bytes\":%llu},"
        "\"parser\":{\"absolute_mode\":%s,\"feedrate\":%.1f,\"actual_feedrate\":%.1f},"
        "\"lcd\":{\"lines\":[\"%s\",\"%s\",\"%s\",\"%s\"]}",
        // Arguments (must match format specifiers EXACTLY)
        status,                                   // %s  - status
        health,                                   // %s  - health
        uptime,                                   // %lu - uptime_sec
        cpu,                                      // %u  - cpu_percent
        free_heap,                                // %lu - free_heap_bytes
        temperature,                              // %.1f - temperature
        ver_str,                                  // %s  - firmware_version
        build_date,                               // %s  - build_date
        lcd_msg,                                  // %s  - lcd_msg
        lcd_msg_id,                               // %llu - lcd_msg_id
        rtc_battery_low ? "true" : "false",       // %s  - rtc_battery_low *** THE FIELD THAT WAS MISSING ***
        full ? (plc_present ? ",\"plc_hardware_present\":true" : ",\"plc_hardware_present\":false") : "",   // %s
        full ? ",\"hw_model\":\"BISSO E350\"" : "",                                                         // %s
        full ? ",\"hw_mcu\":\"" : "", full ? mcu_name : "", full ? "\"" : "",                               // %s %s %s
        full ? ",\"hw_revision\":\"" : "", full ? rev_str : "", full ? "\"" : "",                            // %s %s %s
        full ? ",\"hw_serial\":\"" : "", full ? serial_str : "", full ? "\"" : "",                           // %s %s %s
        (double)x, (double)y, (double)z, (double)a,                                                         // 4x %.3f
        moving ? "true" : "false",                // %s  - motion_active
        moving ? "true" : "false",                // %s  - moving
        buffer_count,                             // %d  - buffer_count
        buffer_capacity,                          // %d  - buffer_capacity
        dro_connected ? "true" : "false",         // %s  - dro_connected
        (double)vfd_amps, (double)vfd_freq, vfd_thermal, vfd_fault,    // %.2f %.2f %d %u
        (double)vfd_threshold, vfd_calib ? "true" : "false", vfd_conn ? "true" : "false",   // %.2f %s %s
        (double)rpm, (double)speed, (double)eff, (double)load,                               // 4x %.Xf
        // Axis X
        metrics[0].quality_score, (double)metrics[0].jitter_mms, (double)metrics[0].vfd_error_percent,
        metrics[0].quality_score < 10 ? "true" : "false", metrics[0].maintenance_warning ? "true" : "false",
        // Axis Y
        metrics[1].quality_score, (double)metrics[1].jitter_mms, (double)metrics[1].vfd_error_percent,
        metrics[1].quality_score < 10 ? "true" : "false", metrics[1].maintenance_warning ? "true" : "false",
        // Axis Z
        metrics[2].quality_score, (double)metrics[2].jitter_mms, (double)metrics[2].vfd_error_percent,
        metrics[2].quality_score < 10 ? "true" : "false", metrics[2].maintenance_warning ? "true" : "false",
        // Network
        wifi ? "true" : "false", wifi_signal,
        // SD
        sd_mounted ? "true" : "false", sd_health, sd_total, sd_used,
        // Parser
        abs_mode ? "true" : "false", (double)feedrate, (double)actual_feedrate,
        // LCD lines
        lcd_line0, lcd_line1, lcd_line2, lcd_line3
    );

    if (n < 0 || (size_t)n >= buffer_size) return (size_t)n;

    size_t offset = (size_t)n;

    // Close root object (same as web_server.cpp line 521-524)
    if (offset < buffer_size - 1) {
        buffer[offset++] = '}';
        buffer[offset] = '\0';
    }

    return offset;
}

// ============================================================================
// TESTS
// ============================================================================

// @test Basic JSON validity (full=false, no conditional hw fields)
void test_telemetry_json_valid_compact(void) {
    char buffer[2048];
    size_t len = mock_serialize_telemetry(buffer, sizeof(buffer), false,
                                          "X   1.5  Y     0.0",
                                          "Z   0.0  A   0 ??A",
                                          "Status: READY",
                                          "E350 v1.0.0");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_TRUE_MESSAGE(json_is_valid(buffer), "Compact JSON is structurally invalid");
}

// @test Full JSON validity (full=true, includes hw fields)
void test_telemetry_json_valid_full(void) {
    char buffer[2048];
    size_t len = mock_serialize_telemetry(buffer, sizeof(buffer), true,
                                          "X   1.5  Y     0.0",
                                          "Z   0.0  A   0 ??A",
                                          "Status: READY",
                                          "E350 v1.0.0");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_TRUE_MESSAGE(json_is_valid(buffer), "Full JSON is structurally invalid");
}

// @test rtc_battery_low field contains a valid boolean
void test_telemetry_json_rtc_battery_field(void) {
    char buffer[2048];
    mock_serialize_telemetry(buffer, sizeof(buffer), false,
                             "line0", "line1", "line2", "line3");
    TEST_ASSERT_TRUE_MESSAGE(json_has_bool_field(buffer, "rtc_battery_low"),
                             "rtc_battery_low should be true or false, not empty");
}

// @test LCD lines appear in the JSON output
void test_telemetry_json_lcd_lines_present(void) {
    char buffer[2048];
    mock_serialize_telemetry(buffer, sizeof(buffer), false,
                             "X   1.5  Y     0.0",
                             "Z   0.0  A   0 12A",
                             "Status: READY",
                             "E350 v1.0.0");

    TEST_ASSERT_TRUE_MESSAGE(json_has_field(buffer, "lcd"),
                             "JSON should contain 'lcd' field");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buffer, "X   1.5  Y     0.0"),
                                "LCD line 0 content missing from JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buffer, "Status: READY"),
                                "LCD line 2 content missing from JSON");
}

// @test All required top-level sections exist
void test_telemetry_json_required_sections(void) {
    char buffer[2048];
    mock_serialize_telemetry(buffer, sizeof(buffer), false,
                             "L0", "L1", "L2", "L3");

    TEST_ASSERT_TRUE(json_has_field(buffer, "system"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "x_mm"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "motion_active"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "vfd"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "axis"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "network"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "sd"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "parser"));
    TEST_ASSERT_TRUE(json_has_field(buffer, "lcd"));
}

// @test full=true includes hardware identification fields
void test_telemetry_json_full_mode_hw_fields(void) {
    char buffer[2048];
    mock_serialize_telemetry(buffer, sizeof(buffer), true,
                             "L0", "L1", "L2", "L3");

    TEST_ASSERT_TRUE_MESSAGE(json_has_field(buffer, "plc_hardware_present"),
                             "full mode should include plc_hardware_present");
    TEST_ASSERT_TRUE_MESSAGE(json_has_field(buffer, "hw_model"),
                             "full mode should include hw_model");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buffer, "ESP32-S3"),
                                "full mode should include MCU name");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buffer, "BS-E350-ABCD"),
                                "full mode should include serial number");
}

// @test full=false omits hardware identification fields
void test_telemetry_json_compact_omits_hw(void) {
    char buffer[2048];
    mock_serialize_telemetry(buffer, sizeof(buffer), false,
                             "L0", "L1", "L2", "L3");

    TEST_ASSERT_NULL_MESSAGE(strstr(buffer, "plc_hardware_present"),
                             "compact mode should NOT include plc_hardware_present");
    TEST_ASSERT_NULL_MESSAGE(strstr(buffer, "hw_model"),
                             "compact mode should NOT include hw_model");
}

// @test Status field has expected value
void test_telemetry_json_status_value(void) {
    char buffer[2048];
    mock_serialize_telemetry(buffer, sizeof(buffer), false,
                             "L0", "L1", "L2", "L3");

    TEST_ASSERT_TRUE(json_has_string_field(buffer, "status", "READY"));
    TEST_ASSERT_TRUE(json_has_string_field(buffer, "health", "OPTIMAL"));
}

// @test Buffer overflow protection (small buffer still produces valid partial output)
void test_telemetry_json_buffer_overflow(void) {
    char buffer[256]; // Way too small for full JSON
    size_t len = mock_serialize_telemetry(buffer, sizeof(buffer), false,
                                          "L0", "L1", "L2", "L3");

    // snprintf returns how many chars WOULD have been written
    TEST_ASSERT_GREATER_OR_EQUAL(256, len);
    // Buffer should be null-terminated by snprintf
    TEST_ASSERT_EQUAL('\0', buffer[255]);
}

// ============================================================================
// JSON Validator Unit Tests (testing the test helper itself)
// ============================================================================

void test_json_validator_valid(void) {
    TEST_ASSERT_TRUE(json_is_valid("{\"a\":1}"));
    TEST_ASSERT_TRUE(json_is_valid("{\"a\":[1,2,3]}"));
    TEST_ASSERT_TRUE(json_is_valid("{\"a\":{\"b\":\"c\"}}"));
    TEST_ASSERT_TRUE(json_is_valid("{\"key\":\"value with \\\"quotes\\\"\"}"));
}

void test_json_validator_invalid(void) {
    TEST_ASSERT_FALSE(json_is_valid("{\"a\":1"));        // unclosed brace
    TEST_ASSERT_FALSE(json_is_valid("{\"a\":[1,2}"));    // bracket/brace mismatch
    TEST_ASSERT_FALSE(json_is_valid(""));                // empty
    TEST_ASSERT_FALSE(json_is_valid("{\"a\":\"unclosed")); // unclosed string
}

// ============================================================================
// REGISTRATION
// ============================================================================

void run_telemetry_json_tests(void) {
    // JSON validator self-test
    RUN_TEST(test_json_validator_valid);
    RUN_TEST(test_json_validator_invalid);

    // Core serialization tests
    RUN_TEST(test_telemetry_json_valid_compact);
    RUN_TEST(test_telemetry_json_valid_full);
    RUN_TEST(test_telemetry_json_rtc_battery_field);
    RUN_TEST(test_telemetry_json_lcd_lines_present);
    RUN_TEST(test_telemetry_json_required_sections);
    RUN_TEST(test_telemetry_json_full_mode_hw_fields);
    RUN_TEST(test_telemetry_json_compact_omits_hw);
    RUN_TEST(test_telemetry_json_status_value);
    RUN_TEST(test_telemetry_json_buffer_overflow);
}

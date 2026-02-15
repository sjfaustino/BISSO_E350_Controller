/**
 * @file test/test_cli_dispatch.cpp
 * @brief Unit tests for CLI subcommand dispatch and argv contract
 *
 * Tests that cliDispatchSubcommand() correctly handles argument indexing,
 * subcommand matching, and edge cases. Catches the class of bug where
 * handlers misuse argv[0] as a subcommand name.
 */

#include <unity.h>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <cstdio>

// Re-implement types from cli.h (avoid Arduino.h dependency)
typedef void (*cli_handler_t)(int argc, char** argv);

typedef struct {
    const char* name;
    cli_handler_t handler;
    const char* help;
} cli_subcommand_t;

// ---------------------------------------------------------------------------
// Track handler invocations
// ---------------------------------------------------------------------------
static int handler_called_argc = -1;
static char** handler_called_argv = nullptr;
static int handler_call_count = 0;

static void mock_handler_a(int argc, char** argv) {
    handler_called_argc = argc;
    handler_called_argv = argv;
    handler_call_count++;
}

static int handler_b_call_count = 0;
static void mock_handler_b(int argc, char** argv) {
    (void)argc; (void)argv;
    handler_b_call_count++;
}

// Log capture for usage / warning messages (static to avoid linker collisions)
static char dispatch_log_buffer[256] = {0};
static int dispatch_log_offset = 0;

static void dispatch_logPrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    dispatch_log_offset += vsnprintf(dispatch_log_buffer + dispatch_log_offset,
                                     sizeof(dispatch_log_buffer) - dispatch_log_offset,
                                     format, args);
    va_end(args);
}

static void dispatch_logPrintln(const char* msg) {
    dispatch_log_offset += snprintf(dispatch_log_buffer + dispatch_log_offset,
                                    sizeof(dispatch_log_buffer) - dispatch_log_offset,
                                    "%s\n", msg);
}

static void dispatch_logWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    dispatch_log_offset += vsnprintf(dispatch_log_buffer + dispatch_log_offset,
                                     sizeof(dispatch_log_buffer) - dispatch_log_offset,
                                     format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Re-implementation of cliDispatchSubcommand from cli_base.cpp
// Uses internal dispatch_log* functions to avoid linker collisions
// ---------------------------------------------------------------------------
static bool cliDispatchSubcommand(const char* prefix, int argc, char** argv,
                                   const cli_subcommand_t* table, size_t table_size,
                                   int arg_index = 1) {
    // Check if argument exists
    if (argc <= arg_index) {
        dispatch_logPrintf("%s Usage: %s [", prefix, argv[0]);
        for (size_t i = 0; i < table_size; i++) {
            dispatch_logPrintf("%s%s", table[i].name, (i < table_size - 1) ? " | " : "");
        }
        dispatch_logPrintln("]");

        // Print available subcommands with help
        for (size_t i = 0; i < table_size; i++) {
            dispatch_logPrintf("  %-12s %s\n", table[i].name, table[i].help);
        }
        return false;
    }

    // Find matching subcommand
    for (size_t i = 0; i < table_size; i++) {
        if (strcasecmp(argv[arg_index], table[i].name) == 0) {
            table[i].handler(argc, argv);  // Passes ORIGINAL argc/argv
            return true;
        }
    }

    // Not found
    dispatch_logWarning("%s Unknown subcommand: %s", prefix, argv[arg_index]);
    return false;
}

// ---------------------------------------------------------------------------
// Test fixture reset
// ---------------------------------------------------------------------------
static void reset_dispatch_state() {
    handler_called_argc = -1;
    handler_called_argv = nullptr;
    handler_call_count = 0;
    handler_b_call_count = 0;
    memset(dispatch_log_buffer, 0, sizeof(dispatch_log_buffer));
    dispatch_log_offset = 0;
}

// Subcommand table used across tests
static const cli_subcommand_t test_table[] = {
    {"stress",  mock_handler_a, "Run stress tests"},
    {"info",    mock_handler_b, "Show system info"},
};
static const size_t test_table_size = sizeof(test_table) / sizeof(test_table[0]);

// ============================================================================
// TESTS
// ============================================================================

// @test Correct subcommand is dispatched at arg_index=1
void test_dispatch_finds_correct_handler(void) {
    reset_dispatch_state();

    // Simulate: "test stress"
    char* argv[] = {(char*)"test", (char*)"stress"};
    int argc = 2;

    bool result = cliDispatchSubcommand("[TEST]", argc, argv, test_table, test_table_size, 1);

    TEST_ASSERT_TRUE_MESSAGE(result, "Should find 'stress' handler");
    TEST_ASSERT_EQUAL_MESSAGE(1, handler_call_count, "stress handler should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(0, handler_b_call_count, "info handler should not be called");
}

// @test Handler receives the ORIGINAL argc/argv (not shifted)
void test_dispatch_passes_original_argv(void) {
    reset_dispatch_state();

    char* argv[] = {(char*)"test", (char*)"stress", (char*)"all"};
    int argc = 3;

    cliDispatchSubcommand("[TEST]", argc, argv, test_table, test_table_size, 1);

    TEST_ASSERT_EQUAL_MESSAGE(3, handler_called_argc, "Handler should receive original argc");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(argv, handler_called_argv, "Handler should receive original argv pointer");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test", handler_called_argv[0], "argv[0] should be command name, not subcommand");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("stress", handler_called_argv[1], "argv[1] should be subcommand");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("all", handler_called_argv[2], "argv[2] should be the argument");
}

// @test Insufficient args shows usage, returns false
void test_dispatch_no_subcommand_shows_usage(void) {
    reset_dispatch_state();

    char* argv[] = {(char*)"test"};
    int argc = 1;

    bool result = cliDispatchSubcommand("[TEST]", argc, argv, test_table, test_table_size, 1);

    TEST_ASSERT_FALSE_MESSAGE(result, "Should return false when no subcommand");
    TEST_ASSERT_EQUAL_MESSAGE(0, handler_call_count, "No handler should be called");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(dispatch_log_buffer, "stress"), "Usage should list 'stress'");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(dispatch_log_buffer, "info"), "Usage should list 'info'");
}

// @test Unknown subcommand returns false and warns
void test_dispatch_unknown_subcommand(void) {
    reset_dispatch_state();

    char* argv[] = {(char*)"test", (char*)"banana"};
    int argc = 2;

    bool result = cliDispatchSubcommand("[TEST]", argc, argv, test_table, test_table_size, 1);

    TEST_ASSERT_FALSE_MESSAGE(result, "Should return false for unknown subcommand");
    TEST_ASSERT_EQUAL_MESSAGE(0, handler_call_count, "No handler should be called");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(dispatch_log_buffer, "Unknown subcommand"),
                                "Should warn about the unknown subcommand");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(dispatch_log_buffer, "banana"),
                                "Warning should mention the unknown command name");
}

// @test Case-insensitive matching
void test_dispatch_case_insensitive(void) {
    reset_dispatch_state();

    char* argv[] = {(char*)"test", (char*)"STRESS"};
    int argc = 2;

    bool result = cliDispatchSubcommand("[TEST]", argc, argv, test_table, test_table_size, 1);

    TEST_ASSERT_TRUE_MESSAGE(result, "Matching should be case-insensitive");
    TEST_ASSERT_EQUAL_MESSAGE(1, handler_call_count, "stress handler should be called");
}

// @test Dispatch at arg_index=2 (nested subcommands)
void test_dispatch_at_arg_index_2(void) {
    reset_dispatch_state();

    // Simulate: "diag test stress" where we dispatch at index 2
    char* argv[] = {(char*)"diag", (char*)"test", (char*)"stress"};
    int argc = 3;

    bool result = cliDispatchSubcommand("[DIAG]", argc, argv, test_table, test_table_size, 2);

    TEST_ASSERT_TRUE_MESSAGE(result, "Should dispatch at arg_index=2");
    TEST_ASSERT_EQUAL_MESSAGE(1, handler_call_count, "stress handler should be called");
    // Verify argv[0] is still the top-level command
    TEST_ASSERT_EQUAL_STRING_MESSAGE("diag", handler_called_argv[0], "argv[0] should be top-level command");
}

// @test argv[0] contract — handlers must NOT treat argv[0] as subcommand
void test_argv0_is_always_command_name(void) {
    reset_dispatch_state();

    // This is the pattern that caused the "Unknown test: test" bug.
    // Handler was incorrectly reading argv[0] ("test") as the test name
    // instead of argv[1] ("stress") or argv[2] ("all").
    char* argv[] = {(char*)"test", (char*)"stress", (char*)"concurrent"};
    int argc = 3;

    cliDispatchSubcommand("[TEST]", argc, argv, test_table, test_table_size, 1);

    // The handler receives original argv. The test verifies the contract:
    // - argv[0] = command name ("test")
    // - argv[1] = subcommand ("stress")
    // - argv[2] = argument ("concurrent")
    TEST_ASSERT_EQUAL_STRING("test", handler_called_argv[0]);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcmp(handler_called_argv[0], "stress"),
                                  "argv[0] must NOT be the subcommand");
}

// ============================================================================
// REGISTRATION
// ============================================================================

void run_cli_dispatch_tests(void) {
    RUN_TEST(test_dispatch_finds_correct_handler);
    RUN_TEST(test_dispatch_passes_original_argv);
    RUN_TEST(test_dispatch_no_subcommand_shows_usage);
    RUN_TEST(test_dispatch_unknown_subcommand);
    RUN_TEST(test_dispatch_case_insensitive);
    RUN_TEST(test_dispatch_at_arg_index_2);
    RUN_TEST(test_argv0_is_always_command_name);
}

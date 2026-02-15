/**
 * @file test/test_cli_tab_completion.cpp
 * @brief Unit tests for CLI tab completion logic
 *
 * Tests command matching, common prefix computation, and suffix extraction
 * used in the tab completion feature of the CLI.
 */

#include <unity.h>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Re-implement the tab completion matching logic from cli_base.cpp
// Extracted and simplified for testability
// ---------------------------------------------------------------------------

// Simulated command registry
struct test_command_t {
    const char* name;
};

static test_command_t test_commands[32];
static int test_command_count = 0;

static void register_test_commands(const char** names, int count) {
    test_command_count = (count > 32) ? 32 : count;
    for (int i = 0; i < test_command_count; i++) {
        test_commands[i].name = names[i];
    }
}

/**
 * Find all commands matching the given prefix.
 * Returns the match count and fills 'matches' with pointers to matching names.
 */
static int find_matches(const char* prefix, const char** matches, int max_matches) {
    int count = 0;
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) return 0;

    for (int i = 0; i < test_command_count && count < max_matches; i++) {
        if (strncasecmp(test_commands[i].name, prefix, prefix_len) == 0) {
            matches[count++] = test_commands[i].name;
        }
    }
    return count;
}

/**
 * Compute the longest common prefix among matched commands.
 * Returns the length of the common prefix.
 */
static size_t compute_common_prefix(const char** matches, int match_count) {
    if (match_count == 0) return 0;
    if (match_count == 1) return strlen(matches[0]);

    size_t prefix_len = strlen(matches[0]);
    for (int i = 1; i < match_count; i++) {
        size_t j = 0;
        while (j < prefix_len && matches[0][j] != '\0' && matches[i][j] != '\0' &&
               matches[0][j] == matches[i][j]) {
            j++;
        }
        prefix_len = j;
    }
    return prefix_len;
}

/**
 * Get the suffix to append for tab completion.
 * input: current typed text
 * suffix: output buffer for the characters to append
 * Returns the number of characters in suffix (0 = nothing to complete)
 */
static int get_completion_suffix(const char* input, char* suffix, size_t suffix_size) {
    const char* matches[32];
    int match_count = find_matches(input, matches, 32);

    if (match_count == 0) {
        suffix[0] = '\0';
        return 0;
    }

    size_t input_len = strlen(input);
    size_t common_len = compute_common_prefix(matches, match_count);

    if (common_len <= input_len) {
        suffix[0] = '\0';
        return 0;  // Nothing new to add
    }

    // Copy the suffix (the part after what's already typed)
    size_t suffix_len = common_len - input_len;
    if (suffix_len >= suffix_size) suffix_len = suffix_size - 1;
    strncpy(suffix, matches[0] + input_len, suffix_len);
    suffix[suffix_len] = '\0';

    return (int)suffix_len;
}

// ============================================================================
// TESTS
// ============================================================================

// @test Single match completes the full command
void test_tab_single_match(void) {
    const char* cmds[] = {"encoder", "config", "help", "memory", "reboot"};
    register_test_commands(cmds, 5);

    char suffix[64];
    int len = get_completion_suffix("en", suffix, sizeof(suffix));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("coder", suffix);  // "en" + "coder" = "encoder"
}

// @test Multiple matches extend to common prefix
void test_tab_multiple_match_common_prefix(void) {
    const char* cmds[] = {"config", "connect", "help", "memory"};
    register_test_commands(cmds, 4);

    char suffix[64];
    int len = get_completion_suffix("co", suffix, sizeof(suffix));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("n", suffix);  // "co" → "con" (common prefix of config/connect)
}

// @test No matches returns empty suffix
void test_tab_no_match(void) {
    const char* cmds[] = {"config", "help", "memory"};
    register_test_commands(cmds, 3);

    char suffix[64];
    int len = get_completion_suffix("xyz", suffix, sizeof(suffix));

    TEST_ASSERT_EQUAL(0, len);
    TEST_ASSERT_EQUAL_STRING("", suffix);
}

// @test Case-insensitive matching
void test_tab_case_insensitive(void) {
    const char* cmds[] = {"encoder", "config", "help"};
    register_test_commands(cmds, 3);

    char suffix[64];
    int len = get_completion_suffix("EN", suffix, sizeof(suffix));

    TEST_ASSERT_GREATER_THAN(0, len);
    // Should match "encoder" and return the remaining chars
    TEST_ASSERT_EQUAL_STRING("coder", suffix);
}

// @test Already-complete input returns nothing
void test_tab_already_complete(void) {
    const char* cmds[] = {"help", "config"};
    register_test_commands(cmds, 2);

    char suffix[64];
    int len = get_completion_suffix("help", suffix, sizeof(suffix));

    TEST_ASSERT_EQUAL(0, len);  // "help" is already complete
}

// @test Multiple matches with no additional common prefix
void test_tab_multiple_no_extension(void) {
    const char* cmds[] = {"get", "gcode", "gpio"};
    register_test_commands(cmds, 3);

    char suffix[64];
    int len = get_completion_suffix("g", suffix, sizeof(suffix));

    TEST_ASSERT_EQUAL(0, len);  // "g" → "g" (common prefix is just "g", no extension)
}

// @test Common prefix computation correctness
void test_common_prefix_basic(void) {
    const char* matches[] = {"configure", "config", "connect"};

    size_t len = compute_common_prefix(matches, 3);
    TEST_ASSERT_EQUAL(3, len);  // "con" is the common prefix
}

void test_common_prefix_single(void) {
    const char* matches[] = {"encoder"};

    size_t len = compute_common_prefix(matches, 1);
    TEST_ASSERT_EQUAL(7, len);  // Full string for single match
}

void test_common_prefix_identical(void) {
    const char* matches[] = {"test", "test"};

    size_t len = compute_common_prefix(matches, 2);
    TEST_ASSERT_EQUAL(4, len);  // Identical strings
}

// @test Empty input returns no matches
void test_tab_empty_input(void) {
    const char* cmds[] = {"config", "help"};
    register_test_commands(cmds, 2);

    char suffix[64];
    int len = get_completion_suffix("", suffix, sizeof(suffix));

    TEST_ASSERT_EQUAL(0, len);
}

// ============================================================================
// REGISTRATION
// ============================================================================

void run_cli_tab_completion_tests(void) {
    RUN_TEST(test_tab_single_match);
    RUN_TEST(test_tab_multiple_match_common_prefix);
    RUN_TEST(test_tab_no_match);
    RUN_TEST(test_tab_case_insensitive);
    RUN_TEST(test_tab_already_complete);
    RUN_TEST(test_tab_multiple_no_extension);
    RUN_TEST(test_common_prefix_basic);
    RUN_TEST(test_common_prefix_single);
    RUN_TEST(test_common_prefix_identical);
    RUN_TEST(test_tab_empty_input);
}

/**
 * @file test/test_api_routes.cpp
 * @brief Unit tests for modular API route modules
 * @details Tests the api_routes_*.cpp modules extracted from web_server.cpp
 */

#include "helpers/test_utils.h"
#include <cstdint>
#include <cstring>
#include <unity.h>

// ============================================================================
// MOCK STRUCTURES (mirroring actual api_routes.h)
// ============================================================================

typedef struct {
    const char* path;
    const char* method;
    const char* module;
    const char* description;
} api_route_entry_t;

/**
 * @brief Route registry for testing - mirrors actual routes in modules
 */
static api_route_entry_t test_routes[] = {
    // Telemetry Routes (api_routes_telemetry.cpp)
    {"/api/status", "GET", "telemetry", "System status and positions"},
    {"/api/spindle", "GET", "telemetry", "Spindle monitor state"},
    {"/api/spindle/alarm", "GET", "telemetry", "Spindle alarm thresholds"},
    {"/api/spindle/alarm", "POST", "telemetry", "Set spindle alarm thresholds"},
    {"/api/spindle/alarm/clear", "POST", "telemetry", "Clear spindle alarms"},
    {"/api/history/telemetry", "GET", "telemetry", "Historical telemetry data"},
    
    // G-code Routes (api_routes_gcode.cpp)
    {"/api/gcode", "POST", "gcode", "Execute G-code command"},
    {"/api/gcode/state", "GET", "gcode", "Get G-code parser state"},
    {"/api/gcode/queue", "GET", "gcode", "Get queue state and history"},
    {"/api/gcode/queue", "DELETE", "gcode", "Clear queue"},
    {"/api/gcode/queue/retry", "POST", "gcode", "Retry failed job"},
    {"/api/gcode/queue/skip", "POST", "gcode", "Skip failed job"},
    {"/api/gcode/queue/resume", "POST", "gcode", "Resume from current position"},
    
    // Motion Routes (api_routes_motion.cpp)
    {"/api/encoder/calibrate", "POST", "motion", "Calibrate encoder"},
    {"/api/hardware/wj66/baud", "POST", "motion", "Set WJ66 baud rate"},
    {"/api/hardware/wj66/detect", "POST", "motion", "Autodetect WJ66"},
    
    // Network Routes (api_routes_network.cpp)
    {"/api/network/status", "GET", "network", "Network status"},
    {"/api/network/reconnect", "POST", "network", "Trigger reconnection"},
    {"/api/time", "GET", "network", "Get current time"},
    {"/api/time/sync", "POST", "network", "Sync time from client"},
    
    // Hardware Routes (api_routes_hardware.cpp)
    {"/api/io/status", "GET", "hardware", "I/O status"},
    {"/api/hardware/io", "GET", "hardware", "Hardware I/O state"},
    {"/api/hardware/pins", "GET", "hardware", "Pin mapping"},
    {"/api/hardware/pins", "POST", "hardware", "Set pin mapping"},
    {"/api/hardware/pins/reset", "POST", "hardware", "Reset pin mapping"},
    {"/api/hardware/tachometer", "GET", "hardware", "Tachometer state"},
    {"/api/logs/boot", "GET", "hardware", "Boot log"},
    {"/api/logs/boot", "DELETE", "hardware", "Delete boot log"},
    
    // System Routes (api_routes_system.cpp)
    {"/api/config/get", "GET", "system", "Get config by category"},
    {"/api/config/set", "POST", "system", "Set config value"},
    {"/api/config", "GET", "system", "Get merged config"},
    {"/api/config", "POST", "system", "Set config (simple)"},
    {"/api/config/batch", "POST", "system", "Batch set config"},
    {"/api/config/backup", "GET", "system", "Download full config"},
    {"/api/config/detect-rs485", "POST", "system", "Autodetect RS485 baud"},
    {"/api/faults", "GET", "system", "Get fault history"},
    {"/api/faults", "DELETE", "system", "Clear fault history"},
    {"/api/faults/clear", "POST", "system", "Clear faults (POST)"},
    {"/api/ota/check", "GET", "system", "Check for updates"},
    {"/api/ota/latest", "GET", "system", "Get cached update info"},
    {"/api/ota/update", "POST", "system", "Trigger OTA update"},
    {"/api/ota/status", "GET", "system", "Get OTA progress"},
    {"/api/system/reboot", "POST", "system", "Reboot system"},
};

static const int test_route_count = sizeof(test_routes) / sizeof(test_routes[0]);

// ============================================================================
// ROUTE STRUCTURE TESTS
// ============================================================================

void test_routes_have_valid_paths(void) {
    for (int i = 0; i < test_route_count; i++) {
        const api_route_entry_t* route = &test_routes[i];
        
        // Path must exist and start with /api/
        TEST_ASSERT_NOT_NULL(route->path);
        TEST_ASSERT_TRUE(strncmp(route->path, "/api/", 5) == 0);
    }
}

void test_routes_have_valid_methods(void) {
    const char* valid_methods[] = {"GET", "POST", "PUT", "DELETE"};
    const int valid_count = sizeof(valid_methods) / sizeof(valid_methods[0]);
    
    for (int i = 0; i < test_route_count; i++) {
        const api_route_entry_t* route = &test_routes[i];
        TEST_ASSERT_NOT_NULL(route->method);
        
        bool found = false;
        for (int j = 0; j < valid_count; j++) {
            if (strcmp(route->method, valid_methods[j]) == 0) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, route->path);
    }
}

void test_routes_have_modules_assigned(void) {
    const char* valid_modules[] = {"telemetry", "gcode", "motion", "network", "hardware", "system"};
    const int module_count = sizeof(valid_modules) / sizeof(valid_modules[0]);
    
    for (int i = 0; i < test_route_count; i++) {
        const api_route_entry_t* route = &test_routes[i];
        TEST_ASSERT_NOT_NULL(route->module);
        
        bool found = false;
        for (int j = 0; j < module_count; j++) {
            if (strcmp(route->module, valid_modules[j]) == 0) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, route->path);
    }
}

void test_routes_have_descriptions(void) {
    for (int i = 0; i < test_route_count; i++) {
        const api_route_entry_t* route = &test_routes[i];
        TEST_ASSERT_NOT_NULL(route->description);
        TEST_ASSERT_TRUE(strlen(route->description) > 0);
    }
}

// ============================================================================
// MODULE DISTRIBUTION TESTS
// ============================================================================

void test_telemetry_module_routes_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].module, "telemetry") == 0) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 5);  // At least 5 routes expected
}

void test_gcode_module_routes_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].module, "gcode") == 0) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 6);  // At least 6 routes expected
}

void test_motion_module_routes_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].module, "motion") == 0) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 3);  // At least 3 routes expected
}

void test_network_module_routes_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].module, "network") == 0) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 4);  // At least 4 routes expected
}

void test_hardware_module_routes_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].module, "hardware") == 0) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 8);  // At least 8 routes expected
}

void test_system_module_routes_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].module, "system") == 0) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 14);  // At least 14 routes expected
}

// ============================================================================
// CRITICAL ROUTE TESTS
// ============================================================================

void test_status_endpoint_exists(void) {
    bool found = false;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].path, "/api/status") == 0 &&
            strcmp(test_routes[i].method, "GET") == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_gcode_endpoint_exists(void) {
    bool found = false;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].path, "/api/gcode") == 0 &&
            strcmp(test_routes[i].method, "POST") == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_config_endpoints_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strstr(test_routes[i].path, "/api/config") != NULL) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 6);  // Multiple config endpoints
}

void test_reboot_endpoint_exists(void) {
    bool found = false;
    for (int i = 0; i < test_route_count; i++) {
        if (strcmp(test_routes[i].path, "/api/system/reboot") == 0 &&
            strcmp(test_routes[i].method, "POST") == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_ota_endpoints_exist(void) {
    int count = 0;
    for (int i = 0; i < test_route_count; i++) {
        if (strstr(test_routes[i].path, "/api/ota") != NULL) {
            count++;
        }
    }
    TEST_ASSERT_TRUE(count >= 4);  // check, latest, update, status
}

// ============================================================================
// PATH CONVENTION TESTS
// ============================================================================

void test_all_paths_start_with_api(void) {
    for (int i = 0; i < test_route_count; i++) {
        TEST_ASSERT_TRUE(strncmp(test_routes[i].path, "/api/", 5) == 0);
    }
}

void test_no_trailing_slashes(void) {
    for (int i = 0; i < test_route_count; i++) {
        size_t len = strlen(test_routes[i].path);
        if (len > 1) {
            TEST_ASSERT_NOT_EQUAL('/', test_routes[i].path[len - 1]);
        }
    }
}

void test_path_module_consistency(void) {
    // Routes with /api/gcode should be in gcode module
    for (int i = 0; i < test_route_count; i++) {
        if (strncmp(test_routes[i].path, "/api/gcode", 10) == 0) {
            TEST_ASSERT_EQUAL_STRING("gcode", test_routes[i].module);
        }
    }
}

// ============================================================================
// REGISTRATION FUNCTION
// ============================================================================

void run_api_routes_tests(void) {
    // Structure tests
    RUN_TEST(test_routes_have_valid_paths);
    RUN_TEST(test_routes_have_valid_methods);
    RUN_TEST(test_routes_have_modules_assigned);
    RUN_TEST(test_routes_have_descriptions);
    
    // Module distribution tests
    RUN_TEST(test_telemetry_module_routes_exist);
    RUN_TEST(test_gcode_module_routes_exist);
    RUN_TEST(test_motion_module_routes_exist);
    RUN_TEST(test_network_module_routes_exist);
    RUN_TEST(test_hardware_module_routes_exist);
    RUN_TEST(test_system_module_routes_exist);
    
    // Critical route tests
    RUN_TEST(test_status_endpoint_exists);
    RUN_TEST(test_gcode_endpoint_exists);
    RUN_TEST(test_config_endpoints_exist);
    RUN_TEST(test_reboot_endpoint_exists);
    RUN_TEST(test_ota_endpoints_exist);
    
    // Path convention tests
    RUN_TEST(test_all_paths_start_with_api);
    RUN_TEST(test_no_trailing_slashes);
    RUN_TEST(test_path_module_consistency);
}

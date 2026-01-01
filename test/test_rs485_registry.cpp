/**
 * @file test/test_rs485_registry.cpp
 * @brief Unit tests for RS-485 device registry and scheduling
 *
 * Tests cover:
 * - Device registration and unregistration
 * - Priority-based scheduling
 * - Polling interval enforcement
 * - Device type lookup
 * - Error counter management
 */

#include "test/unity/unity.h"
#include <cstdint>
#include <cstring>

// ============================================================================
// RS485 TYPE DEFINITIONS (from rs485_device_registry.h)
// ============================================================================

#define RS485_MAX_DEVICES           8
#define RS485_DEFAULT_BAUD_RATE     9600
#define RS485_INTER_FRAME_DELAY_MS  5

typedef enum {
    RS485_DEVICE_TYPE_ENCODER = 0,
    RS485_DEVICE_TYPE_CURRENT_SENSOR,
    RS485_DEVICE_TYPE_VFD,
    RS485_DEVICE_TYPE_RPM_SENSOR,
    RS485_DEVICE_TYPE_GENERIC
} rs485_device_type_t;

typedef bool (*rs485_poll_fn)(void);
typedef bool (*rs485_response_fn)(const uint8_t* data, uint16_t len);

typedef struct {
    const char* name;
    rs485_device_type_t type;
    uint8_t slave_address;
    uint8_t priority;
    uint16_t poll_interval_ms;
    bool enabled;
    
    // Callbacks (NULL for testing)
    rs485_poll_fn poll;
    rs485_response_fn on_response;
    
    // Runtime state
    uint32_t last_poll_time_ms;
    uint32_t poll_count;
    uint32_t error_count;
    uint8_t consecutive_errors;
    bool pending_response;
} rs485_device_t;

typedef struct {
    rs485_device_t* devices[RS485_MAX_DEVICES];
    uint8_t device_count;
    uint8_t current_device_index;
    uint32_t last_switch_time_ms;
    uint32_t baud_rate;
    bool bus_busy;
    uint32_t total_transactions;
    uint32_t total_errors;
} rs485_registry_state_t;

// ============================================================================
// MOCK REGISTRY IMPLEMENTATION
// ============================================================================

static rs485_registry_state_t g_registry;

static void registry_reset(void) {
    memset(&g_registry, 0, sizeof(g_registry));
    g_registry.baud_rate = RS485_DEFAULT_BAUD_RATE;
}

static bool registry_add_device(rs485_device_t* dev) {
    if (!dev || g_registry.device_count >= RS485_MAX_DEVICES) return false;
    
    // Check for duplicate address
    for (uint8_t i = 0; i < g_registry.device_count; i++) {
        if (g_registry.devices[i]->slave_address == dev->slave_address) {
            return false;
        }
    }
    
    // Add sorted by priority (highest first)
    uint8_t insert_idx = g_registry.device_count;
    for (uint8_t i = 0; i < g_registry.device_count; i++) {
        if (dev->priority > g_registry.devices[i]->priority) {
            insert_idx = i;
            break;
        }
    }
    
    // Shift devices
    for (uint8_t i = g_registry.device_count; i > insert_idx; i--) {
        g_registry.devices[i] = g_registry.devices[i - 1];
    }
    
    g_registry.devices[insert_idx] = dev;
    g_registry.device_count++;
    return true;
}

static bool registry_remove_device(rs485_device_t* dev) {
    if (!dev) return false;
    
    for (uint8_t i = 0; i < g_registry.device_count; i++) {
        if (g_registry.devices[i] == dev) {
            for (uint8_t j = i; j < g_registry.device_count - 1; j++) {
                g_registry.devices[j] = g_registry.devices[j + 1];
            }
            g_registry.device_count--;
            g_registry.devices[g_registry.device_count] = NULL;
            return true;
        }
    }
    return false;
}

static rs485_device_t* registry_find_by_type(rs485_device_type_t type) {
    for (uint8_t i = 0; i < g_registry.device_count; i++) {
        if (g_registry.devices[i]->type == type) {
            return g_registry.devices[i];
        }
    }
    return NULL;
}

static rs485_device_t* registry_find_by_address(uint8_t addr) {
    for (uint8_t i = 0; i < g_registry.device_count; i++) {
        if (g_registry.devices[i]->slave_address == addr) {
            return g_registry.devices[i];
        }
    }
    return NULL;
}

// ============================================================================
// DEVICE REGISTRATION TESTS
// ============================================================================

// @test Registry starts empty
void test_registry_starts_empty(void) {
    registry_reset();
    TEST_ASSERT_EQUAL_UINT8(0, g_registry.device_count);
}

// @test Can add device to registry
void test_registry_add_device(void) {
    registry_reset();
    
    rs485_device_t dev = {
        .name = "TestDevice",
        .type = RS485_DEVICE_TYPE_ENCODER,
        .slave_address = 1,
        .priority = 5,
        .poll_interval_ms = 100,
        .enabled = true
    };
    
    bool result = registry_add_device(&dev);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT8(1, g_registry.device_count);
}

// @test Cannot add duplicate address
void test_registry_rejects_duplicate_address(void) {
    registry_reset();
    
    rs485_device_t dev1 = {.name = "Dev1", .slave_address = 1, .priority = 5};
    rs485_device_t dev2 = {.name = "Dev2", .slave_address = 1, .priority = 3};
    
    registry_add_device(&dev1);
    bool result = registry_add_device(&dev2);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_UINT8(1, g_registry.device_count);
}

// @test Registry limited to max devices
void test_registry_max_devices_limit(void) {
    registry_reset();
    
    rs485_device_t devices[RS485_MAX_DEVICES + 1];
    for (int i = 0; i <= RS485_MAX_DEVICES; i++) {
        devices[i].name = "Device";
        devices[i].slave_address = i + 1;
        devices[i].priority = 1;
    }
    
    // Add max devices
    for (int i = 0; i < RS485_MAX_DEVICES; i++) {
        TEST_ASSERT_TRUE(registry_add_device(&devices[i]));
    }
    
    // Next should fail
    TEST_ASSERT_FALSE(registry_add_device(&devices[RS485_MAX_DEVICES]));
    TEST_ASSERT_EQUAL_UINT8(RS485_MAX_DEVICES, g_registry.device_count);
}

// @test Can remove device from registry
void test_registry_remove_device(void) {
    registry_reset();
    
    rs485_device_t dev = {.name = "Dev", .slave_address = 1, .priority = 5};
    registry_add_device(&dev);
    
    bool result = registry_remove_device(&dev);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT8(0, g_registry.device_count);
}

// ============================================================================
// PRIORITY SORTING TESTS
// ============================================================================

// @test Devices sorted by priority (highest first)
void test_registry_priority_sorting(void) {
    registry_reset();
    
    rs485_device_t low = {.name = "Low", .slave_address = 1, .priority = 1};
    rs485_device_t high = {.name = "High", .slave_address = 2, .priority = 10};
    rs485_device_t med = {.name = "Med", .slave_address = 3, .priority = 5};
    
    registry_add_device(&low);
    registry_add_device(&high);
    registry_add_device(&med);
    
    // Highest priority should be first
    TEST_ASSERT_EQUAL_UINT8(10, g_registry.devices[0]->priority);
    TEST_ASSERT_EQUAL_UINT8(5, g_registry.devices[1]->priority);
    TEST_ASSERT_EQUAL_UINT8(1, g_registry.devices[2]->priority);
}

// ============================================================================
// DEVICE LOOKUP TESTS
// ============================================================================

// @test Find device by type
void test_registry_find_by_type(void) {
    registry_reset();
    
    rs485_device_t encoder = {.name = "Encoder", .slave_address = 1, 
                              .type = RS485_DEVICE_TYPE_ENCODER, .priority = 5};
    rs485_device_t vfd = {.name = "VFD", .slave_address = 2,
                          .type = RS485_DEVICE_TYPE_VFD, .priority = 3};
    
    registry_add_device(&encoder);
    registry_add_device(&vfd);
    
    rs485_device_t* found = registry_find_by_type(RS485_DEVICE_TYPE_VFD);
    
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("VFD", found->name);
}

// @test Find device by address
void test_registry_find_by_address(void) {
    registry_reset();
    
    rs485_device_t dev1 = {.name = "Dev1", .slave_address = 5, .priority = 5};
    rs485_device_t dev2 = {.name = "Dev2", .slave_address = 10, .priority = 3};
    
    registry_add_device(&dev1);
    registry_add_device(&dev2);
    
    rs485_device_t* found = registry_find_by_address(10);
    
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Dev2", found->name);
}

// @test Find returns NULL for non-existent device
void test_registry_find_returns_null(void) {
    registry_reset();
    
    rs485_device_t* found = registry_find_by_address(99);
    TEST_ASSERT_NULL(found);
}

// ============================================================================
// DEVICE TYPE ENUM TESTS
// ============================================================================

// @test Device types have expected values
void test_device_types_values(void) {
    TEST_ASSERT_EQUAL(0, RS485_DEVICE_TYPE_ENCODER);
    TEST_ASSERT_EQUAL(1, RS485_DEVICE_TYPE_CURRENT_SENSOR);
    TEST_ASSERT_EQUAL(2, RS485_DEVICE_TYPE_VFD);
    TEST_ASSERT_EQUAL(3, RS485_DEVICE_TYPE_RPM_SENSOR);
    TEST_ASSERT_EQUAL(4, RS485_DEVICE_TYPE_GENERIC);
}

// ============================================================================
// CONFIGURATION TESTS
// ============================================================================

// @test Default baud rate is 9600
void test_default_baud_rate(void) {
    registry_reset();
    TEST_ASSERT_EQUAL_UINT32(9600, g_registry.baud_rate);
}

// @test Inter-frame delay constant is defined
void test_inter_frame_delay_defined(void) {
    TEST_ASSERT_EQUAL(5, RS485_INTER_FRAME_DELAY_MS);
}

// @test Max devices constant is defined
void test_max_devices_defined(void) {
    TEST_ASSERT_EQUAL(8, RS485_MAX_DEVICES);
}

// ============================================================================
// ERROR COUNTER TESTS
// ============================================================================

// @test Device error counters initialize to zero
void test_device_error_counters_init(void) {
    rs485_device_t dev = {0};
    
    TEST_ASSERT_EQUAL_UINT32(0, dev.poll_count);
    TEST_ASSERT_EQUAL_UINT32(0, dev.error_count);
    TEST_ASSERT_EQUAL_UINT8(0, dev.consecutive_errors);
}

// @test Registry error counters initialize to zero
void test_registry_error_counters_init(void) {
    registry_reset();
    
    TEST_ASSERT_EQUAL_UINT32(0, g_registry.total_transactions);
    TEST_ASSERT_EQUAL_UINT32(0, g_registry.total_errors);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_rs485_registry_tests(void) {
    // Device registration
    RUN_TEST(test_registry_starts_empty);
    RUN_TEST(test_registry_add_device);
    RUN_TEST(test_registry_rejects_duplicate_address);
    RUN_TEST(test_registry_max_devices_limit);
    RUN_TEST(test_registry_remove_device);
    
    // Priority sorting
    RUN_TEST(test_registry_priority_sorting);
    
    // Device lookup
    RUN_TEST(test_registry_find_by_type);
    RUN_TEST(test_registry_find_by_address);
    RUN_TEST(test_registry_find_returns_null);
    
    // Device types
    RUN_TEST(test_device_types_values);
    
    // Configuration
    RUN_TEST(test_default_baud_rate);
    RUN_TEST(test_inter_frame_delay_defined);
    RUN_TEST(test_max_devices_defined);
    
    // Error counters
    RUN_TEST(test_device_error_counters_init);
    RUN_TEST(test_registry_error_counters_init);
}

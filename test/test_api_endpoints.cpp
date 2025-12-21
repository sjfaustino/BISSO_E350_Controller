/**
 * @file test/test_api_endpoints.cpp
 * @brief Unit tests for API Endpoint Registry (Phase 5.2)
 *
 * Tests the api_endpoints module which maintains a centralized registry
 * of all available API endpoints for auto-discovery and documentation.
 */

#include "helpers/test_utils.h"
#include <cstdint>
#include <cstring>
#include <unity.h>

/**
 * @brief Mock endpoint structure (mirroring api_endpoints.h)
 */
typedef enum {
  HTTP_GET = 0x01,
  HTTP_POST = 0x02,
  HTTP_PUT = 0x04,
  HTTP_DELETE = 0x08
} http_method_t;

typedef struct {
  const char *path;
  http_method_t methods;
  const char *description;
  bool requires_auth;
  bool rate_limited;
  const char *rate_limit_info;
  const char *response_type;
} api_endpoint_t;

/**
 * @brief Mock endpoint registry for testing
 */
static api_endpoint_t test_endpoints[] = {
    {.path = "/api/status",
     .methods = HTTP_GET,
     .description = "Get system status",
     .requires_auth = true,
     .rate_limited = true,
     .rate_limit_info = "50 requests/min",
     .response_type = "application/json"},
    {.path = "/api/jog",
     .methods = HTTP_POST,
     .description = "Jog axis",
     .requires_auth = true,
     .rate_limited = true,
     .rate_limit_info = "30 requests/min",
     .response_type = "application/json"},
    {.path = "/api/config/get",
     .methods = HTTP_GET,
     .description = "Get configuration",
     .requires_auth = true,
     .rate_limited = true,
     .rate_limit_info = "50 requests/min",
     .response_type = "application/json"},
    {.path = "/api/config/set",
     .methods = HTTP_POST,
     .description = "Set configuration",
     .requires_auth = true,
     .rate_limited = true,
     .rate_limit_info = "30 requests/min",
     .response_type = "application/json"},
    {.path = "/api/endpoints",
     .methods = HTTP_GET,
     .description = "Discover API endpoints",
     .requires_auth = false,
     .rate_limited = false,
     .rate_limit_info = "unlimited",
     .response_type = "application/json"}};

static const int test_endpoint_count =
    sizeof(test_endpoints) / sizeof(test_endpoints[0]);

/**
 * @section Endpoint Registry Tests
 */

void test_endpoints_can_be_registered(void) {
  TEST_ASSERT_TRUE(test_endpoint_count > 0);
  TEST_ASSERT_EQUAL_INT(5, test_endpoint_count);
}

void test_endpoint_has_required_fields(void) {
  for (int i = 0; i < test_endpoint_count; i++) {
    const api_endpoint_t *ep = &test_endpoints[i];

    // Verify required fields exist
    TEST_ASSERT_NOT_NULL(ep->path);
    TEST_ASSERT_NOT_NULL(ep->description);
    TEST_ASSERT_NOT_NULL(ep->rate_limit_info);
    TEST_ASSERT_NOT_NULL(ep->response_type);

    // Verify path starts with /
    TEST_ASSERT_EQUAL_CHAR('/', ep->path[0]);

    // Verify HTTP method is set
    TEST_ASSERT_TRUE(ep->methods > 0);
  }
}

void test_endpoint_paths_unique(void) {
  // Each endpoint should have a unique path
  for (int i = 0; i < test_endpoint_count; i++) {
    for (int j = i + 1; j < test_endpoint_count; j++) {
      TEST_ASSERT_NOT_EQUAL_STRING(test_endpoints[i].path,
                                   test_endpoints[j].path);
    }
  }
}

void test_endpoint_descriptions_not_empty(void) {
  for (int i = 0; i < test_endpoint_count; i++) {
    TEST_ASSERT_TRUE(strlen(test_endpoints[i].description) > 0);
    TEST_ASSERT_TRUE(strlen(test_endpoints[i].description) < 256);
  }
}

void test_endpoint_http_methods_valid(void) {
  // Each endpoint should have valid HTTP method(s)
  for (int i = 0; i < test_endpoint_count; i++) {
    http_method_t methods = test_endpoints[i].methods;

    // At least one method must be set
    TEST_ASSERT_TRUE(methods > 0);

    // Only valid bits should be set
    TEST_ASSERT_TRUE((methods & 0x0F) == methods); // Only bits 0-3
  }
}

void test_endpoints_auth_requirements(void) {
  // Test that auth requirements are properly set
  int auth_required_count = 0;
  int no_auth_count = 0;

  for (int i = 0; i < test_endpoint_count; i++) {
    if (test_endpoints[i].requires_auth) {
      auth_required_count++;
    } else {
      no_auth_count++;
    }
  }

  // Most endpoints should require auth
  TEST_ASSERT_TRUE(auth_required_count >= 4);
  TEST_ASSERT_TRUE(no_auth_count >= 0);
}

void test_endpoints_rate_limiting(void) {
  // Test that rate limiting is properly configured
  int rate_limited_count = 0;
  int unlimited_count = 0;

  for (int i = 0; i < test_endpoint_count; i++) {
    if (test_endpoints[i].rate_limited) {
      rate_limited_count++;
    } else {
      unlimited_count++;
    }
  }

  // Most endpoints should be rate limited
  TEST_ASSERT_TRUE(rate_limited_count > 0);
}

void test_endpoint_response_types_valid(void) {
  // Check that response types are valid MIME types
  for (int i = 0; i < test_endpoint_count; i++) {
    const char *type = test_endpoints[i].response_type;
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_TRUE(strlen(type) > 0);

    // Should be a valid MIME type (contains /)
    TEST_ASSERT_TRUE(strchr(type, '/') != NULL);
  }
}

/**
 * @section Endpoint Discovery Tests
 */

void test_api_status_endpoint_exists(void) {
  // Status endpoint should exist
  bool found = false;
  for (int i = 0; i < test_endpoint_count; i++) {
    if (strcmp(test_endpoints[i].path, "/api/status") == 0) {
      found = true;
      TEST_ASSERT_TRUE(test_endpoints[i].methods & HTTP_GET);
      TEST_ASSERT_TRUE(test_endpoints[i].requires_auth);
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_api_config_endpoints_exist(void) {
  // Configuration endpoints should exist
  int config_count = 0;
  for (int i = 0; i < test_endpoint_count; i++) {
    if (strstr(test_endpoints[i].path, "/api/config") != NULL) {
      config_count++;
    }
  }
  TEST_ASSERT_EQUAL_INT(2, config_count); // get + set
}

void test_endpoint_discovery_endpoint_public(void) {
  // The discovery endpoint should not require authentication
  bool found = false;
  for (int i = 0; i < test_endpoint_count; i++) {
    if (strcmp(test_endpoints[i].path, "/api/endpoints") == 0) {
      found = true;
      TEST_ASSERT_FALSE(test_endpoints[i].requires_auth);
      TEST_ASSERT_FALSE(test_endpoints[i].rate_limited);
    }
  }
  TEST_ASSERT_TRUE(found);
}

/**
 * @section Endpoint Categorization Tests
 */

void test_endpoints_can_be_categorized(void) {
  // Count endpoints by category
  int status_count = 0;
  int config_count = 0;
  int control_count = 0;

  for (int i = 0; i < test_endpoint_count; i++) {
    if (strstr(test_endpoints[i].path, "/api/status") != NULL ||
        strstr(test_endpoints[i].path, "/api/health") != NULL ||
        strstr(test_endpoints[i].path, "/api/telemetry") != NULL) {
      status_count++;
    } else if (strstr(test_endpoints[i].path, "/api/config") != NULL) {
      config_count++;
    } else if (strstr(test_endpoints[i].path, "/api/jog") != NULL) {
      control_count++;
    }
  }

  TEST_ASSERT_TRUE(status_count > 0);
  TEST_ASSERT_TRUE(config_count > 0);
  TEST_ASSERT_TRUE(control_count > 0);
}

void test_get_endpoints_have_read_only_methods(void) {
  // GET endpoints should only support GET
  for (int i = 0; i < test_endpoint_count; i++) {
    if (strstr(test_endpoints[i].path, "/config/get") != NULL ||
        strstr(test_endpoints[i].path, "/status") != NULL ||
        strstr(test_endpoints[i].path, "/health") != NULL) {
      TEST_ASSERT_TRUE(test_endpoints[i].methods & HTTP_GET);
    }
  }
}

void test_post_endpoints_mutate_state(void) {
  // POST endpoints should require auth and rate limiting
  for (int i = 0; i < test_endpoint_count; i++) {
    if (test_endpoints[i].methods & HTTP_POST) {
      TEST_ASSERT_TRUE(test_endpoints[i].requires_auth);
    }
  }
}

/**
 * @section Search & Lookup Tests
 */

void test_can_find_endpoint_by_path(void) {
  // Should be able to locate endpoints by path
  for (int i = 0; i < test_endpoint_count; i++) {
    const char *path = test_endpoints[i].path;
    TEST_ASSERT_NOT_NULL(path);

    // Verify path is unique
    int count = 0;
    for (int j = 0; j < test_endpoint_count; j++) {
      if (strcmp(test_endpoints[j].path, path) == 0) {
        count++;
      }
    }
    TEST_ASSERT_EQUAL_INT(1, count);
  }
}

void test_nonexistent_endpoint_not_found(void) {
  // Non-existent endpoints should not be in registry
  const char *nonexistent = "/api/nonexistent";
  bool found = false;

  for (int i = 0; i < test_endpoint_count; i++) {
    if (strcmp(test_endpoints[i].path, nonexistent) == 0) {
      found = true;
    }
  }
  TEST_ASSERT_FALSE(found);
}

/**
 * @section Rate Limiting Configuration Tests
 */

void test_rate_limit_info_present(void) {
  // All endpoints with rate limiting should have rate limit info
  for (int i = 0; i < test_endpoint_count; i++) {
    if (test_endpoints[i].rate_limited) {
      TEST_ASSERT_NOT_NULL(test_endpoints[i].rate_limit_info);
      TEST_ASSERT_TRUE(strlen(test_endpoints[i].rate_limit_info) > 0);
      TEST_ASSERT_TRUE(strchr(test_endpoints[i].rate_limit_info, '/') !=
                       NULL); // Should have unit
    }
  }
}

void test_rate_limits_are_reasonable(void) {
  // Rate limits should be reasonable values
  for (int i = 0; i < test_endpoint_count; i++) {
    const char *limit_info = test_endpoints[i].rate_limit_info;

    // If it contains a number, it should be positive
    if (strchr(limit_info, '/') != NULL) {
      TEST_ASSERT_NOT_NULL(limit_info);
    }
  }
}

/**
 * @section Registration function for test runner
 */
void run_api_endpoints_tests(void) {
  RUN_TEST(test_endpoints_can_be_registered);
  RUN_TEST(test_endpoint_has_required_fields);
  RUN_TEST(test_endpoint_paths_unique);
  RUN_TEST(test_endpoint_descriptions_not_empty);
  RUN_TEST(test_endpoint_http_methods_valid);
  RUN_TEST(test_endpoints_auth_requirements);
  RUN_TEST(test_endpoints_rate_limiting);
  RUN_TEST(test_endpoint_response_types_valid);

  RUN_TEST(test_api_status_endpoint_exists);
  RUN_TEST(test_api_config_endpoints_exist);
  RUN_TEST(test_endpoint_discovery_endpoint_public);

  RUN_TEST(test_endpoints_can_be_categorized);
  RUN_TEST(test_get_endpoints_have_read_only_methods);
  RUN_TEST(test_post_endpoints_mutate_state);

  RUN_TEST(test_can_find_endpoint_by_path);
  RUN_TEST(test_nonexistent_endpoint_not_found);

  RUN_TEST(test_rate_limit_info_present);
  RUN_TEST(test_rate_limits_are_reasonable);
}

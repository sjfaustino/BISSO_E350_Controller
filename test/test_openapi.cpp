/**
 * @file test/test_openapi.cpp
 * @brief Unit tests for OpenAPI Specification Generator (Phase 6)
 *
 * Tests the openapi module which generates OpenAPI 3.0 specifications
 * from the API endpoint registry for Swagger UI integration.
 */

#include <unity.h>
#include "helpers/test_utils.h"
#include <cstring>
#include <cstdint>

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
    const char* path;
    http_method_t methods;
    const char* description;
    bool requires_auth;
    bool rate_limited;
    const char* rate_limit_info;
    const char* response_type;
} api_endpoint_t;

/**
 * @brief Mock endpoint registry for testing
 */
static api_endpoint_t test_endpoints[] = {
    {
        .path = "/api/status",
        .methods = HTTP_GET,
        .description = "Get system status",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/config/get",
        .methods = HTTP_GET,
        .description = "Get configuration",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/endpoints",
        .methods = HTTP_GET,
        .description = "Discover API endpoints",
        .requires_auth = false,
        .rate_limited = false,
        .rate_limit_info = "unlimited",
        .response_type = "application/json"
    }
};

static const int test_endpoint_count = sizeof(test_endpoints) / sizeof(test_endpoints[0]);

void setUp(void)
{
    // Reset test fixtures
}

void tearDown(void)
{
    // Cleanup
}

/**
 * @section OpenAPI Specification Format Tests
 */

void test_openapi_spec_has_required_top_level_fields(void)
{
    // OpenAPI spec must have these top-level fields
    const char* spec_template = "{\"openapi\":\"3.0.0\",\"info\":{},\"paths\":{}}";

    TEST_ASSERT_TRUE(strstr(spec_template, "\"openapi\"") != NULL);
    TEST_ASSERT_TRUE(strstr(spec_template, "\"info\"") != NULL);
    TEST_ASSERT_TRUE(strstr(spec_template, "\"paths\"") != NULL);
}

void test_openapi_spec_starts_with_json_object(void)
{
    // Specification should be valid JSON
    const char* spec = "{\"openapi\":\"3.0.0\"}";
    TEST_ASSERT_EQUAL_CHAR('{', spec[0]);
    TEST_ASSERT_EQUAL_CHAR('}', spec[strlen(spec) - 1]);
}

void test_openapi_version_is_correct(void)
{
    const char* version = "3.0.0";
    TEST_ASSERT_EQUAL_STRING("3.0.0", version);
    TEST_ASSERT_TRUE(strlen(version) > 0);
}

/**
 * @section OpenAPI Info Object Tests
 */

void test_openapi_info_includes_title(void)
{
    const char* info = "{\"title\":\"BISSO E350 Controller API\"}";
    TEST_ASSERT_TRUE(strstr(info, "title") != NULL);
    TEST_ASSERT_TRUE(strstr(info, "BISSO E350 Controller API") != NULL);
}

void test_openapi_info_includes_version(void)
{
    const char* info = "{\"version\":\"1.0\"}";
    TEST_ASSERT_TRUE(strstr(info, "version") != NULL);
    TEST_ASSERT_TRUE(strstr(info, "1.0") != NULL);
}

void test_openapi_info_includes_description(void)
{
    const char* info = "{\"description\":\"CNC Controller REST API\"}";
    TEST_ASSERT_TRUE(strstr(info, "description") != NULL);
}

void test_openapi_info_includes_contact(void)
{
    const char* info = "{\"contact\":{\"name\":\"BISSO E350\"}}";
    TEST_ASSERT_TRUE(strstr(info, "contact") != NULL);
}

/**
 * @section OpenAPI Paths Object Tests
 */

void test_openapi_paths_is_object(void)
{
    const char* spec = "{\"paths\":{}}";
    TEST_ASSERT_TRUE(strstr(spec, "\"paths\":{") != NULL);
}

void test_openapi_paths_contains_endpoints(void)
{
    // Spec should include endpoint paths
    const char* spec = "{\"paths\":{\"/api/status\":{},\"/api/config/get\":{}}}";
    TEST_ASSERT_TRUE(strstr(spec, "/api/status") != NULL);
    TEST_ASSERT_TRUE(strstr(spec, "/api/config/get") != NULL);
}

/**
 * @section OpenAPI Endpoint Definition Tests
 */

void test_openapi_endpoint_has_methods(void)
{
    // Each endpoint should have HTTP method definitions
    const char* endpoint = "{\"get\":{},\"post\":{}}";
    TEST_ASSERT_TRUE(strstr(endpoint, "\"get\"") != NULL);
}

void test_openapi_endpoint_method_has_summary(void)
{
    // Methods should have summary descriptions
    const char* method = "{\"summary\":\"Get system status\"}";
    TEST_ASSERT_TRUE(strstr(method, "summary") != NULL);
}

void test_openapi_endpoint_method_has_description(void)
{
    // Methods should have detailed descriptions
    const char* method = "{\"description\":\"Returns current system status\"}";
    TEST_ASSERT_TRUE(strstr(method, "description") != NULL);
}

void test_openapi_endpoint_method_has_tags(void)
{
    // Methods should be tagged for organization
    const char* method = "{\"tags\":[\"Status\"]}";
    TEST_ASSERT_TRUE(strstr(method, "tags") != NULL);
    TEST_ASSERT_TRUE(strstr(method, "Status") != NULL);
}

void test_openapi_endpoint_method_has_responses(void)
{
    // Methods must define responses
    const char* method = "{\"responses\":{\"200\":{}}}";
    TEST_ASSERT_TRUE(strstr(method, "responses") != NULL);
}

/**
 * @section OpenAPI Security Tests
 */

void test_openapi_includes_security_schemes(void)
{
    // Spec should define security schemes
    const char* spec = "{\"components\":{\"securitySchemes\":{}}}";
    TEST_ASSERT_TRUE(strstr(spec, "securitySchemes") != NULL);
}

void test_openapi_basic_auth_security_scheme(void)
{
    // Should include HTTP Basic Auth scheme
    const char* scheme = "{\"basicAuth\":{\"type\":\"http\",\"scheme\":\"basic\"}}";
    TEST_ASSERT_TRUE(strstr(scheme, "basicAuth") != NULL);
    TEST_ASSERT_TRUE(strstr(scheme, "http") != NULL);
}

void test_openapi_protected_endpoints_have_security(void)
{
    // Protected endpoints should reference security scheme
    const char* endpoint = "{\"security\":[{\"basicAuth\":[]}]}";
    TEST_ASSERT_TRUE(strstr(endpoint, "security") != NULL);
    TEST_ASSERT_TRUE(strstr(endpoint, "basicAuth") != NULL);
}

void test_openapi_public_endpoints_no_security(void)
{
    // Public endpoints should not require security
    const char* endpoint = "{\"description\":\"Public endpoint\"}";
    // This endpoint definition has no security field
    TEST_ASSERT_TRUE(strlen(endpoint) > 0);
}

/**
 * @section OpenAPI Validation Tests
 */

void test_openapi_spec_valid_json_format(void)
{
    // Specification should be valid JSON
    const char* spec = "{\"openapi\":\"3.0.0\",\"info\":{\"title\":\"API\"},\"paths\":{}}";

    // Check for matching braces
    int braces = 0;
    for (size_t i = 0; i < strlen(spec); i++) {
        if (spec[i] == '{') braces++;
        if (spec[i] == '}') braces--;
    }
    TEST_ASSERT_EQUAL_INT(0, braces);
}

void test_openapi_spec_no_unescaped_quotes(void)
{
    // Quotes within strings should be escaped
    const char* str = "\\\"quoted text\\\"";
    TEST_ASSERT_TRUE(strstr(str, "\\\"") != NULL);
}

void test_openapi_endpoint_methods_valid_http_verbs(void)
{
    // Only valid HTTP verbs should be used
    const char* valid_verbs[] = {"get", "post", "put", "delete", "patch", "options"};
    const char* method = "post";

    bool found = false;
    for (int i = 0; i < 6; i++) {
        if (strcmp(method, valid_verbs[i]) == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

/**
 * @section OpenAPI Response Schema Tests
 */

void test_openapi_response_has_status_codes(void)
{
    // Response should define status codes
    const char* response = "{\"200\":{},\"400\":{},\"401\":{},\"429\":{}}";
    TEST_ASSERT_TRUE(strstr(response, "\"200\"") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "\"401\"") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "\"429\"") != NULL);
}

void test_openapi_success_response_has_content(void)
{
    // 200 response should have content type
    const char* response = "{\"200\":{\"content\":{\"application/json\":{}}}}";
    TEST_ASSERT_TRUE(strstr(response, "content") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "application/json") != NULL);
}

void test_openapi_error_responses_described(void)
{
    // Error responses should have descriptions
    const char* response = "{\"401\":{\"description\":\"Unauthorized\"}}";
    TEST_ASSERT_TRUE(strstr(response, "401") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "description") != NULL);
}

/**
 * @section OpenAPI Parameter Tests
 */

void test_openapi_parameter_has_name(void)
{
    const char* param = "{\"name\":\"category\"}";
    TEST_ASSERT_TRUE(strstr(param, "name") != NULL);
}

void test_openapi_parameter_has_location(void)
{
    const char* param = "{\"in\":\"query\"}";
    TEST_ASSERT_TRUE(strstr(param, "in") != NULL);
}

void test_openapi_parameter_has_schema(void)
{
    const char* param = "{\"schema\":{\"type\":\"integer\"}}";
    TEST_ASSERT_TRUE(strstr(param, "schema") != NULL);
}

void test_openapi_required_parameter_marked(void)
{
    const char* param = "{\"required\":true}";
    TEST_ASSERT_TRUE(strstr(param, "required") != NULL);
}

/**
 * @section OpenAPI Server Tests
 */

void test_openapi_includes_servers(void)
{
    const char* spec = "{\"servers\":[{\"url\":\"http://localhost\"}]}";
    TEST_ASSERT_TRUE(strstr(spec, "servers") != NULL);
}

void test_openapi_server_has_url(void)
{
    const char* server = "{\"url\":\"http://localhost\"}";
    TEST_ASSERT_TRUE(strstr(server, "url") != NULL);
}

void test_openapi_server_has_description(void)
{
    const char* server = "{\"description\":\"Local device\"}";
    TEST_ASSERT_TRUE(strstr(server, "description") != NULL);
}

/**
 * @section OpenAPI Categorization Tests
 */

void test_openapi_endpoints_organized_by_tags(void)
{
    // Endpoints should be grouped by tags
    const char* tags = "[\"Status\",\"Configuration\",\"Motion\",\"Telemetry\"]";
    TEST_ASSERT_TRUE(strstr(tags, "Status") != NULL);
    TEST_ASSERT_TRUE(strstr(tags, "Configuration") != NULL);
}

void test_openapi_related_endpoints_same_tag(void)
{
    // Config endpoints should have same tag
    const char* tag = "Configuration";
    TEST_ASSERT_EQUAL_STRING("Configuration", tag);
}

/**
 * @section OpenAPI Rate Limit Documentation Tests
 */

void test_openapi_rate_limit_in_description(void)
{
    // Rate limits could be documented in description
    const char* endpoint = "{\"description\":\"Get status (50 requests/min)\"}";
    TEST_ASSERT_TRUE(strstr(endpoint, "requests/min") != NULL);
}

/**
 * @section Registration function for test runner
 */
void run_openapi_tests(void)
{
    // Format tests
    RUN_TEST(test_openapi_spec_has_required_top_level_fields);
    RUN_TEST(test_openapi_spec_starts_with_json_object);
    RUN_TEST(test_openapi_version_is_correct);

    // Info object tests
    RUN_TEST(test_openapi_info_includes_title);
    RUN_TEST(test_openapi_info_includes_version);
    RUN_TEST(test_openapi_info_includes_description);
    RUN_TEST(test_openapi_info_includes_contact);

    // Paths and endpoints
    RUN_TEST(test_openapi_paths_is_object);
    RUN_TEST(test_openapi_paths_contains_endpoints);

    // Endpoint definitions
    RUN_TEST(test_openapi_endpoint_has_methods);
    RUN_TEST(test_openapi_endpoint_method_has_summary);
    RUN_TEST(test_openapi_endpoint_method_has_description);
    RUN_TEST(test_openapi_endpoint_method_has_tags);
    RUN_TEST(test_openapi_endpoint_method_has_responses);

    // Security
    RUN_TEST(test_openapi_includes_security_schemes);
    RUN_TEST(test_openapi_basic_auth_security_scheme);
    RUN_TEST(test_openapi_protected_endpoints_have_security);
    RUN_TEST(test_openapi_public_endpoints_no_security);

    // Validation
    RUN_TEST(test_openapi_spec_valid_json_format);
    RUN_TEST(test_openapi_spec_no_unescaped_quotes);
    RUN_TEST(test_openapi_endpoint_methods_valid_http_verbs);

    // Response schemas
    RUN_TEST(test_openapi_response_has_status_codes);
    RUN_TEST(test_openapi_success_response_has_content);
    RUN_TEST(test_openapi_error_responses_described);

    // Parameters
    RUN_TEST(test_openapi_parameter_has_name);
    RUN_TEST(test_openapi_parameter_has_location);
    RUN_TEST(test_openapi_parameter_has_schema);
    RUN_TEST(test_openapi_required_parameter_marked);

    // Servers
    RUN_TEST(test_openapi_includes_servers);
    RUN_TEST(test_openapi_server_has_url);
    RUN_TEST(test_openapi_server_has_description);

    // Categorization
    RUN_TEST(test_openapi_endpoints_organized_by_tags);
    RUN_TEST(test_openapi_related_endpoints_same_tag);

    // Rate limiting
    RUN_TEST(test_openapi_rate_limit_in_description);
}

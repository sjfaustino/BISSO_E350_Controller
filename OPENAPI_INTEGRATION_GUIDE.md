# OpenAPI/Swagger Integration Guide - Phase 6

## Overview

The BISSO E350 Controller now provides **OpenAPI 3.0 compliant specification** and **Swagger UI** for interactive API documentation and testing. This enables seamless integration with professional API tools, client code generators, and documentation systems.

## Quick Start

### Access Swagger UI (Interactive Documentation)

```bash
# Open in browser
http://controller-ip/api/docs
```

The Swagger UI provides:
- ✓ Interactive endpoint browser
- ✓ Live request/response testing
- ✓ Syntax highlighting
- ✓ Authentication handling
- ✓ Rate limit information

### Get OpenAPI Specification (Machine-Readable)

```bash
# Download OpenAPI JSON
curl http://controller-ip/api/openapi.json | jq
```

## Accessing the API

### Three Discovery Methods

#### 1. Swagger UI (Browser-Based)
- **URL**: `http://controller-ip/api/docs`
- **Best for**: Visual exploration, live testing, learning API
- **No authentication required** for discovery

#### 2. OpenAPI Specification (JSON)
- **URL**: `http://controller-ip/api/openapi.json`
- **Best for**: Automation, code generation, tool integration
- **No authentication required** for specification access

#### 3. Endpoint Registry (Discovery)
- **URL**: `http://controller-ip/api/endpoints`
- **Best for**: Dynamic endpoint discovery, minimal metadata
- **No authentication required** for endpoint list

## OpenAPI Specification Structure

### Complete Example

```bash
# Fetch the specification
curl http://controller-ip/api/openapi.json > spec.json

# Validate with online validator
# https://editor.swagger.io
```

### Specification Components

**OpenAPI Version**: 3.0.0

**Top-Level Fields**:
```json
{
  "openapi": "3.0.0",
  "info": {
    "title": "BISSO E350 Controller API",
    "version": "1.0",
    "description": "CNC Controller REST API...",
    "contact": {"name": "BISSO E350"},
    "license": {"name": "Proprietary"}
  },
  "servers": [
    {"url": "http://localhost", "description": "Local device"},
    {"url": "https://device.local", "description": "HTTPS endpoint"}
  ],
  "paths": {...},
  "components": {
    "securitySchemes": {...}
  }
}
```

## API Endpoint Documentation

### Example: Status Endpoint

**Swagger UI View**:
```
GET /api/status
├─ Summary: System status and axis positions
├─ Tags: Status
├─ Security: HTTP Basic Auth required
├─ Rate Limit: 50 requests/min
├─ Responses:
│  ├─ 200: Success (application/json)
│  ├─ 401: Unauthorized
│  ├─ 429: Rate limit exceeded
│  └─ 400: Bad request
└─ Authentication: admin:password
```

### Example: Configuration Endpoint

**Swagger UI View**:
```
GET /api/config/get?category=0
├─ Summary: Get configuration
├─ Tags: Configuration
├─ Security: HTTP Basic Auth required
├─ Rate Limit: 50 requests/min
├─ Parameters:
│  └─ category (integer, query, required)
│     Description: Config category (0=motion, 1=vfd, 2=encoder, 3=safety, 4=thermal)
├─ Responses:
│  ├─ 200: Success (application/json)
│  ├─ 401: Unauthorized
│  └─ 429: Rate limit exceeded
└─ Example: /api/config/get?category=0
```

## Integration with Popular Tools

### 1. Postman (REST Client)

**Import Specification**:
1. Open Postman
2. Click **Import**
3. Select **Link** tab
4. Enter: `http://controller-ip/api/openapi.json`
5. Click **Continue** → **Import**

**Benefits**:
- Automatic request generation
- Variable management
- Collection organization
- Testing and monitoring
- Custom scripts and workflows

### 2. Insomnia (REST Client)

**Import Specification**:
1. Open Insomnia
2. Click **Create** → **Request Collection**
3. Select **Import** → **From URL**
4. Enter: `http://controller-ip/api/openapi.json`
5. Click **Scan**

**Benefits**:
- Plugin ecosystem
- Environment variables
- Request chaining
- GraphQL support

### 3. Swagger Editor (Online)

**View and Edit Specification**:
1. Go to https://editor.swagger.io
2. Click **File** → **Import URL**
3. Enter: `http://controller-ip/api/openapi.json`

**Benefits**:
- Visual editing
- Live validation
- Documentation generation
- Code generation

### 4. ReDoc (Documentation)

**Render Professional Documentation**:
```html
<!DOCTYPE html>
<html>
  <head>
    <title>API Documentation</title>
    <style>
      body { margin: 0; padding: 0; }
    </style>
  </head>
  <body>
    <redoc spec-url='http://controller-ip/api/openapi.json'></redoc>
    <script src="https://cdn.jsdelivr.net/npm/redoc@next/bundles/redoc.standalone.js"></script>
  </body>
</html>
```

### 5. Code Generation (OpenAPI Generator)

**Generate Client Code**:
```bash
# Python client
openapi-generator-cli generate \
  -i http://controller-ip/api/openapi.json \
  -g python \
  -o ./python-client

# JavaScript client
openapi-generator-cli generate \
  -i http://controller-ip/api/openapi.json \
  -g javascript \
  -o ./js-client

# Go client
openapi-generator-cli generate \
  -i http://controller-ip/api/openapi.json \
  -g go \
  -o ./go-client
```

## Authentication in API Clients

### Using Generated Clients

**Python Example** (using generated client):
```python
from swagger_client import ApiClient
from swagger_client.rest import ApiException

# Configure HTTP basic authentication
configuration = swagger_client.Configuration()
configuration.username = 'admin'
configuration.password = 'password'

api_client = ApiClient(configuration)
api_instance = swagger_client.ApiApi(api_client)

try:
    # Fetch status
    response = api_instance.api_status_get()
    print(response)
except ApiException as e:
    print("Exception: %s\n" % e)
```

**JavaScript Example** (using generated client):
```javascript
import { swagger } from './swagger-client';

// Configure authentication
swagger.setDefaultAuthentication('basicAuth', {
  username: 'admin',
  password: 'password'
});

// Fetch status
swagger.apis.Status.api_status_get()
  .then(response => console.log(response))
  .catch(error => console.error(error));
```

**cURL Example** (direct HTTP):
```bash
# With authentication
curl -u admin:password http://controller-ip/api/status

# With bearer token (if implemented)
curl -H "Authorization: Bearer token" http://controller-ip/api/status
```

## Testing in Swagger UI

### Manual Test Steps

1. **Open Swagger UI**: `http://controller-ip/api/docs`

2. **Authenticate**:
   - Click **Authorize** button (top right)
   - Select **basicAuth**
   - Enter username: `admin`
   - Enter password: `password`
   - Click **Authorize**

3. **Send Test Request**:
   - Find endpoint (e.g., `/api/status`)
   - Click on endpoint to expand
   - Click **Try it out** button
   - Adjust parameters if needed
   - Click **Execute**

4. **View Response**:
   - HTTP status code
   - Response headers (including rate limit info)
   - Response body (formatted JSON)

### Example: Testing Configuration Update

1. Find `/api/config/set` (POST)
2. Click **Try it out**
3. Enter request body:
```json
{
  "category": 0,
  "key": "x_min",
  "value": -100
}
```
4. Click **Execute**
5. View response validation result

## Rate Limiting Information

### In Swagger UI

Each endpoint displays its rate limit in the description:
```
GET /api/status
│
└─ Summary: System status (50 requests/min)
```

### In Response Headers

```bash
curl -i http://controller-ip/api/status

HTTP/1.1 200 OK
X-RateLimit-Limit: 50
X-RateLimit-Remaining: 45
X-RateLimit-Reset: 1640000000
Content-Type: application/json
```

### Understanding Rate Limits

- **X-RateLimit-Limit**: Maximum requests allowed
- **X-RateLimit-Remaining**: Requests available in current window
- **X-RateLimit-Reset**: Unix timestamp when limit resets
- **HTTP 429**: Response when limit exceeded

## Security Considerations

### Authentication

- All endpoints (except discovery) require **HTTP Basic Auth**
- Credentials configured in controller settings
- Transmitted in `Authorization` header as Base64

### Public Endpoints (No Auth Required)

- `/api/endpoints` - Endpoint discovery
- `/api/openapi.json` - Specification
- `/api/docs` - Swagger UI

**Rationale**: These endpoints expose no sensitive data, only API structure and documentation.

### Protected Endpoints

All other endpoints require authentication and rate limiting.

### HTTPS Recommendations

For production deployments:
1. Enable HTTPS on controller
2. Use strong credentials
3. Restrict network access
4. Implement per-user rate limiting
5. Monitor endpoint access

## Specification Validation

### Online Validators

**Swagger Editor**:
- URL: https://editor.swagger.io
- Paste specification JSON or provide URL
- Real-time validation and error detection

**Swagger Online Validator**:
- URL: https://validator.swagger.io
- Validates OpenAPI 2.0 and 3.0 specifications

### Command-Line Validation

```bash
# Using swagger-cli
swagger-cli validate http://controller-ip/api/openapi.json

# Using spectacle
spectacle http://controller-ip/api/openapi.json -d output/

# Using openapi-generator
openapi-generator-cli validate -i http://controller-ip/api/openapi.json
```

## Understanding Response Schemas

### Generic Schema Structure

```json
{
  "200": {
    "description": "Success",
    "content": {
      "application/json": {
        "schema": {
          "type": "object"
        }
      }
    }
  },
  "401": {
    "description": "Unauthorized"
  },
  "429": {
    "description": "Rate limit exceeded"
  }
}
```

### Common Response Types

**Status Response**:
```json
{
  "status": "IDLE",
  "x_pos": 0.0,
  "y_pos": 0.0,
  "z_pos": 10.5,
  "spindle_rpm": 0
}
```

**Configuration Response**:
```json
{
  "category": 0,
  "config": {
    "x_min": -100,
    "x_max": 500,
    "y_min": -100,
    "y_max": 500
  }
}
```

**Error Response**:
```json
{
  "error": "Invalid configuration value",
  "details": "Value out of range: 1000 > 500"
}
```

## Troubleshooting

### Swagger UI Doesn't Load

**Issue**: Blank page or loading spinner

**Solutions**:
1. Check network connectivity
2. Verify `http://controller-ip/api/docs` is accessible
3. Check browser console for errors
4. Clear browser cache
5. Try different browser

### Specification Not Loading in Swagger UI

**Issue**: Error loading specification from `/api/openapi.json`

**Solutions**:
```bash
# Test OpenAPI endpoint directly
curl http://controller-ip/api/openapi.json

# Check for JSON validity
curl http://controller-ip/api/openapi.json | jq

# Check web server logs
# Look for 500 errors or memory issues
```

### CORS Issues When Using External Tools

**Issue**: "Access to XMLHttpRequest blocked by CORS policy"

**Explanation**:
- Swagger UI CDN (jsdelivr.net) makes requests to controller
- Controller doesn't allow cross-origin requests by default

**Solutions**:
1. Deploy Swagger UI locally (not CDN)
2. Configure CORS headers on controller
3. Use tools that don't have CORS restrictions (Postman, Insomnia, cURL)

### Authentication Not Working in Swagger UI

**Issue**: 401 Unauthorized responses

**Solutions**:
1. Click **Authorize** button before testing
2. Verify username and password are correct
3. Check HTTP Basic Auth is properly formatted
4. Try testing with cURL: `curl -u admin:password http://controller-ip/api/status`

### Rate Limit Exceeded

**Issue**: HTTP 429 responses

**Solutions**:
1. Wait for rate limit window to reset (check X-RateLimit-Reset header)
2. Implement exponential backoff in client code
3. Cache responses when possible
4. Use compact endpoints for frequent polling
5. Check request frequency in your client code

## Best Practices

### 1. Use Discovery for Dynamic Integration

Instead of hard-coding endpoints:

```javascript
// Bad - hard-coded endpoints
fetch('/api/status')
  .then(r => r.json())
  .then(data => console.log(data));

// Good - discover endpoints dynamically
fetch('/api/openapi.json')
  .then(r => r.json())
  .then(spec => {
    const statusEndpoint = spec.paths['/api/status'].get;
    console.log('Status endpoint:', statusEndpoint);
    // Use discovered endpoint
    return fetch('/api/status');
  });
```

### 2. Monitor Rate Limits

```javascript
const makeRequest = async (url) => {
  const response = await fetch(url);
  const remaining = response.headers.get('X-RateLimit-Remaining');
  const limit = response.headers.get('X-RateLimit-Limit');

  console.log(`Rate limit: ${remaining}/${limit}`);

  if (remaining < 5) {
    console.warn('Approaching rate limit');
    // Implement backoff strategy
  }

  return response;
};
```

### 3. Implement Proper Error Handling

```javascript
const makeAuthenticatedRequest = async (endpoint, auth) => {
  try {
    const response = await fetch(endpoint, {
      headers: {
        'Authorization': 'Basic ' + btoa(auth)
      }
    });

    if (response.status === 401) {
      throw new Error('Authentication failed');
    } else if (response.status === 429) {
      throw new Error('Rate limit exceeded - please retry later');
    } else if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    return response.json();
  } catch (error) {
    console.error('API Error:', error);
    throw error;
  }
};
```

### 4. Generate Client Libraries

Use OpenAPI Generator to create type-safe client libraries:

```bash
# Python
openapi-generator-cli generate -i openapi.json -g python -o ./client

# TypeScript
openapi-generator-cli generate -i openapi.json -g typescript-axios -o ./client

# C#
openapi-generator-cli generate -i openapi.json -g csharp -o ./client
```

Benefits:
- Type safety
- Automatic validation
- IDE autocomplete
- Built-in error handling

## Implementation Details

### Files Created

- **include/openapi.h** - Header with OpenAPI generation functions
- **src/openapi.cpp** - OpenAPI 3.0 spec generator
- **spiffs/pages/swagger-ui.html** - Swagger UI interface
- **test/test_openapi.cpp** - 32 unit tests

### API Endpoint Additions

```
GET /api/openapi.json
├─ Returns: Complete OpenAPI 3.0 specification
├─ Auth: None required
├─ Rate Limit: Unlimited
└─ Response Type: application/json

GET /api/docs
├─ Returns: Swagger UI HTML page
├─ Auth: None required
├─ Rate Limit: Unlimited
└─ Response Type: text/html
```

### Generated Specification Size

- Typical size: 8-12 KB
- Includes all 23 API endpoints
- Full metadata: paths, methods, parameters, responses
- Security schemes and server information
- Optimized JSON with minimal whitespace

## Future Enhancements

1. **Endpoint Grouping by Tag** - Better organization in Swagger UI
2. **Request/Response Schema Definitions** - Full JSON schema validation
3. **Example Requests/Responses** - Sample data in documentation
4. **Deprecation Notices** - Mark legacy endpoints
5. **API Versioning** - Support multiple API versions
6. **Custom OpenAPI Extensions** - Controller-specific metadata

## Summary

The OpenAPI/Swagger integration provides:

- ✓ Professional API documentation
- ✓ Interactive testing interface
- ✓ Machine-readable specification
- ✓ Tool ecosystem integration
- ✓ Client code generation
- ✓ Authentication and security details
- ✓ Rate limit documentation
- ✓ Foundation for advanced features

This enables robust integration with third-party tools and client applications while maintaining API stability and security.

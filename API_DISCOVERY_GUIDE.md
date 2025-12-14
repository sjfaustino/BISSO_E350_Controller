# API Endpoint Discovery & Documentation - Phase 5.2

## Overview

The BISSO E350 Controller provides automatic API endpoint discovery and documentation through a centralized endpoint registry. This guide covers how to access API information, understand endpoint metadata, and integrate with the discovery system.

## Quick Start

### Discover All Endpoints

```bash
curl http://controller-ip/api/endpoints
```

Response includes all available endpoints with metadata:
- Path and HTTP methods
- Authentication requirements
- Rate limiting details
- Description and response type

### Example Response

```json
{
  "api_version": "1.0",
  "endpoints": [
    {
      "path": "/api/status",
      "methods": ["GET"],
      "description": "Get current system status (motion, VFD, spindle, safety)",
      "auth": true,
      "rate_limit": "50 requests/min"
    },
    {
      "path": "/api/config/get",
      "methods": ["GET"],
      "description": "Get configuration for a category (motion, VFD, encoder, safety, thermal)",
      "auth": true,
      "rate_limit": "50 requests/min"
    },
    ...
  ],
  "total": 23
}
```

## API Endpoints (Complete Registry)

### System Status & Telemetry

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/status` | GET | Yes | 50/min | Current system status (motion, VFD, spindle, safety) |
| `/api/telemetry` | GET | Yes | 20/min | Full telemetry data (frequency, current, temperature, position) |
| `/api/telemetry/compact` | GET | Yes | 100/min | Compact telemetry (reduced bandwidth, essential metrics) |
| `/api/health` | GET | Yes | 50/min | System health status (uptime, memory, network, VFD health) |
| `/api/metrics` | GET | Yes | 50/min | System metrics (request count, uptime, performance stats) |
| `/api/dashboard/metrics` | GET | Yes | 50/min | Dashboard metrics (optimized for web interface) |
| `/api/load` | GET | Yes | 50/min | System load information (CPU usage, memory, heap) |

### Motion Control

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/jog` | POST | Yes | 30/min | Jog an axis (X, Y, Z) with speed and distance |

### VFD / Spindle

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/spindle` | GET | Yes | 50/min | Spindle/VFD status (speed, current, frequency, fault, thermal) |

### Configuration Management

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/config/get` | GET | Yes | 50/min | Get configuration (motion, VFD, encoder, safety, thermal) |
| `/api/config/set` | POST | Yes | 30/min | Set configuration value with validation |
| `/api/config/validate` | POST | Yes | 50/min | Validate configuration change (pre-flight check) |
| `/api/config/schema` | GET | Yes | 50/min | Get configuration schema for client-side validation |

### Encoder & Diagnostics

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/encoder/calibrate` | POST | Yes | 20/min | Calibrate encoder for specified axis |
| `/api/encoder/diagnostics` | GET | Yes | 50/min | Get encoder diagnostics (jitter, deviation, calibration) |

### Firmware Update

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/update` | POST | Yes | 1/min | Upload and apply firmware update |
| `/api/update/status` | GET | Yes | 50/min | Check firmware update status and availability |

### File Management

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/files` | GET, DELETE | Yes | 50/min | File management (list and delete) |
| `/api/upload` | POST | Yes | 10/min | File upload endpoint |

### Discovery

| Endpoint | Method | Auth | Rate Limit | Description |
|----------|--------|------|-----------|-------------|
| `/api/endpoints` | GET | No | Unlimited | Discover all available API endpoints and metadata |

## Endpoint Metadata

Each endpoint descriptor contains:

```c
{
    "path": "/api/status",              // REST endpoint path
    "methods": ["GET", "POST", ...],    // Supported HTTP methods
    "description": "...",               // Human-readable description
    "auth": true/false,                 // HTTP Basic Auth required
    "rate_limit": "50 requests/min",    // Rate limiting configuration
    "response_type": "application/json" // Response MIME type
}
```

## Authentication

### Required Endpoints
Most endpoints require HTTP Basic Authentication with username and password configured in the controller.

### Public Endpoints
- `/api/endpoints` - Can be accessed without authentication for endpoint discovery

Example with authentication:

```bash
curl -u username:password http://controller-ip/api/status
```

## Rate Limiting

### How It Works
- Each endpoint has a configured rate limit (e.g., "50 requests/min")
- Exceeding the limit returns HTTP 429 (Too Many Requests)
- Headers indicate remaining requests and reset time

### Rate Limit Headers
```
X-RateLimit-Limit: 50
X-RateLimit-Remaining: 45
X-RateLimit-Reset: 1640000000
```

### Common Rate Limits
- **Status/Telemetry**: 50-100 requests/min (high frequency allowed)
- **Configuration**: 30-50 requests/min (moderate frequency)
- **Firmware Update**: 1 request/min (single operation)
- **Discovery**: Unlimited (metadata endpoint)

## Client Integration

### Automatic Discovery

```javascript
// Get all endpoints
fetch('/api/endpoints')
  .then(r => r.json())
  .then(data => {
    console.log(`API has ${data.total} endpoints`);

    // Find specific endpoint
    const configEndpoint = data.endpoints.find(ep =>
      ep.path === '/api/config/get'
    );

    console.log(`Config endpoint:`, configEndpoint);
  });
```

### Check Authentication Requirement

```javascript
// Before making a request, check if auth is required
const endpoint = endpoints.find(ep => ep.path === '/api/status');

if (endpoint.auth) {
  // Make request with authentication
  fetch('/api/status', {
    headers: {
      'Authorization': 'Basic ' + btoa('user:pass')
    }
  });
} else {
  // Make request without authentication
  fetch('/api/status');
}
```

### Respect Rate Limits

```javascript
// Monitor rate limit headers
const fetchWithRateLimit = async (url, options = {}) => {
  const response = await fetch(url, options);

  const remaining = response.headers.get('X-RateLimit-Remaining');
  const limit = response.headers.get('X-RateLimit-Limit');

  console.log(`Rate limit: ${remaining}/${limit}`);

  if (response.status === 429) {
    console.warn('Rate limit exceeded, backing off...');
    // Implement exponential backoff
  }

  return response;
};
```

## Endpoint Categories

### Telemetry Endpoints (Read-Only)
- `/api/status` - Real-time status
- `/api/telemetry` - Detailed measurements
- `/api/health` - System health checks

### Control Endpoints (State-Modifying)
- `/api/jog` - Motion control
- `/api/config/set` - Configuration changes
- `/api/update` - Firmware uploads

### Discovery Endpoints (Metadata)
- `/api/endpoints` - Registry (public, no auth required)
- `/api/config/schema` - Validation hints

## Best Practices

### 1. Use Discovery for Dynamic Integration
```javascript
// Instead of hard-coding endpoints
const discoverEndpoints = async () => {
  const response = await fetch('/api/endpoints');
  return response.json();
};

// Later, dynamically construct requests
const endpoints = await discoverEndpoints();
const statusEndpoint = endpoints.find(ep => ep.path === '/api/status');
```

### 2. Respect Rate Limits
- Implement exponential backoff on HTTP 429
- Cache responses when possible
- Use compact endpoints for frequent polling

### 3. Check Authentication Before Requesting
```javascript
const makeAuthenticatedRequest = async (endpoint, auth) => {
  if (endpoint.auth) {
    // Include authentication
    return fetch(endpoint.path, {
      headers: { 'Authorization': 'Basic ' + btoa(auth) }
    });
  } else {
    // No authentication needed
    return fetch(endpoint.path);
  }
};
```

### 4. Validate Response Type
```javascript
const fetchJSON = async (url, endpoint) => {
  const response = await fetch(url);

  if (endpoint.response_type !== 'application/json') {
    console.warn(`Unexpected response type: ${endpoint.response_type}`);
  }

  return response.json();
};
```

## Implementation Details

### Registry Location
- **Header**: `include/api_endpoints.h`
- **Implementation**: `src/api_endpoints.cpp`
- **Endpoint Handler**: `src/web_server.cpp` (lines ~344)

### Functions

```c
// Initialize registry
void apiEndpointsInit();

// Get all endpoints
const api_endpoint_t* apiEndpointsGetAll(int* count);

// Find endpoint by path
const api_endpoint_t* apiEndpointsFind(const char* path);

// Export as JSON
size_t apiEndpointsExportJSON(char* buffer, size_t buffer_size);

// Print to serial
void apiEndpointsPrint();
```

### Adding New Endpoints

1. Add entry to `api_endpoints[]` array in `src/api_endpoints.cpp`
2. Include all metadata (path, methods, description, auth, rate limits)
3. Rebuild firmware
4. New endpoint automatically appears in `/api/endpoints` response

Example:
```c
{
    .path = "/api/new",
    .methods = HTTP_GET,
    .description = "New endpoint description",
    .requires_auth = true,
    .rate_limited = true,
    .rate_limit_info = "50 requests/min",
    .response_type = "application/json"
}
```

## Testing

### Manual Testing

```bash
# Get all endpoints
curl http://192.168.1.100/api/endpoints

# Get specific endpoint info with jq
curl http://192.168.1.100/api/endpoints | \
  jq '.endpoints[] | select(.path == "/api/status")'

# Test with authentication
curl -u admin:password http://192.168.1.100/api/status
```

### Automated Testing
- Unit tests: `test/test_api_endpoints.cpp` (18 tests)
- Registry completeness verification
- Metadata validation
- Endpoint discovery validation

## Troubleshooting

### Missing Endpoint in Registry
- Check `src/api_endpoints.cpp` for endpoint definition
- Verify path matches exactly with handler path
- Rebuild firmware

### Rate Limit Issues
- Check current rate limit with response headers
- Implement exponential backoff
- Consider using compact endpoints for frequent polling

### Authentication Failure
- Verify endpoint requires auth (`"auth": true`)
- Check username and password
- Ensure HTTP Basic Auth is correctly formatted

## Future Enhancements

1. **OpenAPI/Swagger Integration** - Generate OpenAPI specification from registry
2. **Endpoint Grouping** - Organize endpoints by resource (motion, config, telemetry)
3. **Parameter Documentation** - Include request/response schema
4. **Endpoint Versioning** - Support multiple API versions
5. **Deprecation Notices** - Mark deprecated endpoints
6. **Usage Statistics** - Track endpoint popularity and performance

## Summary

The API Endpoint Discovery system provides:
- ✓ Centralized endpoint registry
- ✓ Automatic discovery via `/api/endpoints`
- ✓ Complete metadata for each endpoint
- ✓ Authentication and rate limit information
- ✓ Human-readable descriptions
- ✓ Easy client integration
- ✓ Foundation for OpenAPI documentation

This enables robust client integration and simplifies API maintenance.

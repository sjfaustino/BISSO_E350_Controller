# BISSO E350 Controller - Developer Guide

**Version:** 1.0  
**Last Updated:** December 30, 2025  
**Firmware Version:** Gemini v3.5.x  

> **For operators:** See [OPERATOR_QUICKSTART.md](OPERATOR_QUICKSTART.md) for operation instructions.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Hardware Architecture](#2-hardware-architecture)
3. [Software Architecture](#3-software-architecture)
4. [Development Environment Setup](#4-development-environment-setup)
5. [Code Style Guide](#5-code-style-guide)
6. [Motion Control System](#6-motion-control-system)
7. [Encoder Integration](#7-encoder-integration)
8. [Web Server & API](#8-web-server--api)
9. [Security Implementation](#9-security-implementation)
10. [Testing Framework](#10-testing-framework)
11. [Configuration System](#11-configuration-system)
12. [Debugging & Diagnostics](#12-debugging--diagnostics)
13. [Common Tasks](#13-common-tasks)

---

## 1. Project Overview

### 1.1 Purpose

The BISSO E350 Motion Controller Firmware is a custom embedded solution designed to upgrade and replace obsolete control systems in industrial bridge saw equipment. It leverages modern ESP32-S3 microcontrollers to manage motion control, safety, and human-machine interfaces.

**Project Codename:** Gemini

### 1.2 Core Function

The primary purpose is to manage a **single VFD (Variable Frequency Drive) motor** responsible for all primary motion axes (X, Y, Z, and rotational A). The system achieves closed-loop control by integrating high-speed communication with the machine's external I/O.

### 1.3 Motion Control Strategy: PLC I/O Emulation

This system operates as a **"smart I/O bridge"**, translating high-level digital commands into low-level binary signals that the machine's legacy PLC expects:

1. **High-Level Command:** System receives commands via CLI or Web interface
2. **Actuation (Output):** ESP32-S3 communicates with PCF8574 I²C I/O expanders (addresses `0x20`, `0x21`) to output binary signals
3. **Feedback (Input):** Position tracked via Wayjun WJ66 DRO reader over dedicated serial port

### 1.4 Repository Structure

```
BISSO_E350_Controller/
├── include/              # Header files (75 files)
│   ├── motion.h          # Motion control API
│   ├── encoder_wj66.h    # Encoder driver
│   ├── config_unified.h  # Configuration system
│   ├── web_server.h      # Web server (PsychicHttp)
│   └── ...
├── src/                  # Source files (91 files)
│   ├── main.cpp          # Entry point
│   ├── motion_control.cpp# Motion state machine
│   ├── web_server.cpp    # HTTP handlers
│   └── ...
├── lib/                  # Local libraries
│   ├── TestHelpers/      # Unit test utilities
│   └── TestMocks/        # Hardware mocks for testing
├── data/                 # Web assets (LittleFS)
│   ├── index.html        # Main dashboard
│   ├── bundle.css.gz     # Compressed CSS
│   └── pages/            # Additional pages
├── data_src/             # Uncompressed web assets (development)
├── docs/                 # Design documentation
│   ├── GEMINI_FINAL_AUDIT.md
│   └── ...
├── test/                 # Unit tests
├── platformio.ini        # Build configuration
└── optimize_assets.py    # Asset build script
```

---

## 2. Hardware Architecture

### 2.1 Control Board

| Specification | Detail |
|:---|:---|
| **Model** | KC868-A16 Industrial Controller |
| **Microcontroller** | ESP32-S3 (Dual-core, 240 MHz) |
| **I/O Count** | 16 Opto-isolated Inputs (X1-X16), 16 Relay Outputs (Y1-Y16) |
| **Flash** | 16 MB (3.1 MB for firmware, remainder for filesystem) |
| **RAM** | 320 KB SRAM |

### 2.2 Communication Interfaces

| Interface | Component/Protocol | Purpose | Firmware Modules |
|:---|:---|:---|:---|
| **I²C Bus** | PCF8574 I/O Expanders (0x20, 0x21) | Axis Select, Direction, Speed Profiles | `plc_iface.cpp`, `i2c_bus_recovery.cpp` |
| **Serial (UART)** | Wayjun WJ66 DRO Reader | 4-axis position data from optical encoders | `encoder_wj66.cpp`, `tasks_encoder.cpp` |
| **RS-485/Modbus** | Altivar 31 VFD, JXK10 Current Sensor, YH-TC05 | VFD telemetry, spindle monitoring | `altivar31_modbus.cpp`, `jxk10_modbus.cpp` |
| **WiFi** | ESP32 onboard | Web interface, OTA updates | `network_manager.cpp`, `web_server.cpp` |
| **NVS** | Internal Flash | Configuration, fault history | `config_unified.cpp`, `fault_logging.cpp` |

### 2.3 Axis Motor Control (Multiplexed)

**Key Principle:** Only ONE axis can move at a time. The PLC contactor system selects which motor receives VFD power.

```
User Command → ESP32 → I²C Expanders → PLC Contactors → Single Axis Motor
                           ↑                              ↓
                    (Axis Select,              WJ66 Encoder Feedback
                     Direction,                     ↓
                     Speed Profile)           ESP32 Position Tracking
```

### 2.4 Spindle Control

**IMPORTANT:** The spindle is controlled by a **separate VFD** that is NOT connected to this controller. The spindle is manually controlled by the operator.

---

## 3. Software Architecture

### 3.1 FreeRTOS Task Structure

| Task Name | Period | Priority | Core | Function |
|:---|:---|:---|:---|:---|
| **Safety** (`tasks_safety.cpp`) | 5 ms | P24 (CRITICAL) | Core 1 | E-Stop, motion stall detection |
| **Motion** (`tasks_motion.cpp`) | 10 ms | P22 (HIGH) | Core 1 | Move execution, axis state machine |
| **Encoder** (`tasks_encoder.cpp`) | 20 ms | P20 | Core 1 | WJ66 DRO polling |
| **PLC_Comm** (`tasks_plc.cpp`) | 50 ms | P18 | Core 1 | I²C I/O expander communication |
| **Monitor** (`tasks_monitor.cpp`) | 1000 ms | P12 | Core 1 | Memory check, telemetry updates |

### 3.2 Key Design Patterns

#### Module State Structs (Preferred Pattern)

Each module encapsulates its state in a static struct with accessor functions:

```cpp
// In .cpp file (private state)
static module_state_t state = {0};
static SemaphoreHandle_t state_mutex = NULL;

// Public accessor (thread-safe)
float moduleGetValue(void) {
    float value = 0.0f;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100))) {
        value = state.value;
        xSemaphoreGive(state_mutex);
    }
    return value;
}
```

#### Integer Core, Float Boundaries

The motion control uses **int32_t encoder counts** internally for zero cumulative error:

```
User Input (float MM) → Convert → Motion Buffer (int32_t counts)
                                        ↓
                                 Motion Planner (int32_t)
                                        ↓
                                 Encoder Driver (int32_t)
                                        ↓
                       Convert → Display (float MM)
```

**Why:** 
- Eliminates floating-point accumulation errors
- Hardware native format (WJ66 returns int32_t)
- Faster real-time performance (integer ops vs FPU)
- Perfect accuracy over long jobs

### 3.3 Fault Logging API

All errors route through central fault logging:

```cpp
void faultLogEntry(fault_severity_t severity, fault_code_t code, 
                   int32_t axis, int32_t value, const char* format, ...);
```

- **severity:** Determines action (CRITICAL triggers E-Stop)
- **axis:** 0-3 or -1 for system-wide
- **value:** Raw metric (memory, I2C address, deviation)
- **format:** Printf-style message

---

## 4. Development Environment Setup

### 4.1 Prerequisites

- **PlatformIO:** Install via VS Code extension or CLI
- **Python 3.8+:** For asset build scripts
- **Git:** Version control

### 4.2 Clone and Build

```bash
# Clone repository
git clone https://github.com/your-org/BISSO_E350_Controller.git
cd BISSO_E350_Controller

# Build firmware
pio run

# Build and upload
pio run -t upload

# Upload filesystem (web assets)
pio run -t uploadfs

# Monitor serial output
pio device monitor -b 115200
```

### 4.3 Build Environments (platformio.ini)

```ini
[env:esp32dev]           # Default development build
[env:esp32dev_debug]     # Debug build with extra logging
[env:esp32dev_test]      # Unit test build
```

### 4.4 Key Dependencies

From `platformio.ini`:
- `hoeken/PsychicHttp` - Web server (replaces ESPAsyncWebServer)
- `bblanchon/ArduinoJson@^7` - JSON parsing
- `throwtheswitch/Unity` - Unit testing

---

## 5. Code Style Guide

### 5.1 File Organization

- **Headers (.h):** Type definitions, function declarations, constants
- **Source (.cpp):** Implementation, static module state
- **Naming:** `module_name.cpp` / `module_name.h`

### 5.2 Global Variable Patterns

#### ✅ DO: Group related state into structs

```cpp
typedef struct {
    float speed;
    float current;
    bool fault;
    uint32_t runtime;
} motor_state_t;
static motor_state_t motor = {0};
```

#### ✅ DO: Use static for file-scope globals

```cpp
static module_state_t state = {0};  // File-scope only
```

#### ✅ DO: Provide accessor functions

```cpp
float moduleGetValue(void) {
    return state.value;
}
```

#### ❌ DON'T: Use extern for module state

```cpp
// BAD - exposes internals
extern motor_state_t motor;

// GOOD - controlled access
float motorGetSpeed(void);
```

### 5.3 Thread Safety

- **Spinlocks** for short critical sections (<10μs)
- **Mutexes** for longer operations (I2C, file access)
- **Critical sections** for ISR-safe operations

```cpp
// Spinlock pattern (fast, short operations)
portENTER_CRITICAL(&motionSpinlock);
int32_t pos = axes[axis].position;
portEXIT_CRITICAL(&motionSpinlock);

// Mutex pattern (longer operations)
if (taskLockMutex(taskGetMotionMutex(), 100)) {
    // Critical section
    taskUnlockMutex(taskGetMotionMutex());
}
```

### 5.4 Logging

Use the logging API from `serial_logger.h`:

```cpp
logInfo("[MODULE] Normal operation message");
logWarning("[MODULE] Warning: something unusual");
logError("[MODULE] Error: operation failed");
logDebug("[MODULE] Debug info (only in debug builds)");
```

---

## 6. Motion Control System

### 6.1 Motion States

```cpp
typedef enum {
    MOTION_IDLE = 0,
    MOTION_WAIT_CONSENSO = 1,
    MOTION_EXECUTING = 2,
    MOTION_STOPPING = 3,
    MOTION_PAUSED = 4,
    MOTION_ERROR = 5,
    MOTION_HOMING_APPROACH_FAST = 6,
    MOTION_HOMING_BACKOFF = 7,
    MOTION_HOMING_APPROACH_FINE = 8,
    MOTION_HOMING_SETTLE = 9,
    MOTION_DWELL = 10,
    MOTION_WAIT_PIN = 11
} motion_state_t;
```

### 6.2 Core Motion API

```cpp
// Movement commands
bool motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
bool motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s);
bool motionJog(float dx, float dy, float dz, float da, float speed_mm_s);

// Control commands
bool motionStop();
bool motionPause();
bool motionResume();
void motionEmergencyStop();
bool motionClearEmergencyStop();

// Homing
bool motionHome(uint8_t axis);

// Status
motion_state_t motionGetState(uint8_t axis);
float motionGetPositionMM(uint8_t axis);
bool motionIsMoving();
bool motionIsEmergencyStopped();
```

### 6.3 Motion Buffer

The motion buffer queues commands for sequential execution:

```cpp
// In motion_buffer.h
bool motionBufferPush(int32_t x_counts, int32_t y_counts, 
                      int32_t z_counts, int32_t a_counts);
bool motionBufferPop(motion_command_t* cmd);
bool motionBufferIsFull();
bool motionBufferIsEmpty();
```

### 6.4 Adding New Motion Commands

1. Declare in `include/motion.h`
2. Implement in `src/motion_control.cpp`
3. Add CLI command in `src/cli_motion.cpp`
4. Add API endpoint in `src/web_server.cpp`

---

## 7. Encoder Integration

### 7.1 WJ66 Encoder Driver

The Wayjun WJ66 DRO reader consolidates data from 4 optical encoders:

```cpp
// Get encoder position (thread-safe)
int32_t wj66GetPosition(uint8_t axis);

// Get position in millimeters
float motionGetPositionMM(uint8_t axis);

// Check encoder health
bool encoderMotionHasError(uint8_t axis);
```

### 7.2 Hardware Abstraction Layer

The encoder HAL allows switching between RS232/RS485:

```cpp
// In encoder_hal.h
void encoderHalInit(encoder_interface_t interface);
bool encoderHalRead(uint8_t* buffer, size_t len);
bool encoderHalWrite(const uint8_t* buffer, size_t len);
```

### 7.3 Calibration

```cpp
// Store pulses per mm calibration
// Accessed via MachineCalibration struct (machineCal)
machineCal.X.pulses_per_mm = 100.0f;
machineCal.Y.pulses_per_mm = 100.0f;
machineCal.Z.pulses_per_mm = 100.0f;
machineCal.A.pulses_per_degree = 22.222f;
```

---

## 8. Web Server & API

### 8.1 PsychicHttp Server

The web server uses PsychicHttp (replaced ESPAsyncWebServer):

```cpp
// Handler signature
[this](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
    // Authentication check
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;
    
    // Handle request
    JsonDocument doc;
    doc["status"] = "ok";
    
    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    return response->send(200, "application/json", buffer);
}
```

### 8.2 Key API Endpoints

| Endpoint | Method | Description |
|:---|:---|:---|
| `/api/status` | GET | System status |
| `/api/jog` | POST | Jog axes `{x, y, z, a, speed}` |
| `/api/faults` | GET | Fault history |
| `/api/config/get` | GET | Get configuration |
| `/api/config/set` | POST | Set configuration |
| `/api/endpoints` | GET | List all endpoints |
| `/api/openapi.json` | GET | OpenAPI specification |
| `/api/docs` | GET | Swagger UI |

### 8.3 Adding New Endpoints

1. Add handler in `WebServerManager::setupRoutes()` in `web_server.cpp`
2. Register in endpoint registry (for discovery)
3. Update OpenAPI spec if needed

```cpp
// Example: Add new endpoint
server.on("/api/myendpoint", HTTP_GET, 
    [this](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
    if (requireAuth(request, response) != ESP_OK) return ESP_OK;
    
    // Implementation
    return response->send(200, "application/json", "{\"result\":\"ok\"}");
});
```

### 8.4 WebSocket for Real-Time Updates

```cpp
// Broadcast state to all WebSocket clients
void WebServerManager::broadcastState() {
    if (wsHandler.count() == 0) return;
    
    JsonDocument doc;
    doc["x"] = motionGetPositionMM(0);
    doc["y"] = motionGetPositionMM(1);
    doc["z"] = motionGetPositionMM(2);
    
    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    wsHandler.sendAll(buffer);
}
```

---

## 9. Security Implementation

### 9.1 Credential Storage

All credentials stored in NVS, NOT in source code:

```cpp
// Get credentials
const char* username = configGetStr(KEY_WEB_USERNAME, "admin");
const char* password = configGetStr(KEY_WEB_PASSWORD, "");

// Set credentials
configSetStr(KEY_WEB_PASSWORD, newPassword);
configUnifiedSave();
```

### 9.2 Authentication

HTTP Basic Authentication on all protected endpoints:

```cpp
esp_err_t WebServerManager::requireAuth(PsychicRequest *request, PsychicResponse *response) {
    String authHeader = request->header("Authorization");
    // Validate credentials...
    if (!authenticated) {
        response->addHeader("WWW-Authenticate", "Basic realm=\"BISSO\"");
        response->send(401, "text/plain", "Authentication required");
        return ESP_FAIL;
    }
    return ESP_OK;
}
```

### 9.3 Rate Limiting

API endpoints have rate limiting:

```cpp
if (!apiRateLimiterCheck(API_ENDPOINT_STATUS, 0)) {
    return response->send(429, "application/json", 
        "{\"error\":\"Rate limit exceeded\"}");
}
```

### 9.4 Security Best Practices

- ❌ Never hardcode credentials in source
- ❌ Never log passwords in plain text
- ✅ Use NVS for credential storage
- ✅ Change default passwords immediately
- ✅ Use VPN for remote access (HTTP not HTTPS)

---

## 10. Testing Framework

### 10.1 Unity Test Framework

Tests use Unity with hardware mocks:

```cpp
// In test/test_motion/test_motion.cpp
void test_motion_jog_positive_x() {
    motionInit();
    bool result = motionJog(10.0f, 0.0f, 0.0f, 0.0f, 50.0f);
    TEST_ASSERT_TRUE(result);
}
```

### 10.2 Running Tests

```bash
# Run all tests
pio test

# Run specific test
pio test -f test_motion
```

### 10.3 Mock Objects

Hardware mocks in `lib/TestMocks/`:

- `MockEncoder` - Simulates WJ66 encoder
- `MockI2C` - Simulates I2C bus
- `MockVFD` - Simulates Altivar 31

### 10.4 Mock Data Mode (Web UI)

Press **M** key in dashboard to toggle mock mode for UI testing without hardware.

---

## 11. Configuration System

### 11.1 Unified Configuration API

```cpp
// Read configuration
int32_t value = configGetInt(KEY_MOTION_SPEED, 100);
float fvalue = configGetFloat(KEY_ENCODER_PPM, 100.0f);
const char* str = configGetStr(KEY_WEB_USERNAME, "admin");

// Write configuration
configSetInt(KEY_MOTION_SPEED, 150);
configSetFloat(KEY_ENCODER_PPM, 105.5f);
configSetStr(KEY_WEB_USERNAME, "operator");

// Persist to NVS
configUnifiedSave();
```

### 11.2 Configuration Keys

Defined in `include/config_keys.h`. Key naming convention:

- `KEY_MOTION_*` - Motion control
- `KEY_ENCODER_*` - Encoder settings
- `KEY_WEB_*` - Web interface
- `KEY_NET_*` - Network settings

### 11.3 Adding New Configuration

1. Add key constant in `config_keys.h`
2. Add default in `config_defaults.h`
3. Use via `configGetXxx()` / `configSetXxx()`

---

## 12. Debugging & Diagnostics

### 12.1 Serial Console

Connect at 115200 baud. Key commands:

| Command | Description |
|:---|:---|
| `help` | List all commands |
| `info` | System information |
| `debug` | Full diagnostic dump |
| `faults` | View fault history |
| `motion` | Motion system status |
| `axis status` | Per-axis status |
| `vfd status` | VFD diagnostics |

### 12.2 Web Diagnostics

Navigate to `/api/docs` for Swagger UI with interactive API testing.

### 12.3 Common Debug Patterns

```cpp
// Conditional debug logging
#ifdef DEBUG_BUILD
logDebug("[MODULE] Detailed state: %d", state);
#endif

// Performance timing
uint32_t start = micros();
// ... operation ...
uint32_t elapsed = micros() - start;
if (elapsed > 1000) {
    logWarning("[MODULE] Slow operation: %lu us", elapsed);
}
```

---

## 13. Common Tasks

### 13.1 Adding a New CLI Command

1. Create handler in appropriate `cli_*.cpp` file:
```cpp
void cmd_mycommand(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("Usage: mycommand <arg>");
        return;
    }
    // Implementation
}
```

2. Register in `cli_init()`:
```cpp
cliRegisterCommand("mycommand", cmd_mycommand, "Description");
```

### 13.2 Adding a New Configuration Option

1. Add key in `config_keys.h`:
```cpp
#define KEY_MY_OPTION "my_option"
```

2. Use in code:
```cpp
int value = configGetInt(KEY_MY_OPTION, 42);  // 42 = default
```

### 13.3 Updating Web Assets

1. Edit files in `data_src/`
2. Run `python optimize_assets.py`
3. Upload: `pio run -t uploadfs`

### 13.4 Building for Production

```bash
# Clean build
pio run -t clean

# Build release
pio run

# Verify size
# RAM: <50% recommended
# Flash: <80% recommended
```

---

## Appendix A: Key Files Reference

| File | Purpose |
|:---|:---|
| `main.cpp` | Entry point, task initialization |
| `motion_control.cpp` | Motion state machine, move commands |
| `motion_buffer.cpp` | Command queue |
| `motion_planner.cpp` | Real-time motion execution |
| `encoder_wj66.cpp` | WJ66 DRO driver |
| `web_server.cpp` | HTTP server, API endpoints |
| `config_unified.cpp` | NVS configuration system |
| `fault_logging.cpp` | Fault recording and history |
| `tasks_*.cpp` | FreeRTOS task implementations |
| `plc_iface.cpp` | I2C I/O expander control |
| `altivar31_modbus.cpp` | VFD Modbus communication |

---

## Appendix B: Security Checklist

Before deployment:

- [ ] Change default web password (`web_setpass <password>`)
- [ ] Change default OTA password (`ota_setpass <password>`)
- [ ] Configure WiFi credentials
- [ ] Verify network is isolated (not internet-facing)
- [ ] Test E-Stop functionality
- [ ] Verify encoder calibration

---

**Document maintained by:** BISSO Development Team  
**Last audit:** December 2025

# Universal GCode Sender (UGS) Compatibility Analysis

**Document Version:** 1.0
**Target Software:** Universal GCode Sender (UGS)
**Current Firmware:** Gemini v3.5.x
**Analysis Date:** 2025-12-08

---

## Executive Summary

**Current Status:** ⚠️ **PARTIAL COMPATIBILITY** - Basic G-code parsing works, but UGS-specific protocol features are missing.

**What Works:**
- ✅ G-code parser (G0, G1, G90, G91, G92)
- ✅ Serial communication over USB
- ✅ Command-line interface for manual testing
- ✅ Basic M-codes (M0, M2, M112)

**What's Missing:**
- ❌ "ok" / "error" response format (Grbl protocol)
- ❌ Status reporting (`?` command, `<Idle|...>` format)
- ❌ Real-time commands (feed hold, cycle start, reset)
- ❌ $ commands for settings
- ❌ Alarm state reporting
- ❌ G-code buffer management signals

**Estimated Effort to Make UGS-Compatible:** 3-4 days

---

## 1. Current G-Code Implementation

### 1.1 Supported G-Codes

Your current parser supports:

| G-Code | Function | Status |
|--------|----------|--------|
| **G0** | Rapid positioning | ✅ Implemented |
| **G1** | Linear move | ✅ Implemented |
| **G90** | Absolute mode | ✅ Implemented |
| **G91** | Relative mode | ✅ Implemented |
| **G92** | Set position | ⚠️ Partially implemented |

### 1.2 Supported M-Codes

| M-Code | Function | Status |
|--------|----------|--------|
| **M0** | Program pause | ✅ Implemented |
| **M2** | Program end | ✅ Implemented |
| **M112** | Emergency stop | ✅ Implemented |

### 1.3 How Commands Are Processed

From `cli_base.cpp:102-116`:
```cpp
void cliProcessCommand(const char* cmd) {
  // --- G-Code Auto-Detection ---
  // If line starts with 'G' or 'M' followed by a digit, send to Parser
  if ((cmd[0] == 'G' || cmd[0] == 'M') && isdigit(cmd[1])) {
      if (gcodeParser.processCommand(cmd)) {
          // Command successfully handled by G-Code engine
          return;
      }
  }
  // ... falls through to CLI commands
}
```

**Problem:** After processing a G-code command, **no response is sent back**. UGS expects "ok" or "error" after every command.

---

## 2. UGS Protocol Requirements

Universal GCode Sender expects a **Grbl-compatible protocol**:

### 2.1 Command Response Format

After **every** G-code command, controller must respond with:

```
ok
```

Or if there's an error:

```
error:1 (G-code Unsupported Command)
error:2 (Bad number format)
error:3 (Invalid $ statement)
... etc
```

**Current Status:** ❌ **NOT IMPLEMENTED**

---

### 2.2 Status Reporting

UGS periodically sends a **`?` character** to query machine status.

Controller must respond immediately with:

```
<Idle|MPos:0.000,10.000,0.000,0.000|FS:0,0|WCO:0.000,0.000,0.000,0.000>
```

Or:

```
<Run|MPos:50.123,10.456,0.000,0.000|FS:500,0>
```

**Format:**
```
<State|MPos:X,Y,Z,A|FS:FeedRate,SpindleRPM|WCO:X,Y,Z,A>
```

**States:** Idle, Run, Hold, Jog, Alarm, Door, Check, Home, Sleep

**Current Status:** ❌ **NOT IMPLEMENTED**

---

### 2.3 Real-Time Commands

UGS sends single-byte commands that should be processed **immediately** (not buffered):

| Byte | Command | Action |
|------|---------|--------|
| **`?`** | Status query | Send status report |
| **`!`** | Feed hold | Pause motion immediately |
| **`~`** | Cycle start | Resume motion |
| **`0x18`** | Soft reset | Reset controller (Ctrl-X) |

**Current Status:** ❌ **NOT IMPLEMENTED**

---

### 2.4 $ Commands (Settings)

UGS expects Grbl-style settings commands:

```
$$ - View all settings
$# - View work coordinate offsets
$G - View G-code parser state
$I - View build info
$N - View startup blocks
$x=value - Set setting x to value
```

Example:
```
$0=10    (Step pulse time)
$1=25    (Step idle delay)
$100=250 (X steps/mm)
```

**Current Status:** ❌ **NOT IMPLEMENTED** (but you have NVS config system that could be mapped)

---

### 2.5 Startup Message

When UGS connects, it expects a startup message like:

```
Grbl 1.1h ['$' for help]
```

Or custom:

```
BISSO E350 v3.5.19 ['$' for help]
```

**Current Status:** ⚠️ **PARTIAL** - Controller sends startup logs but not in Grbl format

---

## 3. Implementation Plan for UGS Compatibility

### Phase 1: Basic Protocol Support (2 days)

#### 1.1 Add "ok" Response After G-Code Commands

**Modify:** `src/gcode_parser.cpp:29`

```cpp
bool GCodeParser::processCommand(const char* line) {
    if (!line || strlen(line) == 0) {
        Serial.println("error:1");  // Empty command
        return false;
    }

    // Skip comments
    if (line[0] == '(' || line[0] == ';') {
        Serial.println("ok");  // Comments are "successful"
        return true;
    }

    // Look for G codes
    float gVal = -1.0f;
    if (parseCode(line, 'G', gVal)) {
        int cmd = (int)gVal;
        switch (cmd) {
            case 0:
            case 1:
                handleG0_G1(line);
                Serial.println("ok");  // <-- ADD THIS
                break;
            case 90:
                handleG90();
                Serial.println("ok");  // <-- ADD THIS
                break;
            case 91:
                handleG91();
                Serial.println("ok");  // <-- ADD THIS
                break;
            case 92:
                handleG92(line);
                Serial.println("ok");  // <-- ADD THIS
                break;
            default:
                Serial.printf("error:1 (Unsupported G%d)\n", cmd);  // <-- ADD THIS
                return false;
        }
        return true;
    }

    // Look for M codes
    float mVal = -1.0f;
    if (parseCode(line, 'M', mVal)) {
        int cmd = (int)mVal;
        switch (cmd) {
            case 0:
            case 2:
                motionStop();
                Serial.println("ok");  // <-- ADD THIS
                break;
            case 112:
                motionEmergencyStop();
                Serial.println("ok");  // <-- ADD THIS
                break;
            default:
                Serial.printf("error:1 (Unsupported M%d)\n", cmd);  // <-- ADD THIS
                return false;
        }
        return true;
    }

    Serial.println("error:1 (Unknown command)");  // <-- ADD THIS
    return false;
}
```

---

#### 1.2 Add Status Reporting (`?` Command)

**Create new file:** `src/grbl_protocol.cpp`

```cpp
/**
 * @file grbl_protocol.cpp
 * @brief Grbl Protocol Compatibility Layer for UGS
 */

#include "grbl_protocol.h"
#include "motion.h"
#include "motion_state.h"
#include "safety.h"
#include <Arduino.h>

void grblSendStatusReport() {
    // Determine machine state
    const char* state = "Idle";

    if (motionIsMoving()) {
        state = "Run";
    } else if (safetyIsAlarmed()) {
        state = "Alarm";
    } else {
        // Check for hold/pause
        motion_state_t mstate = motionGetState(motionGetActiveAxis());
        if (mstate == MOTION_PAUSED) {
            state = "Hold";
        }
    }

    // Get positions (in mm)
    float x = motionGetPositionMM(0);
    float y = motionGetPositionMM(1);
    float z = motionGetPositionMM(2);
    float a = motionGetPositionMM(3);

    // Get current feed rate (if available)
    float feed = 0.0;  // TODO: Get from motion system
    float spindle = 0.0;  // TODO: Not applicable for your system

    // Format: <State|MPos:X,Y,Z,A|FS:Feed,Spindle>
    Serial.printf("<%s|MPos:%.3f,%.3f,%.3f,%.3f|FS:%.0f,%.0f>\n",
                  state, x, y, z, a, feed, spindle);
}

void grblSendWelcomeMessage() {
    Serial.println("BISSO E350 v3.5.19 ['$' for help]");
}

void grblSendAlarm(uint8_t alarm_code) {
    Serial.printf("ALARM:%d\n", alarm_code);
}

void grblSendError(uint8_t error_code, const char* description) {
    if (description) {
        Serial.printf("error:%d (%s)\n", error_code, description);
    } else {
        Serial.printf("error:%d\n", error_code);
    }
}
```

**Create new file:** `include/grbl_protocol.h`

```cpp
#ifndef GRBL_PROTOCOL_H
#define GRBL_PROTOCOL_H

#include <stdint.h>

// Status reporting
void grblSendStatusReport();
void grblSendWelcomeMessage();

// Error/Alarm reporting
void grblSendAlarm(uint8_t alarm_code);
void grblSendError(uint8_t error_code, const char* description);

// Grbl error codes (subset)
#define GRBL_ERROR_UNSUPPORTED_CMD       1
#define GRBL_ERROR_BAD_NUMBER_FORMAT     2
#define GRBL_ERROR_INVALID_STATEMENT     3
#define GRBL_ERROR_NEGATIVE_VALUE        4
#define GRBL_ERROR_SETTING_DISABLED      5
#define GRBL_ERROR_SOFT_LIMIT            20
#define GRBL_ERROR_OVERFLOW              21
#define GRBL_ERROR_MAX_TRAVEL            22

// Grbl alarm codes
#define GRBL_ALARM_HARD_LIMIT            1
#define GRBL_ALARM_SOFT_LIMIT            2
#define GRBL_ALARM_ABORT_CYCLE           3
#define GRBL_ALARM_PROBE_FAIL            4
#define GRBL_ALARM_HOMING_FAIL           5

#endif
```

---

#### 1.3 Integrate Real-Time Commands into CLI

**Modify:** `src/cli_base.cpp:75-99`

```cpp
void cliUpdate() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    // ========== NEW: Real-Time Command Detection ==========
    // These must be processed IMMEDIATELY, not buffered

    if (c == '?') {
      // Status query - respond immediately
      grblSendStatusReport();
      continue;  // Don't add to buffer
    }

    if (c == '!') {
      // Feed hold - pause motion immediately
      motionPause();
      Serial.println("ok");
      continue;
    }

    if (c == '~') {
      // Cycle start - resume motion
      motionResume();
      Serial.println("ok");
      continue;
    }

    if (c == 0x18) {  // Ctrl-X soft reset
      Serial.println("BISSO E350 Resetting...");
      delay(100);
      ESP.restart();
      continue;
    }
    // ========== END Real-Time Commands ==========

    if (c == '\n' || c == '\r') {
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        Serial.println();
        cliProcessCommand(cli_buffer);
        cli_pos = 0;
      } else {
        Serial.println();
      }

      // Only show prompt if NOT in UGS mode
      // UGS doesn't expect prompts
      // cliPrintPrompt();  // <-- COMMENT THIS OUT

    } else if (c == '\b' || c == 0x7F) {
      if (cli_pos > 0) {
        cli_pos--;
        Serial.write('\b');
        Serial.write(' ');
        Serial.write('\b');
      }
    } else if (c >= 32 && c < 127 && cli_pos < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_pos++] = c;
      // Don't echo in UGS mode
      // Serial.write(c);  // <-- COMMENT THIS OUT
    }
  }
}
```

---

#### 1.4 Send Welcome Message on Startup

**Modify:** `src/main.cpp` (in setup function)

```cpp
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Send Grbl-compatible welcome message for UGS
    grblSendWelcomeMessage();  // <-- ADD THIS

    // ... rest of initialization
}
```

---

### Phase 2: $ Commands Support (1 day)

#### 2.1 Map Your Config System to Grbl $ Commands

You already have a comprehensive config system in NVS. Just need to map to $ format.

**Add to:** `src/grbl_protocol.cpp`

```cpp
void grblProcessSettingsCommand(const char* cmd) {
    // $$ - View all settings
    if (strcmp(cmd, "$$") == 0) {
        Serial.println("$100=1000.000 (X PPM)");
        Serial.println("$101=1000.000 (Y PPM)");
        Serial.println("$102=1000.000 (Z PPM)");
        Serial.println("$103=1000.000 (A PPM)");
        // ... etc
        Serial.println("ok");
        return;
    }

    // $# - View work coordinate offsets
    if (strcmp(cmd, "$#") == 0) {
        Serial.println("[G54:0.000,0.000,0.000,0.000]");
        Serial.println("[G55:0.000,0.000,0.000,0.000]");
        // ... etc
        Serial.println("ok");
        return;
    }

    // $G - View G-code parser state
    if (strcmp(cmd, "$G") == 0) {
        Serial.println("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]");
        Serial.println("ok");
        return;
    }

    // $I - View build info
    if (strcmp(cmd, "$I") == 0) {
        Serial.println("[VER:BISSO E350 v3.5.19]");
        Serial.println("[OPT:V,15,128]");  // Variable spindle, 15 axes, 128 buffer
        Serial.println("ok");
        return;
    }

    // $100=value format - set setting
    if (cmd[0] == '$' && isdigit(cmd[1])) {
        // Parse $XXX=value
        int setting_num = atoi(&cmd[1]);
        char* equals = strchr(cmd, '=');
        if (equals) {
            float value = atof(equals + 1);
            // Map to your config keys
            // Example: $100 = X PPM
            if (setting_num == 100) {
                configSetFloat("ppm_x", value);
                Serial.println("ok");
                return;
            }
            // ... more mappings
        }
    }

    Serial.println("error:3 (Invalid $ command)");
}
```

**Call from:** `src/cli_base.cpp:102`

```cpp
void cliProcessCommand(const char* cmd) {
  if (strlen(cmd) == 0) return;

  // NEW: $ command detection
  if (cmd[0] == '$') {
      grblProcessSettingsCommand(cmd);
      return;
  }

  // ... existing G-code detection
}
```

---

### Phase 3: Advanced Features (1 day - Optional)

#### 3.1 Add More G-Codes for Compatibility

UGS may send these common codes:

| G-Code | Function | Implementation Needed |
|--------|----------|----------------------|
| **G4** | Dwell (pause) | `delay(P_milliseconds)` |
| **G17/G18/G19** | Plane selection | Just acknowledge with "ok" |
| **G20/G21** | Inches/MM mode | Track unit mode |
| **G28** | Go to predefined position | Move to home |
| **G54-G59** | Work coordinate systems | Track active WCS |

#### 3.2 Add Buffer Management

UGS needs to know when to send next command. Two approaches:

**Option A: Flow Control Characters**
```cpp
// After processing each command
Serial.println("ok");
```

**Option B: Buffer Status in Status Report**
```cpp
Serial.printf("<%s|MPos:%.3f|Bf:%d,%d>\n", state, pos, blocks_available, rx_bytes_available);
```

---

## 4. Configuration for UGS

### 4.1 UGS Settings to Use

In UGS, configure as:

1. **Controller Type:** Grbl (or GRBL)
2. **Baud Rate:** 115200
3. **Port:** Select your ESP32 COM port
4. **Firmware:** Set to "GRBL 1.1"

### 4.2 Disable Incompatible Features

In UGS preferences, disable:
- ❌ Spindle control (you don't have VFD access)
- ❌ Coolant control (manual for now)
- ❌ Homing (unless you implement it)
- ❌ Continuous jogging (your system is sequential)

---

## 5. Testing Checklist

### 5.1 Basic Protocol Test

```
# Send via serial terminal
G0 X10
> ok

?
> <Idle|MPos:10.000,0.000,0.000,0.000|FS:0,0>

G1 X20 F100
> ok

?
> <Run|MPos:15.234,0.000,0.000,0.000|FS:100,0>

!
> ok
> <Hold|MPos:15.234,0.000,0.000,0.000|FS:0,0>

~
> ok
> <Run|MPos:15.678,0.000,0.000,0.000|FS:100,0>
```

### 5.2 UGS Connection Test

1. Open UGS
2. Select COM port, 115200 baud
3. Click "Connect"
4. Should see: "BISSO E350 v3.5.19 ['$' for help]"
5. Status should show position
6. Try sending G0 X10
7. Machine should move, status should update

---

## 6. Implementation Effort Summary

| Phase | Tasks | Effort | Priority |
|-------|-------|--------|----------|
| **Phase 1** | "ok" responses, status reporting, real-time commands | 2 days | 🔴 CRITICAL |
| **Phase 2** | $ commands mapping | 1 day | 🟡 HIGH |
| **Phase 3** | Additional G-codes, buffer management | 1 day | 🟢 OPTIONAL |
| **TOTAL** | | **3-4 days** | |

---

## 7. Limitations Due to Your Architecture

Even with full UGS compatibility, these limitations remain:

### ❌ Cannot Support

1. **Continuous jogging** - Your system is sequential (one axis at a time)
2. **Simultaneous multi-axis motion** - Single VFD limitation
3. **Spindle speed control** - No VFD access from controller
4. **Coolant control** - Manual operation
5. **Feed rate override during motion** - Only 3 discrete speeds
6. **Smooth trajectory planning** - Discrete speeds, sequential motion

### ✅ Will Work

1. **Point-to-point G-code programs** - Works fine
2. **Sequential motion** - X, then Y, then Z
3. **Position display** - Real-time via WJ66 encoders
4. **Pause/Resume/Stop** - Fully supported
5. **Status monitoring** - Position, state
6. **Job loading** - Send G-code files from UGS

---

## 8. Alternative: Custom UGS Profile

Instead of full Grbl emulation, create a **custom UGS controller profile**:

UGS supports custom controller definitions via JSON configuration. You could define BISSO E350 as a custom controller type with:
- Custom command format
- Custom status reporting
- Custom capabilities (sequential only)

**Pros:**
- Less implementation work
- Can match your actual architecture
- No need to fake Grbl responses

**Cons:**
- Requires UGS configuration
- May not work with all UGS features
- Less portable (Grbl is standard)

---

## 9. Recommendation

### **Option A: Implement Basic Grbl Compatibility** (RECOMMENDED)

**Effort:** 2-3 days for Phase 1
**Value:** Works with UGS, bCNC, CNCjs, and other Grbl-compatible senders

**Implementation Order:**
1. Add "ok"/"error" responses (0.5 day)
2. Add `?` status reporting (0.5 day)
3. Add real-time commands (`!`, `~`, `0x18`) (0.5 day)
4. Add welcome message (0.1 day)
5. Testing and fixes (0.5 day)

**Result:** UGS will connect and can send G-code programs, monitor position, pause/resume

---

### **Option B: Use Web UI Instead**

Your system already has a web interface (`data/index.html`). You could enhance it to:
- Upload G-code files
- Visualize toolpath
- Real-time position display
- Job control (start/pause/stop)

**Effort:** 3-4 days
**Value:** Custom interface tailored to your sequential architecture

---

## 10. Next Steps

**If you want UGS compatibility, I recommend:**

1. **Start with Phase 1** - Basic protocol (2 days)
   - Add "ok" responses
   - Add status reporting
   - Add real-time commands

2. **Test with UGS** - Verify connection and basic operation

3. **Add Phase 2 if needed** - $ commands (1 day)

4. **Enhance as needed** - Additional G-codes, buffer management

**Would you like me to:**
1. **Implement Phase 1** (Grbl basic protocol)?
2. **Enhance the web UI** instead for G-code sending?
3. **Create a hybrid** (both UGS compatibility + web UI)?

Let me know which direction you prefer!

# 💻 BISSO E350 - CLI COMMAND MASTER REFERENCE (V2.0) 💻

```text
   ____ _     ___    ____ ___  __  __ __  __    _    _   _ ____  ____  
  / ___| |   |_ _|  / ___/ _ \|  \/  |  \/  |  / \  | \ | |  _ \/ ___| 
 | |   | |    | |  | |  | | | | |\/| | |\/| | / _ \ |  \| | | | \___ \ 
 | |___| |___ | |  | |__| |_| | |  | | |  | |/ ___ \| |\  | |_| |___) |
  \____|_____|___|  \____\___/|_|  |_|_|  |_/_/   \_\_| \_|____/|____/ 
                                                                       
              THE DEFINITIVE 70+ COMMAND TECHNICAL BIBLE
              =========================================
              COMPLETE COVERAGE • EXAMPLES • ERROR HANDLING
```

---

## 📜 Overview

The Command Line Interface (CLI) is the **ultimate gateway** to the BISSO E350 (PosiPro) internal logic. This document is the **exhaustive source of truth** for all primary commands and their subcommands.

**Access Methods:**
- **USB Serial**: 115200 baud, 8N1 (COM port on PC)
- **Telnet**: Port 23 over WiFi/Ethernet (requires authentication)
- **Web Terminal**: Built into Web UI at `/terminal`

```text
CLI ARCHITECTURE:
┌─────────────────────────────────────────────────────────────────────┐
│                         CLI COMMAND FLOW                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   Serial Input → cliUpdate() → cliProcessCommand()                  │
│                        │                                             │
│                        ├── G/M Codes → gcodeParser.processCommand()  │
│                        ├── $ Settings → Grbl handler                 │
│                        └── Text Commands → Registered Handlers       │
│                                                                      │
│   Command Structure:  <command> [subcommand] [args...]              │
│   Example:            config set wifi_ssid "MyNetwork"               │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 🧭 THE 7-SECTION DOCUMENTATION STANDARD

Every command in this manual is documented with:
1. **Syntax**: Exact command format with parameters
2. **Description**: Plain English explanation
3. **Subcommands**: Table of available subcommands (if applicable)
4. **Parameters**: Detailed parameter descriptions
5. **Usage Examples**: Real-world command sequences
6. **Expected Output**: What the terminal will display
7. **Possible Errors**: Error messages and solutions

---

## 1. 🔧 SYSTEM CORE & OPERATIONS

---

### `help` - Show All Commands

**Syntax:**
```
help
```

**Description:**
Displays a complete list of all registered CLI commands with their brief descriptions.

**How It Works:**
The CLI uses a command registration system where each module registers its commands at startup. When you type `help`, the CLI iterates through all registered command handlers in `cli.cpp` and prints their names and descriptions. Commands are grouped by category (Grbl, System, Hardware, etc.) for easier navigation.

```text
HELP COMMAND FLOW:
┌─────────────────────────────────────────────────────────────────────┐
│  help → cliCmdHelp() → Iterate commandHandlers[] → Print all       │
│                                                                      │
│  The commandHandlers[] array is populated at boot by:               │
│   • cliInit() - Core commands (help, info, status, reboot)         │
│   • cliRegisterCommand() - Module-specific commands                 │
│   • Each command has: name, handler function, description           │
└─────────────────────────────────────────────────────────────────────┘
```

**Expected Output:**
```text
=== BISSO E350 CLI Help ===
Grbl Commands:
  $         - Show Grbl settings
  $H        - Run homing cycle
  $G        - Show parser state
  ?         - Real-time status report
  !         - Feed hold
  ~         - Cycle start / resume
  Ctrl-X    - Soft reset

System Commands:
  help         - Show help
  info         - System info
  reboot       - Restart system
  config       - Configuration management
  ...
==========================
```

---

### `info` - System Information

**Syntax:**
```
info
```

**Description:**
Reports firmware version, hardware configuration, and key system parameters.

**How It Works:**
The `info` command queries compile-time constants and runtime configuration to build a Grbl-compatible version string. The version format `[VER:base.PosiPro:build]` identifies this as a PosiPro-enhanced Grbl fork. Hardware parameters are read from the configuration cache (`configGet*` functions) rather than raw NVS to ensure consistency.

```text
INFO DATA SOURCES:
┌─────────────────────────────────────────────────────────────────────┐
│  Firmware Version  → GRBL_VERSION + POSIPRO_VERSION (compile-time) │
│  I2C Frequency     → Wire.getClock() (runtime query)               │
│  RS485 Baud        → configGetUint32("rs485_baud")                  │
│  Encoder Baud      → configGetUint32("encoder_baud")                │
│  Echo/OTA Status   → configGetBool() flags                         │
└─────────────────────────────────────────────────────────────────────┘
```

**Expected Output:**
```text
[VER:1.1h.PosiPro:3.5.26]
[I2C: 400000 Hz (Fast Mode)]
[RS485: 9600 baud | Encoder: 9600 baud]
[Echo: ON | OTA Check: OFF]
```

---

### `status` - Quick System Dashboard

**Syntax:**
```
status
```

**Description:**
Displays a real-time summary of machine health including network, position, memory, and safety status.

**How It Works:**
The status command aggregates data from multiple subsystems in a single atomic snapshot. Each subsystem provides a status accessor function that the CLI calls sequentially. This is designed as a quick health check—for detailed diagnostics, use specialized commands like `memory stats` or `estop status`.

```text
STATUS DATA AGGREGATION:
┌─────────────────────────────────────────────────────────────────────┐
│  Uptime       → millis() / 1000 → formatted HH:MM:SS               │
│  Motion State → motionGetState() → IDLE/RUN/HOLD/HOME/ALARM        │
│  Positions    → motionGetPosition(axis) for X/Y/Z/A                │
│  Free Heap    → ESP.getFreeHeap() → formatted with commas          │
│  WiFi RSSI    → WiFi.RSSI() → dBm with quality indicator           │
│  E-Stop       → safetyGetEstopState() → Active/Clear               │
│  Safety       → alarmGetCount() → Active alarm count               │
└─────────────────────────────────────────────────────────────────────┘
```

**Expected Output:**
```text
=== BISSO E350 System Dashboard ===
┌───────────────────────────┬────────────────────────────┐
│ SYSTEM                    │ VALUES                     │
├───────────────────────────┼────────────────────────────┤
│ Uptime                    │ 04:32:15                   │
│ Motion State              │ IDLE                       │
│ Position X/Y/Z/A          │ 1234.5 / 567.8 / 50.0 / 0  │
│ Free Heap                 │ 145,432 bytes              │
│ WiFi RSSI                 │ -67 dBm                    │
│ E-Stop                    │ [OK] CLEAR                 │
│ Safety                    │ [OK] No Alarms             │
└───────────────────────────┴────────────────────────────┘
```

---

### `reboot` / `reset` - System Restart

**Syntax:**
```
reboot
reset
```

**Description:**
Performs a complete ESP32 system restart. All motion stops immediately.

**How It Works:**
The reboot command performs a controlled shutdown sequence before invoking `ESP.restart()`. Critical steps include flushing the NVS cache to prevent data loss and safely disabling motor outputs. The ESP32's watchdog timer is also disabled to prevent a double-reset.

```text
REBOOT SEQUENCE:
┌─────────────────────────────────────────────────────────────────────┐
│  1. motionStop()           → Halt all axis movement                 │
│  2. outputsDisable()       → Set all PLC outputs to safe state     │
│  3. configFlush()          → Write pending NVS changes             │
│  4. Serial.flush()         → Empty TX buffers                      │
│  5. delay(100)             → Allow messages to transmit            │
│  6. ESP.restart()          → Hardware reset                        │
└─────────────────────────────────────────────────────────────────────┘
```

> [!WARNING]
> This command immediately halts all axis motion and drops PLC signals. The 22kW spindle motor will coast to a stop.

**Usage Example:**
```
reboot
```

**Expected Output:**
```text
[SYSTEM] Rebooting...
```
*(Device disconnects and restarts)*

---

### `echo` - Terminal Echo Control

**Syntax:**
```
echo [on|off] [save]
```

**Description:**
Controls whether typed characters are echoed back to the terminal. Essential when using raw serial terminals like PuTTY.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `on` | Enable character echo |
| `off` | Disable character echo |
| `save` | Save current setting to NVS |

**Usage Examples:**
```
echo on         # Enable echo for current session
echo off save   # Disable echo and save permanently
```

**Expected Output:**
```text
[CLI] Echo is now ON
```

**How It Works:**
```
ECHO CONTROL:
┌─────────────────────────────────────────────────────────────────┐
│  Terminal Type       Echo Needed?                                  │
│  ───────────────────────────────────────────────────────────   │
│  Web Serial Monitor  NO (browser echoes)                          │
│  PuTTY/Telnet        YES (raw terminal)                           │
│  Arduino IDE         NO (auto-echoes)                             │
│  CNCjs/UGS           NO (G-code sender)                           │
│                                                                    │
│  The `save` flag stores preference to NVS for boot default.       │
└─────────────────────────────────────────────────────────────────┘
```

---

### `passwd` - Password Management

**Syntax:**
```
passwd [web|ota] <new_password>
```

**Description:**
Sets or updates security credentials for system services. Replaces the legacy `web_setpass` and `ota_setpass` commands with a unified interface.

**How It Works:**
```text
PASSWORD MANAGEMENT FLOW:
┌─────────────────────────────────────────────────────────────┐
│  passwd [type] [key]                                         │
│         │                                                    │
│         ├── web → authSetPassword()                          │
│         │          ├─ Check complexity (min 8 chars, mixed)  │
│         │          ├─ SHA-256 Hash + Salt                    │
│         │          └─ Save to NVS "auth" namespace           │
│         │                                                    │
│         └── ota → configSetString("ota_pass")                │
│                    └─ Save to NVS "config" namespace         │
└─────────────────────────────────────────────────────────────┘
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `web` | Set Web UI (admin) password |
| `ota` | Set Over-The-Air firmware update password |

**Usage Examples:**
```
passwd web MyStrongP@ss!
passwd ota UpdateMe123!
```

**Requirements:**
- Minimum 8 characters
- Mixed char types (lowercase, uppercase, numbers, symbols) recommended

**Expected Output (Web):**
```text
[AUTH] [OK] Web UI password updated successfully.
[AUTH] [OK] New password active immediately.
```

**Expected Output (OTA):**
```text
[OTA] [OK] OTA password updated successfully
[OTA] Reboot required for changes to take effect
```

---

## 2. 🕹️ GRBL COMPATIBILITY COMMANDS

These commands provide Grbl-compatible interfaces for CNC software compatibility.

---

### `$` - Show Grbl Settings

**Syntax:**
```
$
```

**Description:**
Displays all Grbl-compatible settings in the standard `$ID=Value` format.

**How It Works:**
The Grbl settings interface maps internal PosiPro configuration keys to standard Grbl `$ID` numbers for compatibility with G-code senders like CNCjs, UGS, and bCNC. When you type `$100=250`, the CLI parses this as "set ppm_x to 250" internally. This bidirectional mapping ensures the machine works with standard CNC software while maintaining the more descriptive internal key names.

```text
GRBL SETTING ID MAPPING:
┌─────────────────────────────────────────────────────────────────────┐
│  $100 → ppm_x (pulses per mm, X-axis)                            │
│  $101 → ppm_y                                                    │
│  $102 → ppm_z                                                    │
│  $103 → ppm_a (4th axis / rotation)                              │
│  $110 → max_speed_cal_x (calibrated max speed, mm/min)           │
│  $120 → default_accel (motion acceleration, mm/sec²)             │
│  $130 → x_max_travel (soft limit, mm)                            │
└─────────────────────────────────────────────────────────────────────┘
```

**Expected Output:**
```text
$100=200.000
$101=200.000
$102=200.000
$103=200.000
$110=1500.000
$111=1500.000
$112=500.000
$113=500.000
$120=100.000
$130=5000.000
ok
```

**Setting IDs:**
| ID | Description | Unit |
|----|-------------|------|
| `$100-103` | Axis scale (pulses/mm) | pulses/mm |
| `$110-113` | Max speed calibration | mm/min |
| `$120` | Default acceleration | mm/sec² |
| `$130-132` | Max travel (soft limits) | mm |

**Setting a Value:**
```
$100=250
```

---

### `$H` - Run Homing Cycle

**Syntax:**
```
$H
```

**Description:**
Initiates the homing sequence (equivalent to `G28`). Homes Z first for safety.

**How It Works:**
The homing cycle moves each axis toward its limit switch at the configured homing speed. The sequence is **Z first** (vertical axis) to ensure the blade clears the workpiece, then Y (bridge travel), then X (carriage). Each axis performs a two-phase approach: fast seek to the switch, back off, then slow approach for precision. The encoder position is zeroed upon switch contact.

```text
HOMING SEQUENCE LOGIC:
┌─────────────────────────────────────────────────────────────────────┐
│  Phase 1: Z-Axis (Blade Clearance)                               │
│    → Move Z+ at homing_speed until Z_LIMIT triggers              │
│    → Back off 5mm, approach slowly                               │
│    → Set Z = 0 (or configured home offset)                       │
│                                                                    │
│  Phase 2: Y-Axis (Bridge)                                        │
│    → Same pattern toward Y home switch                           │
│                                                                    │
│  Phase 3: X-Axis (Carriage)                                      │
│    → Same pattern toward X home switch                           │
└─────────────────────────────────────────────────────────────────────┘
```

> [!TIP]
> If homing fails, check that limit switches are connected and `home_enable` is set for each axis in `config dump`.

**Expected Output:**
```text
[MOTION] Homing sequence started...
[MOTION] Homing Z...
[MOTION] Homing Y...
[MOTION] Homing X...
ok
```

---

### `$G` - Parser State

**Syntax:**
```
$G
```

**Description:**
Shows the currently active G-code modal states.

**Expected Output:**
```text
[GC:G0 G54 G90 G94 M5]
ok
```

**How It Works:**
```
MODAL STATE TRACKING:
┌─────────────────────────────────────────────────────────────┐
│  The parser tracks these modal groups:                       │
│                                                              │
│  [GC:G0 G54 G90 G94 M5]                                     │
│       │   │   │   │  │                                      │
│       │   │   │   │  └── Spindle state (M3/M4/M5)          │
│       │   │   │   └───── Feed mode (G93=inv, G94=mm/min)   │
│       │   │   └───────── Distance mode (G90=abs, G91=inc)  │
│       │   └───────────── Work coord system (G54-G59)       │
│       └───────────────── Motion mode (G0=rapid, G1=linear) │
│                                                              │
│  Modal states persist until changed by a new command.        │
└─────────────────────────────────────────────────────────────┘
```

---

### `?` - Real-Time Status (Grbl)

**Syntax:**
```
?
```
*(Single character, no Enter required)*

**Description:**
Returns immediate status report with position, state, and buffer status.

**Expected Output:**
```text
<Idle|MPos:100.000,50.000,25.000,0.000|WPos:0.000,0.000,25.000,0.000|Bf:30,127|FS:0,0>
```

**Status Fields:**
| Field | Description |
|-------|-------------|
| `<State>` | Idle, Run, Hold:0, Hold:1, Home, Alarm |
| `MPos` | Machine position (X,Y,Z,A) |
| `WPos` | Work position (X,Y,Z,A) |
| `Bf` | Buffer status (plan slots, RX buffer) |
| `FS` | Feed and spindle (feed%, spindle RPM) |

**How It Works:**
```
REAL-TIME STATUS QUERY (bypasses command queue):
┌─────────────────────────────────────────────────────────────┐
│  '?' received on UART                                        │
│         │                                                    │
│         ↓                                                    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  ISR immediately samples:                           │    │
│  │  • Motion controller state                          │    │
│  │  • Encoder positions (all 4 axes)                   │    │
│  │  • G-code buffer occupancy                          │    │
│  │  • Current feed rate and spindle state              │    │
│  └─────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ↓                                                    │
│  Format as Grbl-compatible status string                     │
│  Send immediately (does NOT wait for ok)                     │
└─────────────────────────────────────────────────────────────┘
```

---

### `!` - Feed Hold (Pause)

**Syntax:**
```
!
```
*(Single character)*

**Description:**
Immediately pauses all axis motion. Program position is preserved.

**How It Works:**
```
FEED HOLD STATE TRANSITION:
┌─────────────────────────────────────────────────────────────┐
│                                                              │
│   RUNNING ───────('!')───────→ HOLD:0 (Decelerating)        │
│      ↑                              │                        │
│      │                              ↓                        │
│     ('~')                      HOLD:1 (Stopped)              │
│      │                              │                        │
│      └──────────────────────────────┘                        │
│                                                              │
│   Motion buffer is PRESERVED, not cleared                    │
│   Spindle continues running (safety: blade in cut)          │
│   Resume from exact pause point with '~'                     │
└─────────────────────────────────────────────────────────────┘
```

---

### `~` - Cycle Start (Resume)

**Syntax:**
```
~
```
*(Single character)*

**Description:**
Resumes motion after a feed hold (`!`) or program pause (M0/M1).

**How It Works:**
```
CYCLE START SEQUENCE:
┌─────────────────────────────────────────────────────────────┐
│  1. Verify system in HOLD state (not ALARM)                  │
│  2. Check E-stop not active                                  │
│  3. Transition to RUNNING state                              │
│  4. Resume motion from preserved position                    │
│  5. Continue executing queued G-code blocks                  │
└─────────────────────────────────────────────────────────────┘
```

---

### `Ctrl-X` / `0x18` - Soft Reset

**Syntax:**
```
Ctrl-X
```
*(ASCII 0x18)*

**Description:**
Performs a software reset—stops motion, clears buffers, reinitializes without rebooting.

**How It Works:**
```
SOFT RESET SEQUENCE:
┌─────────────────────────────────────────────────────────────┐
│  1. IMMEDIATELY stop all axis motion (decel to zero)         │
│  2. Clear G-code motion queue                                │
│  3. Reset parser state to defaults (G0 G54 G90)              │
│  4. Clear any ALARM state (not E-STOP)                       │
│  5. Re-initialize serial receive buffer                      │
│  6. Output startup banner:                                   │
│     "Grbl 1.1f ['$' for help]"                               │
│                                                              │
│  NOTE: Does NOT reboot ESP32 - much faster than `reboot`     │
│        Position is LOST - rehome after soft reset            │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. ⚙️ CONFIGURATION MANAGEMENT

---

### `config` - Configuration System

**Syntax:**
```
config <subcommand> [args...]
```

**Description:**
The central registry for all machine settings. Changes are cached until saved to Flash.

**How It Works:**
The configuration system uses a **write-through cache** architecture. All settings are stored in ESP32's NVS (Non-Volatile Storage) flash partition, but accessed through a mutex-protected RAM cache for performance and thread safety. When you `config set`, the value goes into the cache. When you `config save`, the cache is flushed to NVS. This prevents flash wear from frequent writes during calibration.

```text
CONFIG SYSTEM ARCHITECTURE:
┌─────────────────────────────────────────────────────────────────────┐
│                           config set                              │
│                               │                                     │
│                               ▼                                     │
│  ┌───────────────────┐     ┌───────────────────┐              │
│  │   RAM Cache       │ ─────┤   NVS (Flash)      │              │
│  │  (mutex-locked)   │ save │  Persistent Store  │              │
│  └───────────────────┘     └───────────────────┘              │
│         ▲                            │                           │
│         │             ───────────────┘                           │
│     config get          restore / boot load                      │
└─────────────────────────────────────────────────────────────────────┘
```

**Thread Safety:**
All `configGet*` and `configSet*` functions acquire a mutex before accessing the cache, making the config system safe to call from any FreeRTOS task (motion, telemetry, CLI, etc.).

**Subcommands:**
| Subcommand | Syntax | Description |
|------------|--------|-------------|
| `get` | `config get <key>` | Fetch a single setting |
| `set` | `config set <key> <value>` | Set a value (cached) |
| `save` | `config save` | Write cached changes to Flash |
| `dump` | `config dump` | List ALL settings |
| `validate` | `config validate` | Check settings for errors |
| `export` | `config export` | Export settings as JSON |
| `import` | `config import <json>` | Import settings from JSON |
| `backup` | `config backup` | Create internal backup |
| `restore` | `config restore` | Restore from backup |
| `reset` | `config reset` | Reset all settings to defaults |
| `show` | `config show` | Show current config state |
| `schema` | `config schema` | Show configuration schema |
| `nvs` | `config nvs <cmd>` | Low-level NVS operations |

---

#### `config get` - Get Single Value

**Syntax:**
```
config get <key>
```

**Usage Example:**
```
config get wifi_ssid
```

**Expected Output:**
```text
[CONFIG] wifi_ssid = "MyNetwork" (type: string)
```

---

#### `config set` - Set Value (Cached)

**Syntax:**
```
config set <key> <value>
```

**Usage Examples:**
```
config set wifi_ssid "MyNetwork"
config set ppm_x 200.5
config set home_enable 1
config save    # Required to persist!
```

**Expected Output:**
```text
[CONFIG] wifi_ssid = MyNetwork
[CONFIG] Changes staged. Use 'config save' to persist.
```

> [!IMPORTANT]
> Changes are cached in RAM until `config save` is called. Unsaved changes are lost on reboot!

---

#### `config dump` - List All Settings

**Syntax:**
```
config dump
```

**Description:**
Displays every configuration key with its current value.

**Expected Output:**
```text
=== Configuration Dump ===
ppm_x = 200.000
ppm_y = 200.000
ppm_z = 200.000
wifi_ssid = "BISSO_Factory"
wifi_pass = "********"
...
[Total: 127 keys]
```

---

#### `config export` - JSON Export

**Syntax:**
```
config export
```

**Description:**
Exports all configuration as JSON for backup or transfer.

**Expected Output:**
```json
{
  "ppm_x": 200.0,
  "ppm_y": 200.0,
  "wifi_ssid": "BISSO_Factory",
  ...
}
```

---

#### `config import` - JSON Import

**Syntax:**
```
config import <json_string>
```

**Usage Example:**
```
config import {"ppm_x":250,"ppm_y":250}
config save
```

---

#### `config backup` / `config restore`

**Syntax:**
```
config backup
config restore
```

**Description:**
Creates or restores an internal backup of all settings in NVS.

---

### `nvs` - NVS Storage Inspector

**Syntax:**
```
nvs <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `stats` | Show NVS partition statistics |
| `erase` | Erase entire NVS (DANGEROUS!) |

**Usage Example:**
```
nvs stats
```

**Expected Output:**
```text
[NVS] === Storage Statistics ===
Namespace: config
Total entries: 127
Used entries: 89
Free entries: 38
```

**How It Works:**
```
NVS (NON-VOLATILE STORAGE) ARCHITECTURE:
┌─────────────────────────────────────────────────────────────────┐
│                      ESP32 FLASH                                  │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐    │
│  │   NVS       │  │   LittleFS  │  │   OTA_0/1   │    │
│  │  Partition  │  │  (Web UI)   │  │  (Firmware) │    │
│  └──────┬────────┘  └───────────────┘  └───────────────┘    │
│         │                                                       │
│         └─────────────────────────────────┐                  │
│                             ↓                                    │
│   Key-Value Storage:                                              │
│   • Type-safe (int, float, string, blob)                          │
│   • Wear-leveling built-in                                        │
│   • Power-fail safe (journaling)                                  │
│   • ~20KB usable storage                                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. 🏃 MOTION CONTROL

---

### `motionstatus` - Low-Level Motion Diagnostics

**Syntax:**
```
motionstatus
```

**Description:**
Shows detailed internal motion state for all axes.

**How It Works:**
```
MOTION STATUS DATA SOURCES:
┌─────────────────────────────────────────────────────────────────┐
│  Per-Axis Report:                                                  │
│  • Target position (from last G-code command)                     │
│  • Actual position (from WJ66 encoder)                            │
│  • Distance remaining                                             │
│  • Current velocity (counts/ms)                                   │
│  • Axis state (IDLE, MOVING, HOMING, ERROR)                       │
│  • Limit switch status                                            │
│                                                                    │
│  System-level:                                                     │
│  • Motion buffer depth (queued commands)                          │
│  • Active feedrate override                                       │
│  • Current WCS offset applied                                     │
└─────────────────────────────────────────────────────────────────┘
```

---

### `stop` - Stop All Motion

**Syntax:**
```
stop
```

**Description:**
Immediately halts all axis movement. Does NOT trigger E-Stop state.

**How It Works:**
The `stop` command calls `motionStop()` which sends an immediate halt signal to all axis motor drivers via the PLC output expander. Unlike E-Stop, this is a "soft stop" that doesn't latch—you can immediately issue new motion commands. The deceleration is controlled by the configured acceleration limits to prevent mechanical shock.

```text
STOP vs E-STOP COMPARISON:
┌─────────────────────┬─────────────────────────────────────────────┐
│  Command            │  Behavior                                   │
├─────────────────────┼─────────────────────────────────────────────┤
│  stop               │  Controlled decel, no latch, immediate OK   │
│  estop on           │  Hard stop, latches, requires clear         │
│  ! (feed hold)      │  Controlled decel, resumable with ~         │
└─────────────────────┴─────────────────────────────────────────────┘
```

**Expected Output:**
```text
[MOTION] Stop command sent
ok
```

---

### `pause` - Pause Motion

**Syntax:**
```
pause
```

**Description:**
Pauses current motion, preserving position for resume.

**How It Works:**
The pause command saves the current target position and velocity, then decelerates all axes to a stop. The motion planner buffer is preserved, allowing seamless resume. This is equivalent to the Grbl `!` (feed hold) command. The system state changes to `HOLD` and the machine waits for either `resume` or `~` to continue.

---

### `resume` - Resume Motion

**Syntax:**
```
resume
```

**Description:**
Continues motion from a paused state.

**How It Works:**
Resume re-accelerates axes from the held position toward the original target using the saved velocity profile. If the job was paused mid-G-code, execution continues from the exact interrupted point. This is equivalent to the Grbl `~` (cycle start) command.

---

### `estop` - Emergency Stop Management

**Syntax:**
```
estop <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show E-Stop state |
| `on` | Trigger software E-Stop |
| `off` | Clear E-Stop (if safe) |

**How It Works:**
```
E-STOP STATE MACHINE:
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   NORMAL ──────[estop on]──────> E-STOPPED                      │
│     │                               │                            │
│     │                      [estop off + safety clear]            │
│     │                               │                            │
│     <──────────[cleared]────────────┘                            │
│                                                                  │
│   Note: Cannot clear if hardware E-Stop button is pressed!      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Usage Examples:**
```
estop status    # Check current state
estop on        # Trigger E-Stop
estop off       # Clear E-Stop (if conditions allow)
```

**Expected Output:**
```text
[MOTION] EMERGENCY STOP ACTIVE
```
or
```text
[MOTION] [OK] System Enabled
```

---

### `limit` - Soft Limit Configuration

**Syntax:**
```
limit <axis> <min> <max> [enable]
```

**Description:**
Configures software travel limits for an axis to prevent crashes.

**How It Works:**
Soft limits are checked by the motion planner before any move executes. If a target position exceeds the configured min/max, the move is rejected with an alarm. Unlike hardware limit switches (which stop motion reactively), soft limits are predictive—they prevent illegal moves from ever starting. Limits are stored per-axis in the configuration system.

```text
SOFT LIMIT CHECK FLOW:
┌─────────────────────────────────────────────────────────────────────┐
│  G-code received (e.g., G0 X3000)                                  │
│         │                                                          │
│         ▼                                                          │
│  motionValidateTarget(axis, target)                               │
│         │                                                          │
│         ├── target >= min AND target <= max → ALLOW               │
│         └── otherwise → REJECT with ALARM_SOFT_LIMIT              │
└─────────────────────────────────────────────────────────────────────┘
```

**Parameters:**
| Parameter | Description | Example |
|-----------|-------------|---------|
| `axis` | Axis letter (X/Y/Z/A) | X |
| `min` | Minimum position (mm) | 0 |
| `max` | Maximum position (mm) | 2500 |
| `enable` | 1=enable, 0=disable | 1 |

**Usage Example:**
```
limit X 0 2500 1
```

**Expected Output:**
```text
[MOTION] Soft limits updated for Axis 0
```

---

### `feed` - Feed Override

**Syntax:**
```
feed [factor]
```

**Description:**
Adjusts the real-time feed rate as a percentage of programmed speed.

**How It Works:**
Feed override applies a multiplier to all programmed feed rates (F values in G-code). A value of 1.0 (or 100%) means programmed speed, 0.5 means half speed, 2.0 means double. The override is applied in real-time—you can adjust it while the machine is moving. The value is clamped to the safe range (10%-200%) to prevent stalls or dangerous speeds.

> [!TIP]
> Use feed override during test cuts to fine-tune cutting speed without modifying your G-code program.

**Parameters:**
| Parameter | Description | Valid Range |
|-----------|-------------|-------------|
| `factor` | Multiplier (0.1-2.0) or percentage (10-200) | 0.1-2.0 or 10-200% |

**Usage Examples:**
```
feed            # Show current override
feed 1.5        # Set to 150%
feed 75         # Set to 75%
```

**Expected Output:**
```text
[CLI] Current Feed: 100%
```

---

### `predict` - Position Prediction Diagnostics

**Syntax:**
```
predict [axis]
```

**Description:**
Compares predicted position vs. actual encoder reading. High discrepancy indicates mechanical issues.

**How It Works:**
The motion system uses velocity-based prediction to estimate where each axis should be between encoder updates. The `predict` command shows the gap between predicted and actual positions. A small gap (<50 counts) is normal due to encoder timing. Larger gaps indicate:
- **Mechanical slip** (belt/gear issues)
- **Encoder miscounting** (noise, wiring)
- **Motor stalling** (overload, binding)

```text
PREDICTION ALGORITHM:
┌─────────────────────────────────────────────────────────────────────┐
│  Predicted = LastPosition + (Velocity × TimeSinceLastUpdate)     │
│  Gap = |Predicted - ActualEncoderReading|                        │
│                                                                    │
│  Gap < 50:   Normal operation                                     │
│  Gap 50-200: Minor lag (check encoder timing)                     │
│  Gap > 200:  Mechanical issue (investigate immediately)           │
└─────────────────────────────────────────────────────────────────────┘
```

**Usage Example:**
```
predict X
```

**Expected Output:**
```text
=== PREDICTION DIAGNOSTICS ===
Axis:            0
Raw Position:    245678
Actual Latched:  245670
Predicted:       245680
Prediction Gap:  10
Velocity:        2.500 counts/ms
Update Age:      12 ms
```

**Interpreting Results:**
| Prediction Gap | Meaning |
|----------------|---------|
| < 50 | Normal operation |
| 50-200 | Minor lag (acceptable) |
| > 200 | Mechanical slip or encoder issue |

---

### `spinlock` - Critical Section Timing

**Syntax:**
```
spinlock <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `stats` | Show spinlock timing statistics |
| `reset` | Reset timing counters |

**Description:**
Monitors critical section (spinlock) durations to detect code bottlenecks.

**Expected Output:**
```text
[SPINLOCK] === Timing Report ===
Max Hold Time:    8 µs
Average:          3 µs
Violations (>50): 0
```

> [!TIP]
> If max time exceeds 50µs frequently, the code should be refactored to use a mutex.

**How It Works:**
```
SPINLOCK TIMING INSTRUMENTATION:
┌─────────────────────────────────────────────────────────────────┐
│  Spinlocks disable interrupts for atomic access                    │
│  to shared data structures:                                       │
│                                                                    │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  portENTER_CRITICAL()  ←── Start timestamp         │ │
│  │       ... critical code ...                        │ │
│  │  portEXIT_CRITICAL()   ←── End timestamp           │ │
│  │       duration = end - start                       │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                    │
│  THRESHOLDS:                                                       │
│  • Normal: < 10µs        (OK for ISR-safe operations)             │
│  • Warning: 10-50µs     (May impact real-time response)           │
│  • Violation: > 50µs    (Risk of missed encoder edges)            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. 📡 NETWORK & CONNECTIVITY

---

### `wifi` - Wireless Network Management

**Syntax:**
```
wifi <subcommand> [args...]
```

**Description:**
Manages the ESP32's WiFi radio for station (client) and access point modes.

**How It Works:**
The ESP32 supports simultaneous Station (STA) and Access Point (AP) modes. In STA mode, it connects to your factory WiFi. In AP mode, it creates its own network (BISSO_E350_AP) for direct connection. Credentials are stored encrypted in NVS. The WiFi task runs at high priority to maintain connectivity.

```text
WIFI OPERATING MODES:
┌─────────────────────────────────────────────────────────────────────┐
│  STA Mode (Station)          AP Mode (Access Point)              │
│  ──────────────────────────  ─────────────────────────────────────── │
│  Connects to YOUR router     Creates BISSO_E350_AP network       │
│  Gets IP via DHCP            IP: 192.168.4.1                     │
│  Full internet access        Direct device connection only       │
│  Use: Normal operation       Use: Setup, no router available     │
└─────────────────────────────────────────────────────────────────────┘
```

**Subcommands:**
| Subcommand | Syntax | Description |
|------------|--------|-------------|
| `scan` | `wifi scan` | Scan for available networks |
| `connect` | `wifi connect <ssid> <pass>` | Connect to network |
| `disconnect` | `wifi disconnect` | Disconnect from network |
| `status` | `wifi status` | Show connection status |
| `ap` | `wifi ap [on\|off]` | Toggle Access Point mode |

---

#### `wifi scan` - Scan Networks

**Syntax:**
```
wifi scan
```

**Expected Output:**
```text
[WIFI] Scanning...
[WIFI] === Networks Found: 5 ===
  1. BISSO_Factory     -45 dBm  [WPA2]  CH 6
  2. Office_5G         -62 dBm  [WPA2]  CH 36
  3. Guest_Network     -78 dBm  [OPEN]  CH 1
  ...
```

---

#### `wifi connect` - Connect to Network

**Syntax:**
```
wifi connect <ssid> <password>
```

**Usage Example:**
```
wifi connect "BISSO_Factory" "SecurePass123"
```

**Expected Output:**
```text
[WIFI] Connecting to BISSO_Factory...
[WIFI] Connected! IP: 192.168.1.100
```

**Possible Errors:**
| Error | Solution |
|-------|----------|
| `Connection failed` | Check SSID/password, verify network in range |
| `Authentication failed` | Wrong password |
| `No AP found` | Network not in range or hidden |

---

#### `wifi status` - Connection Status

**Syntax:**
```
wifi status
```

**Expected Output:**
```text
[WIFI] === Status ===
Connected:  YES
SSID:       BISSO_Factory
IP:         192.168.1.100
RSSI:       -52 dBm (Good)
Channel:    6
```

---

#### `wifi ap` - Access Point Mode

**Syntax:**
```
wifi ap [on|off]
```

**Usage Example:**
```
wifi ap on
```

**Expected Output:**
```text
[WIFI] Access Point ENABLED
SSID: BISSO_E350_AP
Pass: (configured password)
IP:   192.168.4.1
```

---

### `eth` - Ethernet Management

**Syntax:**
```
eth <subcommand> [args...]
```

**Subcommands:**
| Subcommand | Syntax | Description |
|------------|--------|-------------|
| `status` | `eth status` | Show Ethernet status |
| `on` | `eth on` | Enable Ethernet |
| `off` | `eth off` | Disable Ethernet |
| `dhcp` | `eth dhcp` | Enable DHCP |
| `static` | `eth static <ip> <gw> <mask>` | Set static IP |
| `dns` | `eth dns <server>` | Set DNS server |

---

#### `eth status` - Ethernet Status

**Syntax:**
```
eth status
```

**Expected Output:**
```text
[ETH] === Ethernet Status ===
Link:       UP (100 Mbps Full Duplex)
IP:         192.168.1.50
Gateway:    192.168.1.1
DNS:        8.8.8.8
MAC:        AA:BB:CC:DD:EE:FF
Uptime:     02:45:33
Errors:     0
Reconnects: 0
```

---

### `ping` - Network Connectivity Test

**Syntax:**
```
ping <host> [count]
```

**Description:**
Sends ICMP echo requests to verify network connectivity to a remote host.

**How It Works:**
The ping command uses the ESP-IDF lwIP stack to send ICMP packets. Each packet is timestamped to measure round-trip time. This is essential for diagnosing network issues—if ping fails but WiFi shows connected, check firewall rules or routing.

**Parameters:**
| Parameter | Description | Default |
|-----------|-------------|---------|
| `host` | IP address or hostname | Required |
| `count` | Number of pings | 4 |

**Usage Example:**
```
ping 8.8.8.8
ping 192.168.1.1 10
```

**Expected Output:**
```text
[PING] Pinging 8.8.8.8...
Reply from 8.8.8.8: time=12ms
Reply from 8.8.8.8: time=14ms
Reply from 8.8.8.8: time=11ms
Reply from 8.8.8.8: time=13ms
[PING] 4 sent, 4 received, 0% loss
Average: 12.5ms
```

---

### `ota` - Over-The-Air Updates

**Syntax:**
```
ota <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show OTA update status |
| `cancel` | Cancel pending update |

**How It Works:**
```
OTA (OVER-THE-AIR) UPDATE PROCESS:
┌─────────────────────────────────────────────────────────────────┐
│  1. Upload firmware.bin via Web UI                                 │
│  2. Firmware written to INACTIVE OTA partition                     │
│  3. CRC32 checksum verified                                        │
│  4. Bootloader updated to boot from new partition                  │
│  5. System reboots into new firmware                               │
│  6. If boot fails → automatic rollback to previous                │
│                                                                    │
│  SAFETY:                                                           │
│  • Dual partition scheme prevents bricking                        │
│  • Rollback if new firmware doesn't confirm boot                  │
│  • Progress shown via `ota status`                                │
└─────────────────────────────────────────────────────────────────┘
```

---

### `web_setpass` - Set Web UI Password

**Syntax:**
```
web_setpass <new_password>
```

**Description:**
Changes the web interface authentication password.

> [!CAUTION]
> Password must be at least 8 characters. Common passwords (123456, password, etc.) are rejected!

**Usage Example:**
```
web_setpass "MySecureP@ss123"
```

---

### `auth` - Authentication Diagnostics

**Syntax:**
```
auth
```

**Description:**
Shows authentication manager status and active sessions.

**How It Works:**
```
AUTHENTICATION SYSTEM:
┌─────────────────────────────────────────────────────────────────┐
│  Session Management:                                               │
│  • Web UI uses HTTP Basic Auth + session tokens                   │
│  • Session timeout: 30 minutes (configurable)                     │
│  • Max concurrent sessions: 5                                     │
│                                                                    │
│  Telnet Auth:                                                      │
│  • Same password as web UI                                        │
│  • 3 failed attempts = 5 minute lockout                           │
│                                                                    │
│  Serial Console:                                                   │
│  • No authentication (physical access assumed trusted)            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. 🔬 DIAGNOSTICS & MONITORING

---

### `diag` - System Diagnostic Summary

**Syntax:**
```
diag
```

**Description:**
Quick one-screen summary of critical system status.

**How It Works:**
The `diag` command is a "health dashboard" that aggregates the most critical metrics from all subsystems into one view. It's designed for quick operator checks—if everything shows green/OK, the machine is healthy. Any issues are highlighted for immediate attention.

**Expected Output:**
```text
[DIAG] =========== SYSTEM SUMMARY ===========
Uptime:     04:32:15
Heap:       142 KB free (min: 98 KB)
CPU:        23%
Safety:     OK
Spindle:    15.2 A (peak 18.5 A)
WiFi:       192.168.1.100 (-52 dBm)
Job:        Idle
Faults:     3 total
=============================================
```

---

### `memory` - Heap Memory Diagnostics

**Syntax:**
```
memory <subcommand>
```

**Description:**
Provides detailed ESP32 heap memory statistics.

**How It Works:**
The ESP32 has a complex memory layout with multiple heaps (internal DRAM, IRAM, PSRAM if available). This command queries ESP-IDF's heap allocator to report usage. Key metrics include:
- **Free Heap**: Available memory for new allocations
- **Min Free Ever**: Historical low-water mark (helps identify memory pressure)
- **Largest Block**: Maximum contiguous allocation possible (critical for large buffers)
- **Fragmentation**: How scattered the free memory is (high fragmentation = small blocks scattered)

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `stats` | Show heap statistics |
| `detail` | Detailed fragmentation analysis |
| `reset` | Reset tracking counters |

**Usage Example:**
```
memory stats
```

**Expected Output:**
```text
[MEMORY] === Heap Statistics ===
Total Heap:      320,000 bytes
Free Heap:       145,232 bytes (45.4%)
Min Free Ever:   89,456 bytes
Largest Block:   65,536 bytes
Fragmentation:   23.4%
```

---

### `memleak` - Memory Leak Detection

**Syntax:**
```
memleak [reset]
```

**Description:**
Analyzes memory usage over time to detect potential leaks.

**How It Works:**
This command compares current heap usage against a baseline recorded at startup or last reset. A consistently decreasing free heap over hours of operation suggests a memory leak. Normal operation shows fluctuations but returns to baseline. If you see persistent decline, use `memory detail` to investigate further.

**Usage Example:**
```
memleak
```

**Expected Output:**
```text
[MEMORY] === Memory Leak Analysis ===
Baseline:    152 KB (set 2.5 hours ago)
Current:     145 KB
Change:      -7168 bytes (-4.6%)
All-time min: 98 KB
[MEMORY] No significant leak detected
```

**Reset Baseline:**
```
memleak reset
```

---

### `faults` - Fault Log Management

**Syntax:**
```
faults <subcommand>
```

**Description:**
Manages the "Black Box" fault recorder that logs all machine errors.

**How It Works:**
Every fault condition (E-Stop, soft limit, spindle stall, communication errors) is timestamped and recorded to a persistent log in flash. This log survives reboots, providing a history of what happened and when. Faults are categorized by severity and source, making diagnosis easier.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `show` | Display fault history (the "Black Box") |
| `stats` | Show fault statistics |
| `clear` | Clear fault log |

**Usage Example:**
```
faults show
```

**Expected Output:**
```text
[FAULTS] === Fault History ===
#001 [2026-01-27 14:32:15] ESTOP - Hardware E-Stop triggered
#002 [2026-01-27 14:35:22] SPINDLE_STALL - Current exceeded threshold
#003 [2026-01-27 15:01:44] SOFT_LIMIT - X axis exceeded max travel
[FAULTS] Total: 3 faults recorded
```

---

### `selftest` - Hardware Self-Test

**Syntax:**
```
selftest [quick|full]
```

**Description:**
Runs comprehensive hardware diagnostics checking all sensors, I/O, and communication channels.

**How It Works:**
The self-test systematically probes each hardware component:
1. **I2C Bus**: Scans for expected devices at known addresses
2. **Expanders**: Writes/reads test patterns to verify I/O
3. **Encoders**: Verifies communication and reads current positions
4. **VFD**: Queries Modbus device ID register
5. **Current Sensor**: Reads JXK-10 value and checks range
6. **Storage**: Verifies NVS and LittleFS accessibility

The quick test checks presence; the full test also validates data integrity.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `quick` | Fast test (~5 seconds) |
| `full` | Complete test (~30 seconds) |

**Usage Example:**
```
selftest
```

**Expected Output:**
```text
[SELFTEST] === Hardware Self-Test ===
[PASS] I2C Bus..................OK
[PASS] PCF8574 Input (0x20).....OK
[PASS] PCF8574 Output (0x21)....OK
[PASS] LCD (0x27)...............OK
[PASS] Encoder X................OK (WJ66: 1234567 pulses)
[PASS] Encoder Y................OK (WJ66: 987654 pulses)
[PASS] Encoder Z................OK (WJ66: 456789 pulses)
[PASS] VFD Communication........OK (Altivar31: Ready)
[PASS] JXK-10 Current Sensor....OK (12.5A)
[PASS] NVS Storage..............OK (89/127 entries)
[PASS] LittleFS.................OK (1.2MB/1.5MB)
========================================
[SELFTEST] 11/11 tests passed
```

---

### `telemetry` - System Telemetry Stream

**Syntax:**
```
telemetry <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show telemetry status |
| `stream` | Start real-time streaming |
| `stop` | Stop streaming |

**How It Works:**
```
TELEMETRY STREAMING:
┌─────────────────────────────────────────────────────────────────┐
│  Real-time data broadcast:                                         │
│  • Position: All 4 axes (X, Y, Z, A)                              │
│  • Velocity: Calculated from encoder deltas                       │
│  • Spindle: RPM and current (Amps)                                │
│  • Status: Machine state, alarms, limits                          │
│                                                                    │
│  STREAM FORMAT:                                                    │
│  JSON @ 10Hz for WebSocket clients                                │
│  Compact binary for bandwidth-limited channels                    │
│                                                                    │
│  Used by:                                                          │
│  • Web UI real-time DRO                                           │
│  • External data logging systems                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

### `metrics` - Task Performance Monitor

**Syntax:**
```
metrics <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `summary` | High-level task overview |
| `detail` | Detailed per-task breakdown |
| `reset` | Reset statistics |

**Usage Example:**
```
metrics summary
```

**Expected Output:**
```text
[METRICS] === Task Performance ===
Task Name        CPU%  Stack   High Water
─────────────────────────────────────────
Motion           12%   4096    2048
Encoder          8%    2048    1024
LCD              3%    2048    512
Telemetry        2%    4096    1536
CLI              1%    4096    2048
---─────────────────────────────────────────
Total CPU: 26%
```

**How It Works:**
```
TASK PERFORMANCE MONITORING:
┌─────────────────────────────────────────────────────────────────┐
│  FreeRTOS Instrumentation:                                         │
│  • CPU% = Task run time / total scheduler time                    │
│  • Stack = Allocated stack size (bytes)                           │
│  • High Water = Minimum free stack ever (lower = closer to crash) │
│                                                                    │
│  ALERTS:                                                           │
│  • If High Water < 256 bytes → Stack overflow risk                │
│  • If CPU% > 80% → System may become unresponsive                 │
│  • If any task stalled → Watchdog will trigger reset              │
└─────────────────────────────────────────────────────────────────┘
```

---

### `runtime` - Machine Runtime Counter

**Syntax:**
```
runtime
```

**Description:**
Shows total operating hours, cutting hours, and cycle counts.

**Expected Output:**
```text
[RUNTIME] === Machine Statistics ===
Session Uptime:   04:32:15
Total Runtime:    1,234 hours
Cutting Time:     567 hours (46% efficiency)
Idle Time:        667 hours
Power Cycles:     89
```

**How It Works:**
```
RUNTIME TRACKING:
┌─────────────────────────────────────────────────────────────────┐
│  Session Uptime:                                                   │
│  • Counted from boot via millis()                                 │
│                                                                    │
│  Total Runtime:                                                    │
│  • Saved to NVS every 10 minutes                                  │
│  • Persists across reboots                                        │
│                                                                    │
│  Cutting Time:                                                     │
│  • Accumulated when spindle is running AND motion active          │
│  • Used for blade life estimation                                 │
│                                                                    │
│  Efficiency = Cutting Time / Total Runtime × 100%                  │
└─────────────────────────────────────────────────────────────────┘
```

---

### `test` - System Stress Tests

**Syntax:**
```
test <subcommand>
```

**Description:**
Deliberately stresses system components to identify weak points.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `cpu` | CPU stress test |
| `memory` | Memory allocation stress |
| `i2c` | I2C bus stress |
| `full` | Complete system stress |

> [!CAUTION]
> Stress tests may cause temporary system instability. Do NOT run during cutting operations!

**How It Works:**
```
STRESS TEST METHODOLOGY:
┌─────────────────────────────────────────────────────────────────┐
│  TEST CPU:                                                         │
│  • Runs tight loop with calculations                              │
│  • Measures time to complete fixed iterations                     │
│  • Detects thermal throttling                                     │
│                                                                    │
│  TEST MEMORY:                                                      │
│  • Allocates/frees blocks of increasing size                      │
│  • Tests heap fragmentation resilience                            │
│                                                                    │
│  TEST I2C:                                                         │
│  • Rapid read/write cycles to all I2C devices                     │
│  • Detects intermittent connection issues                         │
│                                                                    │
│  TEST FULL:                                                        │
│  • Runs all tests sequentially                                    │
│  • Reports overall system health score                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 7. 📍 I2C BUS MANAGEMENT

---

### `i2c` - I2C Bus Diagnostics

**Syntax:**
```
i2c <subcommand> [args...]
```

**Description:**
Comprehensive I2C bus management for diagnosing communication issues with peripherals.

**How It Works:**
The ESP32 I2C bus connects to multiple devices on the KinCony KC868-A16 board: input expander (PCF8574 at 0x20), output expander (PCF8574 at 0x21), and LCD display (0x27). The bus runs at 400kHz (Fast Mode). If a device hangs the bus (SCL stuck low), the `recover` command sends 9 clock pulses to reset it.

```text
I2C BUS TOPOLOGY:
┌─────────────────────────────────────────────────────────────────────┐
│  ESP32 (Master) ───┬─── SDA/SCL @400kHz ───────────────────   │
│                    │                                               │
│        ┌───────────┼───────────┐                                  │
│        │           │           │                                  │
│   [0x20 I73]  [0x21 Q73]  [0x27 LCD]                             │
│   8x Inputs   8x Outputs  20x4 Display                          │
└─────────────────────────────────────────────────────────────────────┘
```

**Subcommands:**
| Subcommand | Syntax | Description |
|------------|--------|-------------|
| `scan` | `i2c scan [--timing]` | Scan for devices |
| `test` | `i2c test <address>` | Test specific device |
| `stats` | `i2c stats` | Show bus statistics |
| `recover` | `i2c recover` | Attempt bus recovery |
| `monitor` | `i2c monitor [duration]` | Real-time monitoring |
| `benchmark` | `i2c benchmark` | Speed test |
| `health` | `i2c health` | Health check summary |
| `selftest` | `i2c selftest` | Full device test |
| `troubleshoot` | `i2c troubleshoot` | Diagnostic wizard |

---

#### `i2c scan` - Device Discovery

**Syntax:**
```
i2c scan [--timing] [--json]
```

**Expected Output:**
```text
[I2C] === Bus Scan ===
┌──────────┬────────────────┬─────────────────────────────────┐
│ Address  │ Device         │ Description                     │
├──────────┼────────────────┼─────────────────────────────────┤
│ 0x20     │ I73_INPUT      │ Input Expander (Limit Switches) │
│ 0x21     │ Q73_OUTPUT     │ Output Expander (VFD Control)   │
│ 0x27     │ LCD            │ 20x4 LCD Display                │
└──────────┴────────────────┴─────────────────────────────────┘
[I2C] 3 devices found
```

---

#### `i2c recover` - Bus Recovery

**Syntax:**
```
i2c recover
```

**Description:**
Attempts to recover a stuck I2C bus by sending clock pulses.

**Expected Output:**
```text
[I2C] Attempting bus recovery...
[I2C] Sending 9 clock pulses...
[I2C] Recovery complete - bus status: OK
```

---

## 8. 🔩 HARDWARE PERIPHERALS

---

### `encoder` - Encoder Management

**Syntax:**
```
encoder <subcommand> [args...]
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show encoder status |
| `read` | Read current positions |
| `test` | Test encoder communication |
| `config` | Show/set configuration |
| `protocol` | Show protocol settings |

**How It Works:**
```
WJ66 ENCODER INTERFACE:
┌─────────────────────────────────────────────────────────────────┐
│  RS485 @ 57600 baud, 8N1                                           │
│                                                                    │
│   ESP32 ─── TX/RX ─── WJ66 4-Axis Counter Module               │
│                                                                    │
│  REGISTER MAP:                                                     │
│  0x00-0x03: X-axis count (32-bit signed)                          │
│  0x04-0x07: Y-axis count                                          │
│  0x08-0x0B: Z-axis count                                          │
│  0x0C-0x0F: A-axis count                                          │
│                                                                    │
│  UPDATE RATE: 50Hz (20ms polling interval)                        │
└─────────────────────────────────────────────────────────────────┘
```

---

### `spindle` - Spindle Current Monitor

**Syntax:**
```
spindle <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show current reading and status |
| `alarm` | Alarm management (status/clear/stall) |
| `calibrate` | Calibration mode |

**Usage Example:**
```
spindle status
```

**Expected Output:**
```text
[SPINDLE] === Monitor Status ===
Enabled:      YES
Current:      15.2 A
Peak:         18.5 A
No-Load Base: 8.5 A
Stall Limit:  25.0 A
Status:       NORMAL
```

**How It Works:**
```
SPINDLE CURRENT MONITORING PIPELINE:
┌─────────────────────────────────────────────────────────────────┐
│  JXK-10 Sensor ─── RS485 ─── ESP32 ─── Stall Detection      │
│                                                                    │
│  THRESHOLDS:                                                       │
│  • No-Load Base: Measured during calibration (spindle idle)       │
│  • Stall Limit: Base + configurable margin (default 25A)          │
│  • Peak tracking: Highest current seen in session                 │
│                                                                    │
│  If current > Stall Limit for > 500ms → STALL ALARM triggered    │
└─────────────────────────────────────────────────────────────────┘
```

---

### `jxk10` - JXK-10 Current Sensor

**Syntax:**
```
jxk10 <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `read` | Read current value |
| `info` | Show device info |
| `addr` | Change slave address |
| `status` | Full diagnostics |
| `enable` | Enable sensor |
| `disable` | Disable sensor |

**How It Works:**
```
JXK-10 MODBUS INTERFACE:
┌─────────────────────────────────────────────────────────────────┐
│  Modbus RTU @ 9600 baud, Address 0x02 (configurable)              │
│                                                                    │
│  REGISTERS:                                                        │
│  0x0000: Current reading (A × 10, e.g., 152 = 15.2A)              │
│  0x0001: Device type                                              │
│  0x0002: Slave address                                            │
│                                                                    │
│  COMMANDS:                                                         │
│  • jxk10 addr <new>: Change address (requires power cycle)        │
│  • jxk10 enable/disable: Control polling                          │
└─────────────────────────────────────────────────────────────────┘
```

---

### `lcd` - LCD Display Control

**Syntax:**
```
lcd <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `on` | Enable LCD |
| `off` | Disable LCD |
| `backlight` | Control backlight (on/off) |
| `sleep` | Force display sleep |
| `wakeup` | Wake display |
| `timeout` | Set sleep timeout |
| `reset` | Reset I2C errors |
| `status` | Show LCD status |
| `scan` | Scan for LCD device |
| `test` | Run test pattern |

**Usage Example:**
```
lcd timeout 60
```

**How It Works:**
```
LCD I2C ARCHITECTURE:
┌─────────────────────────────────────────────────────────────────┐
│  20x4 Character LCD @ I2C address 0x27                            │
│                                                                    │
│  DISPLAY ZONES:                                                    │
│  ┌────────────────────┐                                           │
│  │Row 0: Status     │  IDLE/RUNNING/ALARM                         │
│  │Row 1: Position   │  X:100.00 Y:50.00                           │
│  │Row 2: Position   │  Z:25.00  A:0.00                            │
│  │Row 3: Info       │  Spindle RPM, Feed%                         │
│  └────────────────────┘                                           │
│                                                                    │
│  Sleep timeout saves backlight lifespan                           │
└─────────────────────────────────────────────────────────────────┘
```

---

### `dio` - Digital I/O Status

**Syntax:**
```
dio
```

**Description:**
Shows real-time state of all digital inputs and outputs.

**Expected Output:**
```text
[DIO] === Digital I/O Status ===
INPUTS (I73 @ 0x20):
  IN0 [X_LIMIT]: 0  │  IN4 [START_BTN]: 0
  IN1 [Y_LIMIT]: 0  │  IN5 [STOP_BTN]:  0
  IN2 [Z_LIMIT]: 1  │  IN6 [E_STOP]:    0
  IN3 [A_LIMIT]: 0  │  IN7 [VFD_READY]: 1

OUTPUTS (Q73 @ 0x21):
  OUT0 [X_FWD]:  0  │  OUT4 [SPEED_1]:  0
  OUT1 [X_REV]:  0  │  OUT5 [SPEED_2]:  0
  OUT2 [Y_FWD]:  0  │  OUT6 [RUN_SIG]:  0
  OUT3 [Y_REV]:  0  │  OUT7 [SPARE]:    0
```

**How It Works:**
```
DIGITAL I/O HARDWARE:
┌─────────────────────────────────────────────────────────────────┐
│  KinCony KC868-A16 PLC Board                                       │
│                                                                    │
│  I73 INPUT EXPANDER (0x20):                                        │
│  • 8x optocoupled inputs (24VDC compatible)                       │
│  • Directly connected to limit switches, E-stop, VFD ready        │
│  • Polled every 10ms for fast response                            │
│                                                                    │
│  Q73 OUTPUT EXPANDER (0x21):                                       │
│  • 8x relay outputs (5A @ 250VAC rated)                           │
│  • Controls motor direction, speed selection                      │
│  • State changes atomic to prevent glitches                       │
└─────────────────────────────────────────────────────────────────┘
```

---

### `rs485` - RS-485 Device Registry

**Syntax:**
```
rs485 <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show all registered devices |
| `diag` | Full diagnostics |
| `raw` | Send raw hex data |
| `reset` | Reset device registry |

**Usage Example:**
```
rs485 status
```

**Expected Output:**
```text
[RS485] === Device Registry ===
Device          Address  Priority  Status    Success%
───────────────────────────────────────────────────────
WJ66_ENCODER    0x01     HIGH      ONLINE    99.8%
JXK10_CURRENT   0x02     NORMAL    ONLINE    98.5%
ALTIVAR31_VFD   0x03     NORMAL    ONLINE    99.2%
```

**How It Works:**
```
RS485 DEVICE REGISTRY:
┌─────────────────────────────────────────────────────────────────┐
│  Priority-Based Scheduling:                                        │
│                                                                    │
│  HIGH Priority (50Hz):                                            │
│  • WJ66 Encoder - Position feedback (most critical)               │
│                                                                    │
│  NORMAL Priority (10Hz):                                          │
│  • JXK-10 Current Sensor                                          │
│  • Altivar31 VFD                                                   │
│                                                                    │
│  Bus Sharing:                                                      │
│  • Single RS485 bus shared by all devices                         │
│  • Scheduler alternates between devices by priority               │
│  • Retries with exponential backoff on timeout                    │
└─────────────────────────────────────────────────────────────────┘
```

---

## 9. 🗂️ FILESYSTEM & LOGGING

---

### `ls` - List Files

**Syntax:**
```
ls [path]
```

**Usage Examples:**
```
ls
ls /data
ls /logs
```

**Expected Output:**
```text
Listing directory: /
  [DIR]  data
  [DIR]  logs
  [DIR]  pages
  [FILE] config.json            4096 bytes
  [FILE] boot.log               1234 bytes
```

**How It Works:**
```
LITTLEFS FILESYSTEM:
┌─────────────────────────────────────────────────────────────────┐
│  1.5MB Partition on ESP32 SPI Flash                                │
│                                                                    │
│  /                                                                 │
│  ├── /data/          (Job files, fault logs)                      │
│  ├── /logs/          (System logs)                                 │
│  ├── /pages/         (Web UI HTML/JS/CSS)                          │
│  └── config.json     (Runtime config cache)                       │
│                                                                    │
│  Power-fail safe with wear leveling                               │
└─────────────────────────────────────────────────────────────────┘
```

---

### `df` - Disk Free Space

**Syntax:**
```
df
```

**Expected Output:**
```text
LittleFS Partition Status:
  Total:  1572864 bytes
  Used:   1234567 bytes (78%)
  Free:    338297 bytes
```

---

### `cat` - View File Contents

**Syntax:**
```
cat <filename>
```

**Usage Example:**
```
cat /data/fault_log.txt
```

**Expected Output:**
```text
--- /data/fault_log.txt START ---
[2026-01-27 14:32:15] ESTOP: Hardware E-Stop triggered
[2026-01-27 14:35:22] SPINDLE_STALL: Current exceeded 25A
--- /data/fault_log.txt END ---
```

---

### `log` - Log Management

**Syntax:**
```
log <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `boot` | Show boot log |
| `enable` | Enable logging |
| `delete` | Delete log files |

**How It Works:**
```
LOGGING ARCHITECTURE:
┌─────────────────────────────────────────────────────────────────┐
│  LOG DESTINATIONS:                                                 │
│  • Serial (always, for debugging)                                 │
│  • Telnet (if connected)                                          │
│  • LittleFS file (persistent, if enabled)                         │
│                                                                    │
│  LOG LEVELS:                                                       │
│  DEBUG < INFO < WARNING < ERROR < FATAL                           │
│                                                                    │
│  BOOT LOG:                                                         │
│  • Captures startup sequence and any errors                       │
│  • Essential for post-mortem debugging                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 10. 🎯 CALIBRATION & SETUP

---

### `calibrate` / `calib` - Calibration Commands

**Syntax:**
```
calibrate <subcommand> [args...]
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `ppmm` | Pulses-per-mm calibration |
| `speed` | Speed profile calibration |
| `vfd` | VFD current baseline calibration |

---

#### `calibrate ppmm` - Encoder Calibration

**Syntax:**
```
calibrate ppmm <axis> start
calibrate ppmm <axis> end <distance_mm>
calibrate ppmm <axis> reset
```

**Description:**
Calibrates the encoder scale by measuring actual travel distance.

**Calibration Procedure:**
```
PPMM CALIBRATION WORKFLOW:
┌─────────────────────────────────────────────────────────────────┐
│  1. Position axis at a known starting point                     │
│  2. calibrate ppmm X start       ← Records starting pulse       │
│  3. Physically move axis a known distance (e.g., 1000mm)        │
│  4. calibrate ppmm X end 1000    ← Calculates PPM               │
│  5. config save                  ← Save to flash                │
└─────────────────────────────────────────────────────────────────┘
```

**Usage Example:**
```
calibrate ppmm X start
# Move axis 1000mm
calibrate ppmm X end 1000
```

**Expected Output:**
```text
[CALIB] X-axis calibration started. Initial count: 0
...
[CALIB] Distance: 1000.000 mm
[CALIB] Pulses: 200,000
[CALIB] Calculated PPM: 200.000
[CALIB] Updated $100 = 200.000
```

---

#### `calibrate speed` - Speed Calibration

**Syntax:**
```
calibrate speed <axis> <distance_mm>
```

**Description:**
Automatically measures actual axis speed for accurate ETA calculations.

---

### `vfd` - VFD Management

**Syntax:**
```
vfd <subcommand>
```

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `diag` | VFD diagnostics |
| `config` | VFD configuration |
| `calibrate` | Current baseline calibration |

**How It Works:**
```
VFD (VARIABLE FREQUENCY DRIVE) CONTROL:
┌─────────────────────────────────────────────────────────────────┐
│  Schneider Altivar31 VFD                                           │
│  • Modbus RTU @ 19200 baud                                         │
│  • Controls spindle motor (up to 15HP)                             │
│                                                                    │
│  COMMANDS:                                                         │
│  • Start/Stop via M3/M5 G-codes                                   │
│  • Speed set via S parameter (RPM)                                │
│  • Fault read and clear via Modbus                                │
│                                                                    │
│  CALIBRATION:                                                      │
│  • Measures no-load current as baseline                           │
│  • Used for stall detection and blade efficiency                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 11. 📋 JOB MANAGEMENT

---

### `job_start` - Start G-Code Job

**Syntax:**
```
job_start <filename>
```

**Usage Example:**
```
job_start /data/kitchen_top.nc
```

---

### `job_abort` - Abort Current Job

**Syntax:**
```
job_abort
```

---

### `job_status` - Job Status

**Syntax:**
```
job_status
```

**Expected Output:**
```text
Job: kitchen_top.nc
State: 1 (RUNNING)
Line: 1234
```

---

### `job_eta` - Job Progress & ETA

**Syntax:**
```
job_eta
```

**Expected Output:**
```text
[JOB] === Job Progress ===
File:      kitchen_top.nc
Progress:  1234 / 5000 lines (24.7%)
Elapsed:   345 sec
ETA:       15 min 23 sec
           [####----------------]
```

**How It Works:**
```
JOB ETA CALCULATION:
┌─────────────────────────────────────────────────────────────────┐
│  ETA Algorithm:                                                    │
│                                                                    │
│  1. Pre-parse file to count total lines                           │
│  2. Track lines_processed / total_lines = progress %              │
│  3. Measure elapsed_time / lines_processed = time_per_line        │
│  4. remaining_lines × time_per_line = ETA                         │
│                                                                    │
│  ACCURACY FACTORS:                                                 │
│  • Feed override changes affect ETA dynamically                   │
│  • Dwells (G4) add fixed time                                     │
│  • Complex moves take longer than simple rapid moves              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 12. 🔐 SECURITY COMMANDS

---

### `web_setpass` - Set Web Password

**Syntax:**
```
web_setpass <password>
```

**Requirements:**
- Minimum 8 characters
- Not a common password (123456, password, etc.)

---

### `ota_setpass` - Set OTA Password

**Syntax:**
```
ota_setpass <password>
```

---

## 13. 🛠️ ADVANCED DIAGNOSTICS

---

### `web` - Web Server Configuration

**Syntax:**
```
web config <subcommand>
```

**Description:**
Manages web server credentials and configuration.

| `username <name>` | Set web UI username (3-32 chars) |
| `password <pass>` | Set web UI password (4-64 chars) |

**Usage Examples:**
```
web config show           # Show current credentials
web config username admin # Set username
web config password MySecurePass123
```

**How It Works:**
```
CREDENTIAL STORAGE ARCHITECTURE:
┌─────────────────────────────────────────────────────────────┐
│                          NVS Flash                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────┐  │
│  │ KEY_WEB_USERNAME│  │ KEY_WEB_PASSWORD│  │KEY_WEB_PW   │  │
│  │    "admin"      │  │   (plaintext)   │  │  _CHANGED   │  │
│  └────────┬────────┘  └────────┬────────┘  └──────┬──────┘  │
│           │                    │                  │         │
└───────────┼────────────────────┼──────────────────┼─────────┘
            └────────────────────┼──────────────────┘
                                 ↓
              ┌─────────────────────────────────────┐
              │     WebServer.loadCredentials()     │
              │   Called on boot and after changes  │
              └─────────────────────────────────────┘
```

> [!WARNING]
> Passwords are stored in plaintext in NVS. Physical access to the device allows extraction.

---

### `api` - API Rate Limiter Diagnostics

**Syntax:**
```
api <subcommand>
```

**Description:**
Monitors and manages the API rate limiting system that prevents DoS attacks.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `diag` | Show rate limiter diagnostics |
| `reset` | Reset all rate limit counters |

**Usage Example:**
```
api diag
```

**Expected Output:**
```text
[API] === Rate Limiter Diagnostics ===
Requests in window: 45
Window size: 60 seconds
Limit per client: 100 req/min
Blocked requests: 0
```

**How It Works:**
```
SLIDING WINDOW RATE LIMITER:
┌────────────────────────────────────────────────────────────────┐
│                     60-Second Window                           │
│  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐   │
│  │5 │3 │7 │2 │4 │6 │1 │8 │3 │2 │4 │1 │5 │2 │3 │4 │2 │1 │3 │   │
│  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘   │
│  ←────────────────────────────────────────────────────────→   │
│           Window slides with time (FIFO queue)                │
│                                                                │
│   Total requests in window = 66                                │
│   Limit = 100 req/window                                       │
│   Status = ALLOWED (34 remaining)                              │
└────────────────────────────────────────────────────────────────┘

DOS PROTECTION:
  • Per-IP tracking with HashMap
  • Burst allowance for legitimate batch operations
  • Auto-cleanup of stale entries every 5 minutes
```

---

### `axis` - Per-Axis Motion Quality Diagnostics

**Syntax:**
```
axis <subcommand> [axis]
```

**Description:**
Monitors motion quality metrics for each axis, useful for detecting mechanical issues.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show all axes quality summary |
| `detail <X\|Y\|Z>` | Show detailed diagnostics for axis |
| `reset <X\|Y\|Z\|all>` | Reset quality metrics |

**Usage Examples:**
```
axis status          # Overview of all axes
axis detail X        # Detailed X-axis diagnostics
axis reset all       # Clear all metrics
```

**Expected Output:**
```text
[AXIS] === Motion Quality Status (All Axes) ===
Axis  Moves  Errors  Deviation  Status
──────────────────────────────────────────
X     1234     0      0.01mm    GOOD
Y      987     2      0.03mm    GOOD
Z      456     0      0.02mm    GOOD
```

**How It Works:**
```
MOTION QUALITY METRICS COLLECTION:
┌─────────────────────────────────────────────────────────────────┐
│                     AXIS SYNCHRONIZATION MODULE                  │
│                                                                  │
│   For each move command:                                         │
│   ┌──────────────────────────────────────────────────────────┐  │
│   │  1. Record TARGET position from G-code                   │  │
│   │  2. Execute move via PLC                                 │  │
│   │  3. Wait for ACTUAL position from encoder                │  │
│   │  4. Calculate DEVIATION = |Target - Actual|              │  │
│   │  5. Update running statistics:                           │  │
│   │     • Total moves count                                  │  │
│   │     • Error count (deviation > threshold)                │  │
│   │     • Average/Max deviation                              │  │
│   └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│   STATUS CLASSIFICATION:                                         │
│   • GOOD:    Deviation < 0.05mm, Error rate < 1%                 │
│   • WARNING: Deviation < 0.1mm, Error rate < 5%                  │
│   • BAD:     Deviation ≥ 0.1mm or Error rate ≥ 5%                │
└─────────────────────────────────────────────────────────────────┘
```

---

### `debug` - System Debug Utilities

**Syntax:**
```
debug <subcommand>
```

**Description:**
Advanced debugging commands for system internals.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `encoders` | Show encoder debug info |
| `config` | Show configuration debug info |
| `all` | Full system dump |

**Usage Example:**
```
debug all
```

**How It Works:**
```
DEBUG ALL - COMPLETE SYSTEM DUMP:
┌────────────────────────────────────────────────────────────────┐
│  1. FIRMWARE INFO       │ Version, uptime, build date         │
├──────────────────────────┼─────────────────────────────────────┤
│  2. ENCODER STATUS      │ WJ66 diagnostics, pulse counts      │
├──────────────────────────┼─────────────────────────────────────┤
│  3. MOTION STATE        │ Current position, motion buffer     │
├──────────────────────────┼─────────────────────────────────────┤
│  4. SAFETY STATUS       │ E-stop, limits, alarms              │
├──────────────────────────┼─────────────────────────────────────┤
│  5. ELBO VFD STATE      │ VFD communication, parameters       │
├──────────────────────────┼─────────────────────────────────────┤
│  6. CONFIG CACHE        │ Configuration table integrity       │
├──────────────────────────┼─────────────────────────────────────┤
│  7. WATCHDOG STATUS     │ Last feed time, timeout count       │
├──────────────────────────┼─────────────────────────────────────┤
│  8. TASK STATS          │ FreeRTOS task health                 │
└────────────────────────────────────────────────────────────────┘
```

---

### `timeouts` - Timeout Diagnostics

**Syntax:**
```
timeouts
```

**Description:**
Shows diagnostic information about communication timeouts across all subsystems.

**How It Works:**
```
TIMEOUT TRACKING:
┌─────────────────────────────────────────────────────────────┐
│  Subsystem          Last OK      Timeouts   State          │
├─────────────────────────────────────────────────────────────┤
│  RS485 (JXK-10)     2.1s ago     3          OK             │
│  RS485 (WJ66)       0.5s ago     0          OK             │
│  I2C (PCF8574)      0.1s ago     1          OK             │
│  WiFi               5.2s ago     0          OK             │
│  Modbus (VFD)       1.8s ago     12         WARNING        │
└─────────────────────────────────────────────────────────────┘

Timeout thresholds configured per-subsystem for optimal
balance between responsiveness and false-positive avoidance.
```

---

### `wdt` - Watchdog Management

**Syntax:**
```
wdt <subcommand>
```

**Description:**
Manages the system watchdog timer that prevents system hangs.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show watchdog status |
| `feed` | Manually feed the watchdog |
| `stats` | Show watchdog statistics |

**How It Works:**
```
WATCHDOG TIMER ARCHITECTURE:
┌─────────────────────────────────────────────────────────────────┐
│                     ESP32 TASK WATCHDOG                          │
│                                                                  │
│   ┌─────────────┐     Feed every      ┌─────────────────────┐   │
│   │  Main Loop  │ ─────5 seconds ────→│   WDT Counter      │   │
│   │   Task      │                     │   (resets to 0)     │   │
│   └─────────────┘                     └──────────┬──────────┘   │
│                                                  │              │
│   If counter reaches timeout (default 30s):     │              │
│                                                  ↓              │
│                              ┌─────────────────────────────┐    │
│                              │   SYSTEM RESET + PANIC LOG  │    │
│                              │   (saved to NVS for review) │    │
│                              └─────────────────────────────┘    │
│                                                                  │
│   STATS TRACKED:                                                 │
│   • Last feed timestamp                                          │
│   • Total resets caused by WDT                                   │
│   • Longest time between feeds                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

### `task` - Task Monitoring

**Syntax:**
```
task <subcommand>
```

**Description:**
Monitors FreeRTOS task status and performance.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `list` | Show all running tasks |
| `stats` | Show task statistics |

**How It Works:**
```
FREERTOS TASK MONITORING:
┌─────────────────────────────────────────────────────────────────┐
│                      TASK REGISTRY                               │
│                                                                  │
│   Each registered task is tracked via task_stats_t:              │
│   ┌──────────────────────────────────────────────────────────┐  │
│   │  • TaskHandle_t handle     - FreeRTOS task handle        │  │
│   │  • const char* name        - Human-readable name         │  │
│   │  • UBaseType_t priority    - Task priority (0-24)        │  │
│   │  • uint32_t stack_high_water - Minimum free stack ever   │  │
│   │  • uint32_t run_count      - Times task has executed     │  │
│   │  • uint32_t total_time_ms  - Cumulative execution time   │  │
│   │  • uint32_t max_run_time_ms - Longest single execution   │  │
│   └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│   Stats updated by perfMonitor via hardware timer interrupts     │
└─────────────────────────────────────────────────────────────────┘
```

---

### `encoder_deviation` - Encoder Deviation Diagnostics

**Syntax:**
```
encoder_deviation
```

**Description:**
Analyzes encoder position deviation from expected values. High deviation indicates mechanical slip, encoder errors, or calibration issues.

**Expected Output:**
```text
[ENCODER DEVIATION] === Diagnostics ===
Axis  Expected  Actual    Deviation
──────────────────────────────────────
X     1234567   1234560   7 counts
Y     987654    987650    4 counts
Z     456789    456790    1 count
```

**How It Works:**
```
DEVIATION DETECTION PIPELINE:
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   1. COMMANDED POSITION (from G-code parser)                     │
│      └── Target calculated from G0/G1 + WCS offset               │
│                          │                                       │
│   2. EXPECTED COUNTS     │                                       │
│      └── Target × $100 (pulses/mm) = expected encoder value      │
│                          │                                       │
│   3. ACTUAL COUNTS       ↓                                       │
│      └── WJ66 encoder reading after move complete                │
│                          │                                       │
│   4. DEVIATION = |Expected - Actual|                             │
│                          │                                       │
│   5. ALERT THRESHOLDS:   ↓                                       │
│      • < 10 counts: Normal (mechanical backlash)                 │
│      • 10-50 counts: Warning (check coupling/belt)               │
│      • > 50 counts: Error (slip or encoder fault)                │
└─────────────────────────────────────────────────────────────────┘
```

---

### `fault_recovery` - Fault Recovery Status

**Syntax:**
```
fault_recovery
```

**Description:**
Shows status of all fault recovery mechanisms and auto-recovery attempts.

**How It Works:**
```
FAULT RECOVERY STATE MACHINE:
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   FAULT DETECTED ─────→ RECOVERY ATTEMPT ─────→ SUCCESS/FAIL    │
│         │                     │                      │          │
│         ↓                     ↓                      ↓          │
│   ┌──────────┐         ┌───────────────┐      ┌─────────────┐   │
│   │ Log fault│         │ Execute       │      │ Clear fault │   │
│   │ to NVS   │         │ recovery      │      │ OR escalate │   │
│   │          │         │ procedure     │      │ to alarm    │   │
│   └──────────┘         └───────────────┘      └─────────────┘   │
│                                                                  │
│   RECOVERY PROCEDURES:                                           │
│   • RS485 timeout → Retry with exponential backoff               │
│   • Encoder loss  → Re-initialize WJ66, re-home if needed        │
│   • I2C error     → Bus reset, re-enumerate devices              │
│   • VFD fault     → Clear fault register, restart sequence       │
│                                                                  │
│   MAX RETRIES: 3 per fault type before escalating to ALARM      │
└─────────────────────────────────────────────────────────────────┘
```

---

### `task_list` - Detailed Task List

**Syntax:**
```
task_list
```

**Description:**
Shows detailed FreeRTOS task information including priority, stack usage, and timing.

**Expected Output:**
```text
[TASK] === Detailed Task List ===
Task Name          | Priority | Stack HWM | Runs    | Time(ms)  | Max(ms)
-------------------|----------|-----------|---------|-----------|--------
Motion             |        5 |      2048 |   12345 |     15678 |    125
Encoder            |        6 |      1024 |   98765 |      5432 |     12
LCD                |        2 |       512 |    4567 |      1234 |     45
CLI                |        3 |      2048 |     890 |      2345 |     89
```

**How It Works:**
```
STACK HIGH WATER MARK (HWM) INTERPRETATION:
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   STACK ALLOCATION:                                              │
│   ┌────────────────────────────────────────────────────────────┐│
│   │████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░││
│   │← Used (grows →)    │         Free (HWM) →              │  ││
│   └────────────────────────────────────────────────────────────┘│
│                                                                  │
│   • HWM = Minimum free bytes EVER during task lifetime          │
│   • If HWM approaches 0 → Stack overflow risk!                  │
│   • Recommended: Keep HWM > 25% of allocated stack              │
│                                                                  │
│   TIMING METRICS:                                                │
│   • Time(ms) = Total CPU time consumed by task                   │
│   • Max(ms) = Longest single execution (detect blocking)        │
│   • If Max >> Average → Task may have blocking calls            │
└─────────────────────────────────────────────────────────────────┘
```

---

### `cutting` - Stone Cutting Analytics

**Syntax:**
```
cutting <subcommand> [args...]
```

**Description:**
Manages cutting session analytics including specific cutting energy (SCE), blade wear estimation, and production statistics.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `diag` | Show cutting diagnostics (default) |
| `start` | Start a cutting session |
| `stop` | Stop the current session |
| `reset` | Reset cutting statistics |
| `depth <mm>` | Set cutting depth |
| `blade <mm>` | Set blade width |
| `baseline <sce>` | Set SCE baseline value |

**Usage Examples:**
```
cutting                   # Show current diagnostics
cutting start             # Begin tracking session
cutting depth 30          # Set 30mm cutting depth
cutting blade 4.5         # Set blade width
cutting stop              # End session
```

**Expected Output:**
```text
[CUTTING] === Session Diagnostics ===
Session Active:   YES
Duration:         00:45:32
Linear Distance:  12.5 m
Area Cut:         0.375 m²
Current SCE:      42.5 kWh/m³
Blade Efficiency: 94%
```

**How It Works:**
```
SPECIFIC CUTTING ENERGY (SCE) CALCULATION:
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   SCE = Energy Consumed / Volume Removed                         │
│                                                                  │
│   Where:                                                         │
│   ┌──────────────────────────────────────────────────────────┐  │
│   │  Energy (kWh) = ∫ Power(t) dt                            │  │
│   │                 └── From VFD current × voltage readings  │  │
│   │                                                          │  │
│   │  Volume (m³) = Linear Distance × Blade Width × Depth     │  │
│   │                └── From encoder travel during G1 moves   │  │
│   └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│   BLADE EFFICIENCY:                                              │
│   Efficiency % = (Baseline SCE / Current SCE) × 100              │
│                                                                  │
│   • Baseline = SCE with new blade (set via `cutting baseline`)   │
│   • As blade wears → SCE increases → Efficiency decreases        │
│   • Alert when efficiency < 70% (blade change recommended)       │
│                                                                  │
│   DATA SOURCES:                                                  │
│   ┌────────────┐    ┌────────────┐    ┌────────────┐            │
│   │  JXK-10    │    │   WJ66     │    │  Session   │            │
│   │  Current   │──→ │  Distance  │──→ │  Analytics │            │
│   │  Sensor    │    │  Encoders  │    │  Module    │            │
│   └────────────┘    └────────────┘    └────────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 12. 📟 HARDWARE COMMANDS

Direct control and diagnostics for hardware peripherals.

---

### `encoder` - Encoder Management

**Syntax:**
```
encoder <subcommand> [args]
```

**Description:**
Manages the WJ66 Absolute Encoder interface, providing status monitoring, configuration, and integration diagnostics.

**Subcommands:**
| Subcommand | Description |
|------------|-------------|
| `status` | Show comprehensive encoder dashboard (positions, health, integration) |
| `config` | Show current hardware configuration (pins, baud, protocol) |
| `protocol <0/1>` | Set protocol mode (0=ASCII, 1=Modbus RTU) |
| `zero <axis>` | Zero specific axis (Warning: Affects machine coordinates) |

---

#### `encoder status` - Dashboard

**Syntax:**
```
encoder status
```

**Description:**
Displays a real-time table of all 4 axes, showing raw pulse counts, calibrated millimeter positions, and bus health metrics. It also displays the "Encoder Integration" status, which monitors closed-loop performance.

**Expected Output:**
```text
[ENCODER] === Configuration & Status Dashboard ===
+-----------------+---------------------------------------+
| Interface       | RS485                                 |
| Pins            | RX:16 TX:13                           |
| Baud Rate       | 9600                                  |
| Protocol        | ASCII (#XX\r)                         |
| Address         | 1                                     |
+-----------------+---------------------------------------+
| Axis | Name | Pos (Pulse)|  Pos (mm)  | Status | Age (ms)  | Reads | Missed |
+------+------+------------+------------+--------+-----------+-------+--------+
|  0   |  X   |       5432 |     33.950 |  OK    |        50 |   54K |      0 |
|  1   |  Y   |   12000000 |  12000.000 |  OK    |        51 |   12M |     1K |
|  2   |  Z   |       -810 |     -0.810 |  OK    |        48 |   54K |      0 |
|  3   |  A   |          0 |      0.000 | STALE  |      1500 |   54K |      5 |
+------+------+------------+------------+--------+-----------+-------+--------+

=== ENCODER INTEGRATION ===
Feedback: [ON]
Threshold: 100.0 mm
Axis 0: Err=0.0 mm | State=[OK]
Axis 1: Err=1.2 mm | State=[OK]
Axis 2: Err=0.0 mm | State=[OK]
Axis 3: Err=0.0 mm | State=[OK]
ok
```

**Column Definitions:**
- **Axis**: Internal index (0-3).
- **Name**: Axis label (X, Y, Z, A).
- **Pos (Pulse)**: Raw signed integer value from the encoder.
- **Pos (mm)**: Calibrated position (`Pulses / Pulses_per_mm`).
- **Status**:
    - `OK`: Fresh data received recently (< timeout).
    - `STALE`: No data received within timeout (check wiring/power).
- **Age (ms)**: Time elapsed since last valid packet.
- **Reads**: Total valid packets received since boot.
- **Missed**: Total failed poll attempts (`Total Polls - Successful Reads`). High numbers indicate RS485 noise or collision.

**Encoder Integration (Closed-Loop Monitor):**
The integration section reveals how the system monitors the 3-phase motors:
- **Feedback**: `[ON]` means the system is actively comparing the Motion Planner's "Target" against the Encoder's "Actual".
- **Threshold**: The maximum allowed following error (mm) before a fault is triggered.
- **Axis Err**: The real-time difference (`Actual - Target`).
    - **Concept**: Since induction motors have slip and no inherent step counting, this acts as a servo-like following error monitor. If the motor jams or stalls, "Actual" falls behind "Target", "Err" spikes, and the system E-Stops to prevent damage.

```
CLOSED LOOP MONITORING LOGIC:
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│  1. TARGET GENERATOR (Motion Planner)                            │
│     Calculates where the axis SHOULD be right now (e.g., 100mm)  │
│             │                                                    │
│             ▼                                                    │
│     [ COMPARATOR ] ◄──── Threshold Limit (e.g., 5mm)             │
│             ▲                                                    │
│             │                                                    │
│  2. ACTUAL SENSOR (WJ66 Optical Encoder)                         │
│     Reads where the motor ACTUALLY is (e.g., 99mm)               │
│                                                                  │
│  RESULT: Error = 1mm (OK)                                        │
│  IF Error > Threshold ──→ TRIGGER ALARM (Stall Detect)           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📊 COMPLETE COMMAND QUICK REFERENCE

### System Commands
| Command | Description |
|---------|-------------|
| `help` | Show all commands |
| `info` | System information |
| `status` | Quick dashboard |
| `reboot` | Restart system |
| `echo` | Terminal echo control |

### Grbl Commands
| Command | Description |
|---------|-------------|
| `$` | Show settings |
| `$H` | Home machine |
| `$G` | Parser state |
| `?` | Status report |
| `!` | Feed hold |
| `~` | Resume |

### Configuration
| Command | Description |
|---------|-------------|
| `config` | Configuration management |
| `nvs` | NVS storage inspector |

### Motion Control
| Command | Description |
|---------|-------------|
| `stop` | Stop motion |
| `pause` | Pause motion |
| `resume` | Resume motion |
| `estop` | E-Stop management |
| `limit` | Soft limits |
| `feed` | Feed override |
| `predict` | Position prediction |
| `spinlock` | Timing diagnostics |

### Network
| Command | Description |
|---------|-------------|
| `wifi` | WiFi management |
| `eth` | Ethernet management |
| `ping` | Network test |
| `ota` | OTA updates |

### Diagnostics
| Command | Description |
|---------|-------------|
| `diag` | System summary |
| `memory` | Heap diagnostics |
| `memleak` | Leak detection |
| `faults` | Fault log |
| `selftest` | Hardware test |
| `telemetry` | System telemetry |
| `metrics` | Task performance |
| `runtime` | Uptime counter |
| `test` | Stress tests |

### Advanced Diagnostics
| Command | Description |
|---------|-------------|
| `web` | Web server config |
| `api` | API rate limiter |
| `axis` | Motion quality metrics |
| `debug` | System debug utilities |
| `timeouts` | Timeout diagnostics |
| `wdt` | Watchdog management |
| `task` | Task monitoring |
| `encoder_deviation` | Encoder deviation |
| `fault_recovery` | Fault recovery status |
| `task_list` | Detailed task list |
| `cutting` | Cutting analytics |

### Hardware
| Command | Description |
|---------|-------------|
| `i2c` | I2C bus management |
| `encoder` | Encoder management |
| `spindle` | Spindle monitor |
| `jxk10` | Current sensor |
| `lcd` | LCD control |
| `dio` | Digital I/O |
| `rs485` | RS-485 registry |

### Filesystem
| Command | Description |
|---------|-------------|
| `ls` | List files |
| `df` | Disk space |
| `cat` | View file |
| `log` | Log management |

### Calibration
| Command | Description |
|---------|-------------|
| `calibrate` | Calibration commands |
| `vfd` | VFD management |

### Jobs
| Command | Description |
|---------|-------------|
| `job_start` | Start job |
| `job_abort` | Abort job |
| `job_status` | Job status |
| `job_eta` | Progress/ETA |

---

**Document Version:** 2.1 Ultimate Master  
**Last Updated:** 2026-01-28  
**Firmware Compatibility:** v3.5.x+  
**Total Commands Documented:** 81+  
**Author:** Antigravity (DeepMind Advanced Agentic Coding)  
**Machine:** BISSO E350 PosiPro 4-Axis CNC Bridge Saw

---

> [!TIP]
> For G-code and motion commands, see [GCODE_REFERENCE.md](GCODE_REFERENCE.md)

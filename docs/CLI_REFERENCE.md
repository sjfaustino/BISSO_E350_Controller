# BISSO E350 Controller - Command Line Interface Manual

**Version:** 1.0.0 (Gemini)  
**Last Updated:** 2025-12-31  
**Connection:** Serial (115200 baud) or Telnet (Port 23)

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Motion Control Commands](#motion-control-commands)
3. [Network Commands](#network-commands)
4. [Job Management Commands](#job-management-commands)
5. [Diagnostics Commands](#diagnostics-commands)
6. [Configuration Commands](#configuration-commands)
7. [Calibration Commands](#calibration-commands)
8. [I2C Bus Commands](#i2c-bus-commands)
9. [System Commands](#system-commands)
10. [G-Code / Grbl Commands](#g-code--grbl-commands)
11. [Error Reference](#error-reference)

---

## Getting Started

### Connecting via Serial
1. Connect USB cable to ESP32 USB port
2. Open terminal (PuTTY, Arduino Serial Monitor, etc.)
3. Settings: **115200 baud, 8N1**
4. Press Enter to see prompt

### Connecting via Telnet
```
telnet 192.168.4.1 23
```
**Default Credentials:**
- Username: `admin`
- Password: `password` (change immediately!)

### Getting Help
```
help                    # List all available commands
<command> help          # Show help for specific command
```

---

## Motion Control Commands

### `status` - Show Motion Status

**Description:** Display current position, state, and axis status for all axes.

**Syntax:**
```
status
```

**Example Output:**
```
[MOTION] === Motion System Status ===
  State:     IDLE
  E-Stop:    INACTIVE
  Position:
    X:  125.50 mm (steps: 25100)
    Y:   75.25 mm (steps: 15050)
    Z:   50.00 mm (steps: 10000)
    A:    0.00 deg
  Feed Override: 100%
  Moving: NO
```

**Possible Errors:**
- None (always succeeds)

---

### `stop` - Stop All Motion

**Description:** Immediately stop all axis movement. Does NOT trigger E-Stop.

**Syntax:**
```
stop
```

**Example Output:**
```
[MOTION] Stop command sent
```

---

### `pause` - Pause Motion

**Description:** Pause current motion and G-code execution. Can be resumed.

**Syntax:**
```
pause
```

**Example Output:**
```
[MOTION] Pause command sent
```

---

### `resume` - Resume Motion

**Description:** Resume paused motion and G-code execution.

**Syntax:**
```
resume
```

**Example Output:**
```
[MOTION] Resume command sent
```

---

### `estop` - Emergency Stop Management

**Description:** View status, trigger, or clear emergency stop.

**Syntax:**
```
estop                   # Show E-Stop status
estop status            # Show E-Stop status
estop on                # Trigger E-Stop (DANGEROUS)
estop off               # Clear E-Stop (if safe)
```

**Example Output (status):**
```
[MOTION] [OK] System Enabled
```

**Example Output (E-Stop active):**
```
[MOTION] EMERGENCY STOP ACTIVE
```

**Example Output (clear E-Stop):**
```
[MOTION] [OK] E-Stop Cleared
```

**Possible Errors:**
```
[MOTION] Could not clear E-Stop (Check Safety Alarms)
```
**Fix:** Check physical E-Stop button, safety interlocks, and alarm condition.

---

### `limit` - Set Soft Limits

**Description:** Configure software travel limits for an axis.

**Syntax:**
```
limit <axis> <min> <max> [enable]
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| axis | X/Y/Z/A | Axis to configure |
| min | integer | Minimum position in mm |
| max | integer | Maximum position in mm |
| enable | 0/1 | Optional: Enable (1) or disable (0) limits |

**Examples:**
```
limit X 0 1000              # Set X limits: 0-1000mm
limit Y -50 500 1           # Set Y limits and enable
limit Z 0 150               # Set Z limits: 0-150mm
```

**Example Output:**
```
[MOTION] Soft limits updated for Axis 0
```

**Possible Errors:**
```
[MOTION] Invalid axis
```
**Fix:** Use X, Y, Z, or A for axis parameter.

---

### `feed` - Set Feed Override

**Description:** Adjust feed rate override (speed multiplier).

**Syntax:**
```
feed                    # Show current override
feed <factor>           # Set new override
```

**Parameters:**
| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| factor | float | 0.1-2.0 | Speed multiplier (or 10-200 as %) |

**Examples:**
```
feed                    # Show current: "Current Feed: 100%"
feed 0.5                # Set to 50%
feed 150                # Set to 150% (interpreted as %)
feed 1.0                # Set to 100%
```

**Example Output:**
```
[CLI] Feed override set to 1.50
```

---

### `spinlock` - Spinlock Timing Diagnostics

**Description:** Audit critical section timing to identify performance issues.

**Syntax:**
```
spinlock                # Show help
spinlock stats          # Show timing statistics
spinlock reset          # Reset statistics
```

**Example Output (stats):**
```
[SPINLOCK] === Critical Section Timing ===
  Telemetry:  Max: 8us, Avg: 3us, Count: 45000
  Motion:     Max: 12us, Avg: 5us, Count: 120000
  Config:     Max: 15us, Avg: 7us, Count: 1200
```

---

## Network Commands

### `wifi` - WiFi Management

**Description:** Manage WiFi station and access point modes.

**Syntax:**
```
wifi                        # Show help
wifi scan                   # Scan for networks
wifi connect <ssid> <pass>  # Connect to network
wifi status                 # Show connection status
wifi ap on                  # Enable AP mode
wifi ap off                 # Disable AP mode
wifi ap status              # Show AP configuration
wifi ap set s <ssid>        # Set AP SSID
wifi ap set p <password>    # Set AP password (min 8 chars)
```

#### `wifi scan`
**Example Output:**
```
[WIFI] Scanning...
[WIFI] Found 5 networks:
   1: MyNetwork                          | -45 dBm
   2: Neighbor_WiFi                      | -72 dBm
   3: CoffeeShop                         | -80 dBm
```

#### `wifi connect`
**Example:**
```
wifi connect MyNetwork SecretPassword123
```

**Example Output:**
```
[WIFI] Connecting to 'MyNetwork'...
[WIFI] [OK] Connection initiated (non-blocking)
[WIFI] Note: WiFi connects in background during normal operation
[WIFI] Use 'wifi status' to check connection progress
```

#### `wifi status`
**Example Output:**
```
[WIFI] === Status ===
  Status: CONNECTED
  MAC:    AA:BB:CC:DD:EE:FF
  SSID:   MyNetwork
  IP:     192.168.1.100
  RSSI:   -45 dBm
```

**Possible Status Values:**
- `CONNECTED` - Successfully connected
- `DISCONNECTED` - Not connected
- `CONNECT_FAILED` - Connection attempt failed

---

### `eth` - Ethernet Management (KC868-A16)

**Description:** Manage Ethernet connection for wired networking.

**Syntax:**
```
eth                         # Show help
eth status                  # Show detailed status
eth on                      # Enable Ethernet
eth off                     # Disable Ethernet
eth dhcp                    # Use DHCP (default)
eth static <ip> <gw> [mask] # Set static IP
eth dns <dns_ip>            # Set DNS server
```

#### `eth status`
**Example Output (Connected):**
```
[ETH] === Ethernet Status ===
  Enabled:     YES
  Mode:        DHCP
  Status:      CONNECTED
  IP:          192.168.1.100
  Gateway:     192.168.1.1
  Subnet:      255.255.255.0
  DNS:         8.8.8.8
  MAC:         AA:BB:CC:DD:EE:FF
  Link Speed:  100 Mbps
  Duplex:      Full
  Uptime:      01:23:45
  Reconnects:  0
  Errors:      0
```

**Example Output (Disconnected):**
```
[ETH] === Ethernet Status ===
  Enabled:     YES
  Mode:        DHCP
  Status:      DISCONNECTED
  Reconnects:  2
  Errors:      0
```

#### `eth static`
**Description:** Configure static IP address.

**Syntax:**
```
eth static <ip> <gateway> [subnet_mask]
```

**Parameters:**
| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| ip | Yes | - | Static IP address |
| gateway | Yes | - | Default gateway |
| subnet_mask | No | 255.255.255.0 | Subnet mask |

**Examples:**
```
eth static 192.168.1.50 192.168.1.1
eth static 192.168.1.50 192.168.1.1 255.255.255.0
eth dns 8.8.8.8
reboot
```

**Example Output:**
```
[ETH] [OK] Static IP configured:
  IP:      192.168.1.50
  Gateway: 192.168.1.1
  Mask:    255.255.255.0
[ETH] Reboot required for changes to take effect.
```

> **⚠️ IMPORTANT:** All Ethernet configuration changes require a reboot!

---

### `ota_setpass` - Set OTA Update Password

**Description:** Change the over-the-air firmware update password.

**Syntax:**
```
ota_setpass                 # Show current status
ota_setpass <new_password>  # Set new password
```

**Example Output (status):**
```
[OTA] === OTA Password Management ===
Usage: ota_setpass <new_password>
Note: Password must be at least 8 characters
      Requires reboot to take effect

Current: DEFAULT PASSWORD (insecure!)
```

**Example:**
```
ota_setpass MySecureOTAPass99
```

**Example Output:**
```
[OTA] [OK] Password updated successfully
[OTA] Reboot required for changes to take effect
```

**Possible Errors:**
```
[OTA] Password must be at least 8 characters
```

---

## Job Management Commands

### `job_start` - Start G-Code Job

**Description:** Start executing a G-code file.

**Syntax:**
```
job_start <filename>
```

**Example:**
```
job_start /gcode/my_project.nc
```

---

### `job_abort` - Abort Job

**Description:** Stop currently running job immediately.

**Syntax:**
```
job_abort
```

---

### `job_status` - Job Status

**Description:** Show current job progress and state.

**Syntax:**
```
job_status
```

---

## Diagnostics Commands

### `faults` - Fault Log Management

**Description:** View and manage the fault log.

**Syntax:**
```
faults                  # Show recent faults
faults clear            # Clear fault log
faults export           # Export to JSON
```

---

### `encoder` - Encoder Management

**Description:** Encoder diagnostics and configuration.

**Syntax:**
```
encoder                 # Show help
encoder status          # Show encoder status
encoder values          # Show current counts
encoder reset           # Reset encoder counters
```

---

### `spindle` - Spindle Monitor & Alarms

**Description:** Monitor spindle current and configure alarms.

**Syntax:**
```
spindle                 # Show help
spindle status          # Show spindle status
spindle set threshold <amps>    # Set overcurrent threshold
spindle set enable <0|1>        # Enable/disable monitoring
```

**Example Output:**
```
[SPINDLE] === Status ===
  Monitoring: ENABLED
  Current:    5.2 A
  Peak:       8.7 A
  Threshold:  15.0 A
  Running:    YES
  Overcurrent: NO
  Errors:     0
```

---

### `telemetry` - System Telemetry

**Description:** Comprehensive system health metrics.

**Syntax:**
```
telemetry               # Show telemetry snapshot
telemetry json          # Export as JSON
```

**Example Output:**
```
[TELEMETRY] === System Health ===
  Status:     NORMAL
  Uptime:     04:35:22
  CPU:        25%
  Free Heap:  185,432 bytes
  WiFi:       CONNECTED (-52 dBm)
  Ethernet:   CONNECTED (100 Mbps)
```

---

### `debug` - System Diagnostics

**Description:** Debug-level system information.

**Syntax:**
```
debug                   # Show debug info
debug verbose <0|1>     # Set verbose mode
```

---

### `selftest` - Hardware Self-Test

**Description:** Run comprehensive hardware diagnostics.

**Syntax:**
```
selftest
```

**Example Output:**
```
[SELFTEST] === Hardware Self-Test ===
  I2C Bus:      [PASS] Found 4 devices
  PCF8574 IN1:  [PASS] Address 0x20
  PCF8574 IN2:  [PASS] Address 0x21
  PCF8574 OUT1: [PASS] Address 0x22
  PCF8574 OUT2: [PASS] Address 0x23
  Encoders:     [PASS] All responding
  VFD Modbus:   [PASS] Communication OK
  RS-485 Bus:   [PASS] No errors
  LittleFS:     [PASS] 1.2MB free
  =====================================
  Result: ALL TESTS PASSED
```

---

### `dio` - Digital I/O Status

**Description:** Display all digital input/output states.

**Syntax:**
```
dio                     # Show all I/O status
dio inputs              # Show inputs only
dio outputs             # Show outputs only
```

**Example Output:**
```
[DIO] === Digital I/O Status ===

Inputs (PCF8574):
  IN1:  [1] [0] [0] [1] [0] [0] [0] [0]
  IN2:  [0] [0] [0] [0] [0] [0] [0] [0]

Outputs (PCF8574):
  OUT1: [0] [0] [1] [0] [0] [0] [0] [0]
  OUT2: [0] [0] [0] [0] [0] [0] [0] [0]

Special:
  E-Stop:  INACTIVE
```

---

### `runtime` - Machine Runtime Counter

**Description:** Display total machine runtime and cycle count.

**Syntax:**
```
runtime                 # Show runtime stats
runtime reset           # Reset counters (CAUTION)
```

**Example Output:**
```
[RUNTIME] === Machine Statistics ===
  Total Runtime:     1,234:56:78 (hours:min:sec)
  Power Cycles:      456
  Job Cycles:        12,345
  X Distance:        15,678.5 m
  Y Distance:        8,901.2 m
  Z Distance:        2,345.6 m
  Last Maintenance:  890:12:34 runtime hours ago
```

---

### `rs485` - RS-485 Device Registry

**Description:** Manage RS-485 bus devices (Modbus).

**Syntax:**
```
rs485                   # Show help
rs485 status            # Show registered devices
rs485 scan              # Scan for devices
```

---

### `metrics` - Task Performance Monitoring

**Description:** Display FreeRTOS task execution metrics.

**Syntax:**
```
metrics                 # Show all task metrics
metrics reset           # Reset statistics
```

**Example Output:**
```
[METRICS] === Task Performance ===
  Task Name        | Avg (us) | Max (us) | Calls
  -----------------+----------+----------+---------
  Motion           |      450 |     1,200|  245,000
  Encoder          |      125 |       380|  122,500
  Telemetry        |      890 |     2,100|   12,250
  WebServer        |    3,200 |    15,000|    8,750
```

---

### `wdt` / `task` - Watchdog & Task Management

**Description:** Watchdog timer and task scheduler diagnostics.

**Syntax:**
```
wdt status              # Show watchdog status
task list               # List all tasks
task_list               # Detailed task list with stack
```

---

## Configuration Commands

### `config` - Configuration Management

**Description:** View and modify NVS configuration parameters.

**Syntax:**
```
config                      # Show help
config list                 # List all config keys
config get <key>            # Get value
config set <key> <value>    # Set value
config save                 # Save to NVS
config load                 # Reload from NVS
config reset                # Reset to defaults (DANGEROUS)
config export               # Export to JSON
config import <json>        # Import from JSON
```

**Example - Get Value:**
```
config get wifi_ssid
```
```
[CONFIG] wifi_ssid = "MyNetwork"
```

**Example - Set Value:**
```
config set spindl_thr 20
config save
```
```
[CONFIG] spindl_thr = 20
[CONFIG] [OK] Configuration saved
```

**Common Configuration Keys:**

| Key | Type | Description | Default |
|-----|------|-------------|---------|
| wifi_ssid | string | WiFi network name | - |
| wifi_pass | string | WiFi password | - |
| wifi_ap_en | int | Enable AP mode (0/1) | 1 |
| wifi_ap_ssid | string | AP SSID | BISSO-E350-Setup |
| wifi_ap_pass | string | AP password | password |
| eth_en | int | Enable Ethernet (0/1) | 1 |
| eth_dhcp | int | Use DHCP (0/1) | 1 |
| eth_ip | string | Static IP | - |
| spindl_en | int | Enable spindle monitor | 1 |
| spindl_thr | int | Overcurrent threshold (A) | 30 |
| enc_fb_en | int | Enable encoder feedback | 1 |
| x_limit_min | int | X soft limit min (mm) | 0 |
| x_limit_max | int | X soft limit max (mm) | 1000 |
| def_spd | int | Default speed | 500 |
| def_acc | int | Default acceleration | 100 |

**Possible Errors:**
```
[CONFIG] Key not found: invalid_key
```

---

## Calibration Commands

### `calib` - Distance Calibration

**Description:** Automatic encoder distance calibration.

**Syntax:**
```
calib <axis> <distance_mm>
```

**Example:**
```
calib X 100
```

---

### `calibrate speed` - Speed Profile Calibration

**Description:** Auto-detect and save motion profile speeds.

**Syntax:**
```
calibrate speed <axis> <profile> <target_mm>
```

**Parameters:**
| Parameter | Values | Description |
|-----------|--------|-------------|
| axis | X/Y/Z/A | Axis to calibrate |
| profile | SLOW/FAST/RAPID | Speed profile |
| target_mm | number | Move distance in mm |

**Example:**
```
calibrate speed X FAST 500
```

---

### `calibrate ppmm` - Pulses Per MM Calibration

**Description:** Manual encoder PPM measurement.

**Syntax:**
```
calibrate ppmm <axis> <move_mm>      # Start calibration
calibrate ppmm end                    # Complete measurement
calibrate ppmm <axis> reset           # Reset to defaults
```

**Workflow:**
1. Run `calibrate ppmm X 100`
2. Manually move axis exactly 100mm
3. Run `calibrate ppmm end`
4. System calculates and saves PPM value

---

### `vfd diagnostics` - VFD Health Diagnostics

**Description:** Altivar31 VFD telemetry and diagnostics.

**Syntax:**
```
vfd diagnostics                 # Show help
vfd diagnostics status          # Show VFD status
vfd diagnostics faults          # Show fault history
```

---

## I2C Bus Commands

### `i2c` - I2C Bus Management

**Description:** I2C bus diagnostics and device management.

**Syntax:**
```
i2c                     # Show help
i2c scan                # Scan for devices
i2c read <addr> <reg>   # Read register
i2c write <addr> <reg> <val>  # Write register
i2c status              # Show bus status
i2c recovery            # Attempt bus recovery
```

#### `i2c scan`
**Example Output:**
```
[I2C] === Bus Scan ===
  Address 0x20: PCF8574 (Input Bank 1)
  Address 0x21: PCF8574 (Input Bank 2)
  Address 0x22: PCF8574 (Output Bank 1)
  Address 0x23: PCF8574 (Output Bank 2)
  Address 0x27: LCD Display
  
  Found 5 devices
```

**Possible Errors:**
```
[I2C] Bus stuck - attempting recovery
[I2C] Recovery failed - check wiring
```
**Fix:** Check SDA/SCL wiring, ensure pull-up resistors present.

---

## System Commands

### `help` - Show All Commands

**Syntax:**
```
help
```

---

### `reboot` - Restart System

**Syntax:**
```
reboot
```

---

### `info` - System Information

**Syntax:**
```
info
```

**Example Output:**
```
[SYSTEM] === BISSO E350 Controller ===
  Firmware:   3.5.25
  Build:      2025-12-31
  Chip:       ESP32-D0WDQ6
  Flash:      4MB
  PSRAM:      None
  Free Heap:  185,432 bytes
  Uptime:     04:35:22
```

---

### `echo` - Toggle Command Echo

**Syntax:**
```
echo on                 # Enable command echo
echo off                # Disable command echo
```

---

## G-Code / Grbl Commands

The CLI supports limited Grbl-compatible commands:

| Command | Description |
|---------|-------------|
| `$` | Show Grbl settings |
| `$H` | Home all axes |
| `?` | Query current state |
| `$Jog=X10F500` | Jog X axis 10mm at 500mm/min |
| `G0 X100 Y50` | Rapid move |
| `G1 X200 Y100 F300` | Linear move at 300mm/min |
| `G28` | Return to home position |
| `G30` | Return to secondary home |
| `G54`-`G59` | Select work coordinate system |

---

## Error Reference

### Common Error Messages

| Error Message | Cause | Solution |
|---------------|-------|----------|
| `Invalid axis` | Unknown axis letter | Use X, Y, Z, or A |
| `E-Stop active` | Emergency stop triggered | Clear E-Stop with `estop off` |
| `Motion busy` | Axis currently moving | Wait or use `stop` command |
| `Out of limits` | Target exceeds soft limits | Adjust limits or target |
| `I2C timeout` | I2C device not responding | Check wiring, use `i2c recovery` |
| `Modbus error` | RS-485 communication failed | Check connections, baud rate |
| `Config key too long` | NVS key > 15 characters | Use shorter key name |
| `Insufficient heap` | Out of memory | Reboot, reduce operations |
| `WiFi not connected` | No network connection | Use `wifi connect` |
| `Ethernet disconnected` | Cable unplugged | Check cable connection |
| `OTA password too short` | Password < 8 chars | Use longer password |
| `Static IP invalid` | Malformed IP address | Check IP format (x.x.x.x) |

### LED Status Indicators

| LED Pattern | Meaning |
|-------------|---------|
| Solid Green | System OK, idle |
| Blinking Green | Motion in progress |
| Solid Red | E-Stop active |
| Blinking Red | Fault condition |
| Solid Yellow | Warning condition |
| Blinking Blue | WiFi connecting |

---

## Quick Reference Card

```
┌─────────────────────────────────────────────────────────────────┐
│                    BISSO E350 CLI Quick Reference               │
├─────────────────────────────────────────────────────────────────┤
│ MOTION          │ NETWORK            │ DIAGNOSTICS              │
│ status          │ wifi status        │ faults                   │
│ stop            │ wifi scan          │ selftest                 │
│ pause / resume  │ wifi connect       │ dio                      │
│ estop on/off    │ eth status         │ telemetry                │
│ feed <0.1-2.0>  │ eth static/dhcp    │ runtime                  │
│ limit X 0 1000  │                    │ metrics                  │
├─────────────────┴────────────────────┴──────────────────────────┤
│ CONFIGURATION                                                    │
│ config list        - Show all settings                          │
│ config get <key>   - Read setting                               │
│ config set <k> <v> - Write setting                              │
│ config save        - Persist changes                            │
├──────────────────────────────────────────────────────────────────┤
│ EMERGENCY: estop on    │    REBOOT: reboot                      │
└──────────────────────────────────────────────────────────────────┘
```

---

*Document generated for BISSO E350 CNC Controller Firmware v1.0.0 (Gemini)*

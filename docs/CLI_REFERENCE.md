# BISSO E350 Controller - Command Line Interface Manual

**Version:** 1.1.0 (PosiPro)  
**Last Updated:** 2026-01-11  
**Connection:** Serial (115200 baud) or Telnet (Port 23)

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Motion Control Commands](#motion-control-commands)
3. [Network Commands](#network-commands)
4. [Job Management Commands](#job-management-commands)
5. [Diagnostics Commands](#diagnostics-commands)
    - [status (Dashboard)](#status---quick-system-status-dashboard)
    - [motionstatus (Low-Level)](#motionstatus---motion-status-low-level)
    - [spinlock](#spinlock---critical-section-timing)
    - [debug](#debug---system-info-dump)
    - [rs485](#rs485---bus-diagnostics)
    - [wdt / watchdog](#wdt---watchdog-management)
    - [task / task_list](#task---process-monitoring)
    - [memory](#memory---heap-analysis)
    - [encoder_deviation](#encoder_deviation---motion-accuracy)
    - [fault_recovery](#fault_recovery---recovery-stats)
    - [timeouts (Show timeout diagnostics)](#timeouts---show-timeout-diagnostics)
    - [predict (Motion Position Prediction)](#predict---motion-position-prediction)
    - [lcd reset (Manual Bus Recovery)](#lcd-reset)
    - [selftest](#selftest---hardware-hardware-diagnostic)
    - [dio](#dio---digital-io-status)
    - [runtime](#runtime---machine-runtime-counter)
    - [metrics](#metrics---task-performance-monitoring)
6. [System Commands](#system-commands)
    - [auth](#auth---authentication-diagnostics)
    - [lcd](#lcd---display-control)
    - [jxk10](#jxk10---current-sensor-direct)
    - [spindle](#spindle---monitoring--config)
    - [web](#web---web-server-configuration)
    - [api](#api---api-rate-limiter-diagnostics)
    - [reboot](#reboot---restart-system)
    - [info](#info---system-information)
    - [echo](#echo---toggle-command-echo)
7. [Configuration Commands](#configuration-commands)
    - [config](#config---configuration-management)
    - [nvs](#nvs---nvs-storage-inspector)
    - [encoder_baud_set](#encoder_baud_set---set-encoder-baud-rate)
8. [Calibration Commands](#calibration-commands)
9. [I2C Bus Commands](#i2c-bus-commands)
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

---

### `status` - Quick System Status Dashboard

**Description:** Displays a high-level summary of system health, positions, network status, and active faults.

**Syntax:**
```
status
```

**Example Output:**
```
+============================================================+
|           BISSO E350 QUICK STATUS DASHBOARD               |
|  Uptime: 00:15:23                                        |
+============================================================+
| POSITION (mm)                                             |
|   X:      0.000    Y:      0.000                        |
|   Z:      0.000    A:      0.000                        |
+------------------------------------------------------------+
| ENCODER FEEDBACK                                          |
|   Status: [ON]                                            |
+------------------------------------------------------------+
| SPINDLE CURRENT                                           |
|   Current:   5.2 A  |  Peak:   8.4 A                    |
|   Alarm: OK                                               |
+------------------------------------------------------------+
| NETWORK                                                   |
|   WiFi: Connected (-45 dBm)                               |
|   IP: 192.168.1.50                                        |
+------------------------------------------------------------+
| ACTIVE FAULTS                                             |
|   [NONE] System healthy                                   |
+============================================================+
```

---

### `spinlock` - Critical Section Timing

**Description:** Diagnostics for interrupt latency and thread safety locks. Identifies code sections that block interrupts for too long (>10µs).

**Syntax:**
```
spinlock stats     # Show cumulative timing report
spinlock reset     # Reset all counters
```

**Example:**
```
[SPINLOCK] === Spinlock Timing Diagnostics ===
  Total Locks:  154238
  Hold Time Max: 18.5 us (WARNING: >10us)
  Hold Time Avg: 1.2 us
  Location: motion_planner.cpp:L145
```

---

### `axis` - Per-Axis Motion Quality
**Description:** Real-time analysis of the motion system health. Detects mechanical jitter, encoder stalls, and communication errors.

**Syntax:**
```
axis status      # Summary dash for all axes
axis <letter>    # Deep dive for X, Y, Z, or A
```

**Output Example (`axis X`):**
```text
[AXIS X] DIAGNOSTICS:
  Quality Score:  98.5%
  Jitter Amp:     1.2 counts (0.012mm)
  Comm Errors:    2
  State:          STABLE
```

---

### `debug` - System Info Dump

**Description:** Comprehensive dump of internal state for factory debugging.

**Syntax:**
```
debug all          # Complete system dump
debug encoders     # Detailed WJ66 encoder status
debug config       # In-memory config table dump
```

---

### `rs485` - Bus Diagnostics

**Description:** Monitor health of the global Modbus/RS-485 bus.

**Syntax:**
```
rs485 diag
```

**Example:**
```
[RS485] === Bus Diagnostics ===
  Bus State:   HEALTHY
  Error Rate:  0.2%
  Active Slaves: 3 (Altivar, JXK10, YHTC05)
  Timeouts:    0
```

---

### `wdt` - Watchdog Management

**Description:** Monitor task health and scheduler responsiveness.

**Syntax:**
```
wdt status         # Show global watchdog status
wdt tasks          # List all registered tasks
wdt stats          # Timing jitter statistics
wdt report         # Detailed health report
wdt test           # TRIGGER FAULT (Forces 10s stall to test reboot)
```

---

### `task` - Process Monitoring

**Description:** FreeRTOS task monitoring and CPU usage.

**Syntax:**
```
task list          # Table of all running tasks
task stats         # Stack usage and run-time %
task cpu           # Overall CPU load
```

**Example Output:**
```
[TASK] Task List:
  ID   Task Name  Pri   Stack(min)   CPU(%)
  1    Main       10    452          2.5%
  2    Motion     15    890          1.2%
  3    WebServer   5    1520         0.8%
  4    Modbus      8    310          0.4%
[TASK] CPU: 5%
```
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

### `lcd` - LCD Display Control

**Description:** Configure and control the 20x4 LCD display and backlight.

**Syntax:**
```
lcd                     # Show help
lcd on                  # Enable LCD and save setting
lcd off                 # Disable LCD and save setting
lcd backlight <on|off>  # Control backlight
lcd sleep               # Force display to sleep
lcd wakeup              # Force display to wake up
lcd timeout <seconds>   # Set sleep timeout (0=never)
lcd status              # Show LCD configuration status
```

**Example Output (status):**
```
[LCD] === Status ===
  Enabled:   YES
  Mode:      0
  Sleeping:  NO
  Timeout:   300 sec
```

**Fix:** Verify I2C address is 0x27. Check that the LCD is powered. Verify that the I2C bus isn't locked by the PLC (try `i2c clear`).

### `predict` - Motion Position Prediction

**Description:** View and debug the real-time position prediction engine. This tool helps diagnose lag between the physical machine and the Web UI.

**Syntax:**
```
predict <axis>       # Detailed view for specific axis
predict stats        # Global performance stats
```

**Parameters:**
| Parameter | Type | Required | Description |
|:---|:---|:---|:---|
| axis | Char | Yes | X, Y, Z, or A |

**Example Output (`predict X`):**
```text
[PREDICT] X-Axis Evaluation:
  Raw Enc:     125000     (Last Hardware Count)
  Latched:     125000     (Baseline for extrapolation)
  Velocity:    15.2 mm/s  (Calculated over 50ms)
  Delta:       240ms      (Time since last pulse)
  Predicted:   125365     (Extrapolated value)
  Gain:        +365 pulses (+3.65mm)
  Clamp:       ENABLED    (Limited by tgt_margin)
```

**Troubleshooting:**
- **"Prediction = 0"**: The axis is stationary or the encoder is not talking. Check `encoder status`.
- **"Jumping values"**: Velocity jitter is high. Check for mechanical slippage in the encoder belt/coupler.
- **"Out of range"**: The prediction reached the `tgt_margin` limit. This is normal during very high-speed traverses (e.g., G0 at 100Hz).

------

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

### `motionstatus` - Motion Status (Low-Level)

**Description:** Display real-time low-level motion data directly from the motion controller. This is more verbose than the `status` command and useful for debugging motion cycles.

**Syntax:**
```
motionstatus
```

**Output Fields:**
- `AxisState`: CURRENT axis state (IDLE, BUSY, ALARM)
- `Flags`: Bitmask of active motion flags
- `QueueSize`: Number of moves in the motion buffer
- `Velocity`: Current instantaneous velocity (mm/min)

---

### `estop` - Emergency Stop Management

**Description:** Manually trigger, clear, or view the Emergency Stop status.

**Syntax:**
```
estop status          # Show current E-Stop state
estop on              # Manually trigger software E-Stop
estop off             # Clear software E-Stop alarm
```

---

### `limit` - Set Soft Limits

**Description:** Configure software travel limits for each axis.

**Syntax:**
```
limit <axis> <min> <max>
```

**Parameters:**
- `axis`: X, Y, Z, or A
- `min`: Minimum coordinate (mm)
- `max`: Maximum coordinate (mm)

**Example:**
```
limit X 0 1000
```

---

### `feed` - Set Feed Override

**Description:** Dynamically adjust the feed rate during motion.

**Syntax:**
```
feed <multiplier>
```

**Parameters:**
- `multiplier`: Float between `0.1` and `2.0` (10% to 200%)

---

### `job_start` - Execute G-Code Job

**Description:** Start a pre-loaded G-code job.

**Syntax:**
```
job_start <filename>
```

---

### `job_abort` - Abort Current Job

**Description:** Stop the currently executing job immediately.

**Syntax:**
```
job_abort
```

---

### `job_status` - Get Job Progress

**Description:** Show percentage completion and estimated time remaining for the current job.

**Syntax:**
```
job_status
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

### `log` - Boot Log Management

**Description:** View, manage, and configure boot log capture.

**Syntax:**
```
log                     # Show help
log boot                # Display captured boot log from last startup
log enable              # Show current enable status
log enable on           # Enable boot log capture
log enable off          # Disable boot log capture
log delete              # Delete the boot log file
```

#### `log boot`
**Example Output:**
```
[LOG] === Boot Log ===
Boot log size: 4523 bytes

=== BOOT LOG START ===
[SYSTEM] BISSO E350 Controller v3.5.x booting
[BOOTLOG] Boot log capture started
[MOTION] Initializing motion system...
[ENCODER] WJ66 encoder self-test passing...
[VFD] Altivar 31 connection established
[NETWORK] WiFi connected to "BISSO_5GHz" IP: 192.168.1.100
[WEB] Web server listening on port 80
[SYSTEM] Boot complete, ready for operation

=== BOOT LOG END ===
[LOG] === End Boot Log ===
```

**Notes:**
The boot log is limited to 4KB. If the system crashes early, this is the first place to look.

---

### `predict` - Motion Position Prediction

**Description:** View real-time extrapolation diagnostics for the motion smoothing system.

**Syntax:**
```
predict <axis>
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| axis | 0-3 | Axis index (0=X, 1=Y, 2=Z, 3=A) |

**Example:**
```
predict 0
```

**Example Output:**
```text
=== PREDICTION DIAGNOSTICS ===
Axis:            0
Raw Position:    12540
Actual Latched:  12540
Predicted:       12572
Prediction Gap:  32             <-- Extra "smooth" distance (counts)
Velocity:        1.600 counts/ms
Update Age:      20 ms          <-- Time since last hardware update
```

#### Understanding the Output
- **Raw Position**: The last literal value received from the RS485 encoder.
- **Predicted**: The position the motion controller is currently using for soft-limits and target detection.
- **Prediction Gap**: If this is constantly **0** during motion, the prediction system is disabled or stalled.
- **Update Age**: Should be **< 60ms**. If > 200ms, prediction is automatically disabled for safety.

#### Troubleshooting
- **Gap is negative**: Check axis direction calibration.
- **Gap stays 0**: Verify that velocity is non-zero and that encoder feedback is enabled (`config encoder_feedback 1`).
- **Jittery Gap**: High vibration or unstable encoder baud rate. Check WJ66 RS485 cables.
- Boot log captures all serial output during device startup
- Useful for debugging boot issues when serial monitor not available
- Maximum log size: 32KB (configurable)
- Log is overwritten on each boot
- Stored at `/bootlog.txt` on LittleFS filesystem

#### `log enable`
**Example Output (status):**
```
[LOG] Boot log capture: ENABLED
[LOG] Usage: log enable [on | off]
```

**Example (enable):**
```
log enable on
```
```
[LOG] Boot log capture ENABLED (takes effect on next boot)
```

#### `log delete`
**Example Output:**
```
[LOG] Boot log deleted
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

---

**Example Output (read):**
```
[JXK10] === Current Reading ===
Current:    15.25 A
Raw Value:  1525
Last Read:  50 ms ago
```

**Example Output (info):**
```
[JXK10] === Device Info ===
Enabled:       YES
Slave Address: 1 (0x01)
Baud Rate:     9600 bps
Read Count:    12345
Error Count:   2
```

**Notes:**
- Default address: 1, default baud: 9600
- Address change requires power cycle to take effect
- Scaling: raw ≤ 3000 → ÷100 (2 decimals), raw > 3000 → ÷10 (1 decimal)

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

### `memory` - Heap Analysis

**Description:** Detailed memory monitoring and heap fragmentation analysis.

**Syntax:**
```
memory                  # Show help and available subcommands
memory stats            # Detailed memory diagnostics
memory reset            # Reset minimum free heap counter
memory detailed         # Deep analysis with fragmentation
```

**Example Output (memory stats):**
```
=== MEMORY DIAGNOSTICS ===
Total Heap:   320760
Current Free: 185432 (42% used)
Min Free:     142100
Max Used:     178660
Largest Blk:  110592
Samples:      12450
Status: [GOOD]
```

**Example (memory detailed):**
```
[MEMORY] === Detailed Memory Analysis ===

Heap Summary:
  Total:      320760 bytes
  Used:       135328 bytes (42.2%)
  Free:       185432 bytes (57.8%)
  Minimum:    142100 bytes (lowest ever)
  Largest Block: 110592 bytes (max contiguous)

Fragmentation: 40.4%
```

---

### `encoder_deviation` - Motion Accuracy

**Description:** Displays a statistical analysis of the deviation between commanded position and encoder feedback.

**Syntax:**
```
encoder_deviation
```

---

### `fault_recovery` - Recovery Stats

**Description:** Shows statistics on automatic fault recoveries and power-loss resumes.

**Syntax:**
```
fault_recovery
```

---

## System Commands

### `auth` - Authentication Diagnostics

**Description:** Manage and diagnose the SHA-256 authentication system. Useful for troubleshooting login issues, testing password verification, and clearing rate limits after lockouts.

**Syntax:**
```
auth                    # Show usage help
auth diag               # Display authentication diagnostics
auth test <password>    # Test if a password matches the stored hash
auth reload             # Reload credentials from NVS
auth clear_limits       # Clear all IP rate limits (useful after lockout)
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `diag` | Show username, hash format (SHA-256 or plain text), password change status, rate limit entries |
| `test <password>` | Verify if a password matches the stored credential without logging in |
| `reload` | Reload credentials from NVS storage (use after manual NVS edits) |
| `clear_limits` | Clear all brute-force rate limit entries (unlocks all locked IPs) |

**Example - Diagnostics:**
```
auth diag
```
```
[AUTH] === Authentication Diagnostics ===
Username:          admin
Credentials Loaded: YES
Password Change:   Not required
Hash Format:       SHA-256 (secure)
Hash Length:       97 chars
Hash Preview:      $sha256$a1b2c3d4e5f6...
Rate Limit IPs:    0/16
```

**Example - Test Password:**
```
auth test MyPassword123!
```
```
[AUTH] Password test: MATCH
```

**Troubleshooting:**
- **"Password test: NO MATCH"**: Clear browser cache, try incognito mode, or use `auth reload` to refresh
- **"Rate limit exceeded"**: Use `auth clear_limits` to unlock locked out IPs
- **"Hash Format: PLAIN TEXT"**: Upgrade to SHA-256 by re-setting the password with `web_setpass`

---

### `lcd` - Display Control

**Description:** Control the 20x4 LCD display module. Manage power state, backlight, sleep mode, and view current status.

**Syntax:**
```
lcd                     # Show usage help
lcd on                  # Enable LCD and save setting to NVS
lcd off                 # Disable LCD and save setting to NVS
lcd backlight on        # Turn backlight on
lcd backlight off       # Turn backlight off
lcd sleep               # Force display to sleep mode
lcd wakeup              # Wake display from sleep
lcd timeout <seconds>   # Set auto-sleep timeout (0=never sleep)
lcd status              # Show current LCD status
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `on` | Enable LCD module and save to NVS (persistent) |
| `off` | Disable LCD module and save to NVS (persistent) |
| `backlight [on\|off]` | Control backlight without affecting NVS |
| `sleep` | Force display into power-saving sleep mode |
| `wakeup` | Wake display from sleep mode |
| `timeout <sec>` | Set auto-sleep timeout in seconds (0 disables auto-sleep) |
| `status` | Display current LCD configuration and state |

**Example - Status:**
```
lcd status
```
```
[LCD] === Status ===
Enabled:   YES
Mode:      4
Sleeping:  NO
Timeout:   300 sec
```

**Example - Set Timeout:**
```
lcd timeout 120
```
```
[LCD] Sleep timeout set to 120 seconds
```

**Troubleshooting:**
- **LCD not responding**: Check I2C connection with `i2c scan`, verify address 0x27
- **Display garbled**: Try `lcd off` then `lcd on` to reinitialize
- **Backlight flickers**: Power supply may be inadequate, check 5V rail

---

### `jxk10` - Current Sensor Direct Access

**Description:** Direct interface to the JXK-10 spindle current sensor via Modbus RTU. Read real-time current, check device info, configure address, and enable/disable the sensor.

**Syntax:**
```
jxk10                   # Show usage help
jxk10 read              # Read current value in Amps
jxk10 info              # Show device info (address, baud, stats)
jxk10 addr <1-247>      # Change Modbus slave address
jxk10 status            # Show full diagnostics
jxk10 enable            # Enable JXK-10 in config
jxk10 disable           # Disable JXK-10 in config
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `read` | Read and display current value in Amps |
| `info` | Show Modbus address, baud rate, communication statistics |
| `addr <address>` | Change the sensor's Modbus address (1-247) |
| `status` | Full diagnostic report including connection state and error counts |
| `enable` | Enable JXK-10 monitoring in NVS config |
| `disable` | Disable JXK-10 monitoring in NVS config |

**Example - Read Current:**
```
jxk10 read
```
```
[JXK10] Current: 8.5 A
```

**Example - Status:**
```
jxk10 status
```
```
[JXK10] === JXK-10 Current Sensor Status ===
  Enabled:      YES
  Address:      2
  Baud Rate:    9600
  Current:      8.5 A
  Reads OK:     1245
  Reads ERR:    3
  Last Error:   None
```

**Troubleshooting:**
- **"Reads ERR" increasing**: Check RS-485 wiring, termination resistor, baud rate mismatch
- **Current reads 0.0 A**: Verify sensor is powered and CT clamp is installed correctly
- **Communication timeout**: Use `rs485 diag` to diagnose bus issues

---

### `spindle` - Spindle Monitoring & Configuration

**Description:** Comprehensive spindle current monitoring, alarm configuration, and diagnostics. Monitors for tool breakage (sudden current drop) and stall conditions (overcurrent).

**Syntax:**
```
spindle                         # Show subcommand help
spindle diag                    # Print spindle diagnostics
spindle config show             # Display current configuration
spindle config enable [on|off]  # Enable/disable monitoring
spindle config address <1-247>  # Set JXK-10 Modbus address
spindle config threshold <amps> # Set overcurrent threshold (0-50 A)
spindle config interval <ms>    # Set poll interval (100-60000 ms)
spindle alarm status            # Show alarm states
spindle alarm clear             # Clear all alarms
spindle alarm toolbreak <amps>  # Set tool breakage threshold (1-20 A)
spindle alarm stall <amps> <ms> # Set stall parameters
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `diag` | Full spindle diagnostics with current values, alarms, and history |
| `config show` | Display all spindle configuration settings |
| `config enable [on\|off]` | Enable or disable spindle current monitoring |
| `config address <addr>` | Set JXK-10 sensor Modbus address |
| `config threshold <amps>` | Set overcurrent shutdown threshold |
| `config interval <ms>` | Set polling interval in milliseconds |
| `alarm status` | Show tool breakage and stall alarm states |
| `alarm clear` | Clear all active alarms |
| `alarm toolbreak <amps>` | Set tool breakage detection threshold (sudden drop) |
| `alarm stall <amps> <ms>` | Set stall threshold and timeout duration |

**Example - Diagnostics:**
```
spindle diag
```
```
[SPINDLE] === Diagnostics ===
  Monitoring:     ENABLED
  Current:        12.5 A
  RMS Average:    11.8 A
  Peak:           15.2 A
  Tool Breakage:  OK (threshold: 5.0 A drop)
  Stall:          OK (threshold: 25.0 A for 2000 ms)
```

**Example - Configure Alarm:**
```
spindle alarm stall 30 3000
```
```
[SPINDLE] Stall threshold set to 30.0 A for 3000 ms
```

**Troubleshooting:**
- **False tool breakage alarms**: Increase threshold with `spindle alarm toolbreak <higher_value>`
- **Stall not detected**: Lower threshold or increase timeout
- **Current reads 0**: Check `jxk10 status` for sensor connectivity

---

### `web` - Web Server Configuration

**Description:** Configure web server settings including username and password for HTTP Basic Authentication.

**Syntax:**
```
web                                 # Show usage help
web config show                     # Display current web config
web config username <name>          # Set web UI username (3-32 chars)
web config password <pass>          # Set web UI password (4-64 chars)
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `config show` | Display current web server username (password not shown) |
| `config username <name>` | Change the login username |
| `config password <pass>` | Change the login password |

**Example - Show Config:**
```
web config show
```
```
[WEB CONFIG] === Current Configuration ===
  Username: admin
  Password: ******** (hidden)
  Auth Enabled: YES
```

> **Note:** For stronger password management with SHA-256 hashing, use `web_setpass <password>` instead.

**Troubleshooting:**
- **Can't log in after password change**: Clear browser saved passwords, use incognito mode
- **Auth not working**: Verify `web_auth_en` is set to 1 in `config list`

---

### `api` - API Rate Limiter Diagnostics

**Description:** View and manage the API rate limiter used to prevent denial-of-service attacks on the web server.

**Syntax:**
```
api                     # Show subcommand help
api diag                # Show rate limiter diagnostics
api reset               # Reset all rate limit counters
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `diag` | Display current rate limit status, blocked IPs, request counts |
| `reset` | Clear all rate limit counters and unlock blocked endpoints |

**Example - Diagnostics:**
```
api diag
```
```
[API] === Rate Limiter Status ===
  Requests (1min): 45
  Blocked:         0
  Endpoints:       12 tracked
  Max per minute:  60
```

**Troubleshooting:**
- **API requests being blocked**: Use `api reset` to clear limits
- **Slow API responses**: Check `memory stats` for heap pressure

---

### `reboot` - Restart System

**Description:** Safely restart the ESP32 controller. All pending operations are halted, motion is stopped, and the system performs a clean reboot.

**Syntax:**
```
reboot                  # Restart the controller
reset                   # Alias for reboot
```

**Example:**
```
reboot
```
```
[SYSTEM] Rebooting...
```

> **Warning:** Motion will be stopped immediately. Ensure spindle is off and axes are in a safe position before rebooting.

---

### `info` - System Information

**Description:** Display comprehensive system information including firmware version, hardware details, uptime, and memory status.

**Syntax:**
```
info                    # Show system information
```

**Example:**
```
info
```
```
=== BISSO E350 Controller ===
  Firmware:    v1.1.0 (PosiPro)
  Build Date:  Jan 11 2026
  Hardware:    KC868-A16 ESP32-WROOM
  Uptime:      2h 34m 12s
  Free Heap:   185,432 bytes
  WiFi:        Connected (192.168.1.100)
  Ethernet:    Connected (192.168.1.101)
```

---

### `timeouts` - Show Timeout Diagnostics

**Description:** Display diagnostics for all active system timeouts including stall detection, communication timeouts, and safety timers.

**Syntax:**
```
timeouts                # Show timeout diagnostics
```

**Example:**
```
timeouts
```
```
[TIMEOUT] === Active Timeouts ===
  Motion Stall:     2000 ms (not active)
  Modbus Response:  100 ms
  Watchdog:         5000 ms
  LCD Sleep:        300 sec
```

---

### `selftest` - Hardware Self-Test

**Description:** Run comprehensive hardware diagnostic tests. Tests can be run individually by category or as a full suite.

**Syntax:**
```
selftest                # Run full test suite
selftest help           # Show available tests
selftest quick          # Quick health check (fast tests only)
selftest list           # List all available tests
selftest <category>     # Run specific category
```

**Categories:**

| Category | Description |
|----------|-------------|
| `memory` | Heap allocation, fragmentation, and minimum free tests |
| `i2c` | I2C bus scan and device communication tests |
| `storage` | LittleFS and NVS read/write tests |
| `motion` | Motion system initialization and encoder tests |
| `spindle` | Spindle monitor communication tests |
| `safety` | E-stop, limit switch, and alarm system tests |
| `network` | WiFi, Ethernet, and DNS connectivity tests |
| `watchdog` | Watchdog timer feed and recovery tests |

**Example - Full Suite:**
```
selftest
```
```
[SELFTEST] === Self-Test Suite ===

[MEMORY]    ✓ Heap allocation OK (185KB free)
[MEMORY]    ✓ Fragmentation OK (32.5%)
[I2C]       ✓ PCF8574 @0x21 OK
[I2C]       ✓ PCF8574 @0x22 OK
[I2C]       ✓ LCD @0x27 OK
[STORAGE]   ✓ LittleFS mounted (1.2MB free)
[STORAGE]   ✓ NVS read/write OK
[MOTION]    ✓ Encoder X OK
[MOTION]    ✓ Encoder Y OK
[MOTION]    ✗ Encoder Z - No response
[SPINDLE]   ✓ JXK-10 communication OK
[SAFETY]    ✓ E-Stop circuit OK
[NETWORK]   ✓ WiFi connected
[WATCHDOG]  ✓ Fed successfully

Summary: 12 passed, 1 failed
```

**Example - Quick Check:**
```
selftest quick
```
```
[SELFTEST] === Quick Health Check ===
[OK] Quick checks passed
```

**Troubleshooting:**
- **Test fails repeatedly**: Check hardware connections, power supply stability
- **"Encoder Z - No response"**: Verify encoder wiring and baud rate

---

### `dio` - Digital I/O Status

**Description:** Display the state of all digital inputs and outputs on the PCF8574 I/O expanders. Shows raw bit values and active channel names.

**Syntax:**
```
dio                     # Display all digital I/O status
```

**Example:**
```
dio
```
```
[DIO] === Digital I/O Status ===

+---------+----------------+------------------------+
| Addr    | Name           | State (MSB..LSB)       |
+---------+----------------+------------------------+
| 0x21    | INPUTS-SAFE    | 11110111 (0xF7)        |
|         |                | E-Stop
| 0x22    | INPUTS-AUX     | 11111111 (0xFF)        |
|         |                | (none active)
| 0x24    | OUTPUTS-MAIN   | 11111110 (0xFE)        |
|         |                | Spindle
| 0x25    | OUTPUTS-AUX    | 11111111 (0xFF)        |
|         |                | (none active)
+---------+----------------+------------------------+
Legend: Inputs=HIGH when active, Outputs=LOW when relay ON
```

**I/O Bank Mapping:**

| Address | Bank Name | Bit Labels (0-7) |
|---------|-----------|------------------|
| 0x21 | INPUTS-SAFE | Limit-X, Limit-Y, Limit-Z, E-Stop, Pause, Resume, Probe, Door |
| 0x22 | INPUTS-AUX | Home-X, Home-Y, Home-Z, Home-A, ToolSns, Coolant, In-15, In-16 |
| 0x24 | OUTPUTS-MAIN | Spindle, SpinDir, Coolant, Mist, Clamp, Vacuum, Light, Out-8 |
| 0x25 | OUTPUTS-AUX | AirBlast, Lube, Alarm, Ready, Running, Error, Out-15, Out-16 |

**Troubleshooting:**
- **"[NOT CONNECTED]"**: I2C expander not responding, check wiring and power
- **Inputs stuck HIGH/LOW**: Verify sensor wiring, check for short circuits

---

### `runtime` - Machine Runtime Counter

**Description:** Track cumulative machine runtime, job cycle count, and maintenance schedule. Data persists across reboots via NVS.

**Syntax:**
```
runtime                 # Show runtime statistics
runtime reset           # Reset cycle counter to 0
runtime maint           # Record maintenance performed (resets hours counter)
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| *(none)* | Display runtime, cycle count, and time since last maintenance |
| `reset` | Reset the job cycle counter to 0 |
| `maint` | Record that maintenance was performed (resets "since last maintenance" timer) |

**Example:**
```
runtime
```
```
[RUNTIME] === Machine Usage Statistics ===

+-------------------------+--------------------+
| Metric                  | Value              |
+-------------------------+--------------------+
| Total Runtime           | 156 hrs 42 min     |
| Job Cycles Completed    | 1,234              |
| Since Last Maintenance  | 45 hrs             |
+-------------------------+--------------------+
```

**Example - Record Maintenance:**
```
runtime maint
```
```
[RUNTIME] Maintenance recorded
```

> **Note:** A warning is displayed when runtime exceeds 100 hours since last maintenance.

---

### `metrics` - Task Performance Monitoring

**Description:** Monitor FreeRTOS task performance including execution times, CPU usage, and stack high-water marks.

**Syntax:**
```
metrics                 # Show subcommand help
metrics summary         # Quick performance summary
metrics detail          # Detailed task diagnostics
metrics reset           # Clear all collected metrics
```

**Subcommands:**

| Subcommand | Description |
|------------|-------------|
| `summary` | Overview of system performance (CPU, memory, worst-case timings) |
| `detail` | Per-task breakdown with stack usage, execution times, overruns |
| `reset` | Clear all performance counters to start fresh measurement |

**Example - Summary:**
```
metrics summary
```
```
[METRICS] === Performance Summary ===
  CPU Usage:      42%
  Heap Usage:     58%
  Worst Task:     Telemetry (1.2 ms max)
  Overruns:       0
```

**Example - Detailed:**
```
metrics detail
```
```
[METRICS] === Task Performance ===

+---------------+--------+----------+----------+--------+
| Task          | CPU %  | Avg (µs) | Max (µs) | Stack  |
+---------------+--------+----------+----------+--------+
| Motion        | 15.2%  | 450      | 1,200    | 78%    |
| Telemetry     | 8.5%   | 800      | 1,850    | 65%    |
| CLI           | 2.1%   | 150      | 12,000   | 45%    |
| Network       | 5.4%   | 300      | 2,500    | 52%    |
+---------------+--------+----------+----------+--------+
```

**Troubleshooting:**
- **High CPU usage**: Check for runaway tasks, reduce telemetry rate
- **Stack usage > 90%**: Task at risk of overflow, increase stack size in `task_manager.h`
- **High "Max" times**: Identify blocking operations, consider async alternatives

---

## Configuration Commands

### `echo` - Toggle Command Echo

**Description:** Enable or disable local echo for the CLI. When enabled, typed characters are echoed back to the terminal. Use the `save` option to persist the setting across reboots.

**Syntax:**
```
echo                    # Show current echo state
echo on                 # Enable echo (session only)
echo off                # Disable echo (session only)
echo on save            # Enable echo and save to NVS (persistent)
echo off save           # Disable echo and save to NVS (persistent)
```

**Example:**
```
echo on save
```
```
[INFO]  Echo ENABLED (saved to NVS)
```

> **Note:** The echo setting can also be configured via the Web UI: **Settings** → **CLI Options** → **Enable local echo**.

---

### `config` - Configuration Management

**Description:** View, modify, and backup NVS configuration parameters. Handles both internal logic and reactive hardware settings.

**Syntax:**
```
config list                 # List all available keys
config get <key>            # Read a specific value
config set <key> <value>    # Change a setting
config save                 # Persist all pending changes to Flash
config export               # Dump current config as reusable JSON
config import               # Enter interactive JSON import mode
config reset                # Wipe everything (FACTORY RESET)
```

**JSON Bulk Operations (Fleet Management):**
The `export` and `import` commands allow you to copy a specific machine "personality" to another controller.

**Example Export:**
```json
{
  "config": {
    "ppm_0": 100.5,
    "speed_cal_0": 1000,
    "limit_max_0": 500000
  }
}
```

**Using Config Import:**
1. Type `config import`
2. Paste the JSON block into the terminal.
3. Press **Enter twice** to confirm.
4. Type `config save` to make it permanent.

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

**Configuration Keys Reference:**

| Key | Type | Description | Default |
|-----|------|-------------|---------|
| **Network** | | | |
| `wifi_ssid` | string | WiFi SSID | - |
| `wifi_pass` | string | WiFi Password | - |
| `wifi_ap_en` | int | Enable AP Mode (1=on, 0=off) | 1 |
| `eth_en` | int | Enable Ethernet | 1 |
| `eth_dhcp` | int | Use DHCP (1=on, 0=fixed) | 1 |
| **Motion** | | | |
| `ppm_x/y/z/a`| float| Steps/Pulses per MM/Deg | 100.0 |
| `def_spd` | float| Default feedrate (mm/min) | 500 |
| `def_acc` | float| Default acceleration | 100 |
| `home_enable`| int | Enable Homing ($H) support | 1 |
| `x_appr_slow`| int | X Slow Approach Threshold (mm) | 5 |
| `x_appr_med` | int | X Medium Approach Threshold (mm) | 20 |
| `tgt_margin` | float | Target position margin (mm) | 0.1 |
| **Safety** | | | |
| `recov_en` | int | Power Loss Recovery enable | 1 |
| `buzzer_en` | int | Enable audible alarm | 1 |
| `st_light_en`| int | Enable Status Light (Red/Grn/Yel) | 1 |
| `sp_pause` | int | Auto-pause on spindle overload | 1 |
| `stall_ms` | int | Stall timeout (ms) | 2000 |
| **Hardware** | | | |
| `vfd_en` | int | Enable Modbus VFD Comm | 1 |
| `vfd_addr` | int | VFD Modbus ID | 2 |
| `jxk10_en` | int | Enable JXK-10 Current Monitoring | 1 |
| `lcd_en` | int | Enable LCD Display | 1 |
| `bootlog_en` | int | Enable boot log capture | 1 |
| `encoder_baud` | int | Encoder baud rate (1200-115200) | 9600 |
| `enc_iface` | int | Encoder Interface (0=RS232, 1=RS485) | 0 |
| `cli_echo` | int | Enable CLI echo by default (1=on, 0=off) | 0 |
| **Admin** | | | |
| `web_pass` | string | Web UI Password | password |
| `web_auth_en`| int | Require Web/Telnet login | 1 |

---

### `nvs` - NVS Storage Inspector

**Description:** Inspect and manage Non-Volatile Storage (NVS) for configuration persistence.

**Syntax:**
```
nvs stats                    # Show NVS usage statistics
nvs dump                     # Dump all NVS key-value pairs
nvs cleanup legacy           # Erase legacy configuration namespace
nvs cleanup faults           # Clear fault history from NVS
```

---

### `encoder_baud` - Config Recommendation (Reactive)

**Description:** The legacy `encoder_baud_set` command has been deprecated. Please use the unified configuration system. Setting this value immediately re-initializes the encoder hardware.

**Syntax:**
```
config set encoder_baud <baud_rate>
```

**Example:**
```
config set encoder_baud 115200
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

### `vfd` - VFD System Management
**Description:** Comprehensive control and monitoring of the Schneider Altivar 31 VFD.

**Syntax:**
```
vfd diagnostics status    # Real-time telemetry snap
vfd diagnostics thermal   # Watch heat trends
vfd config margin 20      # Set stall sensitivity (%)
vfd config timeout 2000   # Set stall delay (ms)
vfd config enable on      # Toggle active safety
```

**Parameters:**
| Parameter | Range | Default | Description |
|:---|:---|:---|:---|
| margin | 5-100% | 20% | Sensitivity for stall trigger |
| timeout| 100-60000 | 2000ms | Delay before alarm |

---

### `calibrate vfd current` - Load Baseline Setup
**Description:** Guided 3-phase workflow to teach the system what "normal" cutting looks like.

**Syntax:**
```
calibrate vfd current start     # Begin Phase 1 (Idle)
calibrate vfd current confirm   # Accept measurement & Move to next
calibrate vfd current finish    # Calc thresholds & Save
calibrate vfd current abort     # Exit safely
```

**Workflow Summary:**
1. **Phase 1**: Run blade in AIR (no stone).
2. **Phase 2**: Perform standard stone cut.
3. **Phase 3**: (Optional) Heavy-load cut.
4. **Calculated**: Stall threshold is set to `(Standard_Avg * Margin)`.

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

The CLI supports Grbl 1.1h compatible commands for motion and configuration.

### Real-Time Overrides
These commands are processed immediately, even during active motion.

| Command | Action | Description |
|---------|--------|-------------|
| `?` | Status | Get real-time status line (Positions, State, Buffers) |
| `!` | Hold | Feed hold - decelerate to a stop and pause |
| `~` | Resume | Cycle start - resume from pause or finish home |
| `Ctrl-X` | Reset | Soft reset - aborts motion and clears alarm state |

### Settings & State
| Command | Description | Example |
|---------|-------------|---------|
| `$` | Settings | List current Grbl settings (PPM, Speed, Accel) |
| `$H` | Home | Execute homing cycle on enabled axes |
| `$G` | State | Show current G-code parser state (Modal groups) |
| `$J=...`| Jog | Execute a jog move (e.g., `$J=G91X10F500`) |

### Standard G-Code
| Command | Description | Example |
|---------|-------------|---------|
| `G0` | Rapid | `G0 X100 Y50` |
| `G1` | Linear | `G1 X200 Y100 F300` |
| `G28`/`G30`| Home | Return to stored home positions |
| `G54`-`G59`| Work | Select Work Coordinate System |
| `G90`/`G91`| Mode | Absolute vs Relative positioning |
| `G20`/`G21`| Units | Inches vs Millimeters |

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
│ motionstatus     │ wifi status        │ faults                   │
│ stop             │ wifi scan          │ selftest                 │
│ pause / resume   │ wifi connect       │ dio                      │
│ estop on/off     │ eth status         │ telemetry                │
│ limit X 0 1000   │ ping <host>        │ memory / runtime         │
│ job_start <file> │                    │ metrics / log boot       │
├─────────────────┴────────────────────┴──────────────────────────┤
│ CONFIGURATION                                                    │
│ config list        - Show all settings                          │
│ config get <key>   - Read setting                               │
│ config set <k> <v> - Write setting                              │
│ config save        - Persist changes                            │
├──────────────────────────────────────────────────────────────────┤
│ EMERGENCY: estop on    │    REBOOT: reboot                      │
### `log` - Event & Boot Log Viewer
**Description:** Review system events and boot-time messages for troubleshooting connectivity or crash loops.

**Syntax:**
```
log boot      # Dump first 512 bytes of boot sequence
log enable on # Toggle log capture to Flash
```

---

### `ls`, `df`, `cat` - Filesystem Management
**Description:** Direct access to the LittleFS internal storage (where Web UI and logs reside).

**Syntax:**
```
ls            # List files in root directory
df            # Show disk usage (Flash capacity)
cat <file>    # Print content of a specific file
```

---

### `auth` & `web_setpass` - Security
**Syntax:**
```
auth status           # Check session health
web_setpass <secret>  # Update Dashboard password (requires save)
```
```


---

*Document generated for BISSO E350 CNC Controller Firmware v1.1.0 (PosiPro)*

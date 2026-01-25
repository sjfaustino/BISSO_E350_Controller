# 📟 BISSO E350 - THE DEFINITIVE G-CODE MASTER REFERENCE (V1.7) 📟

```text
   ____  ____ ___  ____  _____     ____  _____ _____ 
  / ___|/ ___/ _ \|  _ \| ____|   |  _ \| ____|  ___|
 | |  _| |  | | | | | | |  _|     | |_) |  _| | |_   
 | |_| | |__| |_| | |_| | |___    |  _ <| |___|  _|  
  \____|\____\___/|____/|_____|   |_| \_\_____|_|    
                                                     
          MOTION CONTROL & G-CODE SPECIFICATION
```

## 📜 Overview
Welcome to the absolute "Bible" of G-Code for the BISSO E350 (PosiPro) Controller. This document is a high-verbosity technical manual and educational guide designed to bridge the gap between CNC theory and industrial bridge saw reality.

> [!NOTE]
> Even if you are familiar with standard G-code, please read this reference carefully. The BISSO E350 has unique hardware architectural constraints—such as a **Single Shared VFD** and **Sequential Axis Motion**—that differ from traditional 3-axis CNC mills.

---

### 🎓 Key CNC Concepts Explained
To get the most out of this manual, you must understand these three fundamental concepts:

1.  **Modal vs. Non-Modal**: 
    -   **Modal** commands (like `G0`, `G90`, or `G54`) stay active until changed. If you send `G91` (Incremental), everything will be incremental until you send `G90`.
    -   **Non-Modal** (One-Shot) commands (like `G4` or `G10`) only apply to the line they are written on.
2.  **Machine Coordinates (MCS) vs. Work Coordinates (WCS)**:
    -   **MCS** is the physical ruler of the machine (from the limit switches).
    -   **WCS** is the "Fake" ruler you place on top of your stone slab so that the corner of the stone is exactly `X=0, Y=0`.
3.  **Feedrate (`F`)**: 
    -   The speed at which the machine moves during a cut. In our system, these are mapped to internal PLC speed profiles because we do not have direct analog control over the VFD frequency during a move.

---

### 📖 The 7-Layer Information Standard
Every command in this manual is documented with the following seven sections:
1.  **Command Syntax**: The exact format and spelling required.
2.  **Description**: A simple, clear explanation of what the command does in plain English.
3.  **Parameters**: A list of all letters and numbers you can include.
4.  **How it works**: A deep technical explanation of the internal logic, PLC interaction, and electronic signals.
5.  **Usage Example**: A real-world example of the command being used.
6.  **Expected Output**: What the machine will print back to you to confirm it worked.
7.  **Industrial Use Case**: A scenario explaining why a bridge-saw operator would use this command.

---

## 🏃 MOTION CODES (MODAL GROUP 1)

### `G0` - Rapid Positioning
**Command Syntax:**
`G0 [X<pos>] [Y<pos>] [Z<pos>] [A<pos>]`

**Description:**
`G0` tells the machine to move as fast as possible to a specific location. It is used when the blade is in the air and not touching any stone. Think of it as "Transport Mode."

**Parameters:**
- `X<pos>`: Target coordinate for the Carriage (left/right).
- `Y<pos>`: Target coordinate for the Bridge (forward/back).
- `Z<pos>`: Target coordinate for the Blade Height (up/down).
- `A<pos>`: Target coordinate for the Rotary Head (manual angle).

**How it works:**
1.  **Coordinate Mapping**: The controller takes your coordinates and applies any active "Zero" offsets (WCS).
2.  **Sequential Safety**: Because the Bisso E350 uses a single shared motor drive (Altivar 31 VFD), the axes move one at a time. The controller automatically sequences the moves (Z first, then Y, then X) to ensure the blade is lifted before the bridge moves.
3.  **PLC Speed Selection**: The ESP32 sends a signal to the Siemens PLC to engage the **FAST** speed profile. This is done by triggering the `SPEED_FAST` relay on the PCF8574 expander.
4.  **Encoder Feedback**: The machine monitors the WJ66 encoders at 1000Hz and stops precisely when the target is reached.

**Usage Example:**
`G0 X1500 Y2000`

**Expected Output:**
```text
ok
[MOTION] Moving X to 1500.000... (Profile: FAST)
[MOTION] Moving Y to 2000.000... (Profile: FAST)
ok
```

**Industrial Use Case:**
Moving the blade from the "parking" corner to the start of a slab before beginning a cut. This saves time between jobs.

---

### `G1` - Linear Cutting Move
**Command Syntax:**
`G1 [X<pos>] [Y<pos>] [Z<pos>] [A<pos>] [F<feed>]`

**Description:**
`G1` is the "Precise Cutting" mode. It moves the blade at a controlled speed defined by the `F` value. This is the command used for the actual process of sawing stone.

**Parameters:**
- `X, Y, Z, A`: Target coordinates.
- `F<feed>`: Specified speed in millimeters per minute (mm/min).

**How it works:**
1.  **Feedrate Mapping**: The ESP32 reads the `F` value and compares it to the calibrated speed table.
2.  **Profile Handshake**:
    - If `F` is slow (e.g., 50–500), it selects the **SLOW (V/S)** PLC profile.
    - If `F` is standard (e.g., 500–2000), it selects the **MEDIUM** PLC profile.
3.  **Motor Engagement**: The PLC triggers the motor contactor for the specific axis and keeps the motor energized until the encoder reaches the target.

**Usage Example:**
`G1 Y3000 F1200`

**Expected Output:**
```text
ok
[MOTION] Moving Y to 3000.000... (Profile: MEDIUM)
ok
```

**Industrial Use Case:**
Feeding the bridge forward at a steady 1200 mm/min to cut through a 3cm slab of granite without overheating the blade or stalling the motor.

---

## 📏 DISTANCE MODES (MODAL GROUP 3)

### `G90` - Absolute Positioning Mode
**Command Syntax:**
`G90`

**Description:**
Tells the machine to measure all coordinates from the system "Zero." If you say `X100`, the machine goes to the 100mm mark on its map.

**How it works:**
The firmware sets a global flag called `distanceMode` to `ABSOLUTE`. All subsequent math calculations for axis targets use the literal coordinate relative to the origin.

**Usage Example:**
`G90`

**Expected Output:**
`ok`

**Industrial Use Case:**
Most automated G-code files are written in `G90` so that every point on the slab is a known, fixed location.

---

### `G91` - Incremental Positioning Mode
**Command Syntax:**
`G91`

**Description:**
Tells the machine to treat coordinates as a "distance to add" to the current spot. If you are at 100mm and say `X10`, you will end up at 110mm.

**How it works:**
Sets `distanceMode` to `RELATIVE`. The target calculation becomes `New_Target = Current_Position + User_Request`.

**Usage Example:**
`G91`
`G0 X10`

**Expected Output:**
`ok`

**Industrial Use Case:**
Used when "jogging" or manually fine-tuning the blade position. If you notice the cut is 2mm off, you can send `G91 G0 X-2` to adjust it.

---

## 📍 COORDINATE SYSTEMS (MODAL GROUP 12)

### `G54` to `G59` - Work Coordinate Selection
**Command Syntax:**
`G54` through `G59`

**Description:**
These are "Memory Slots" for different Zero points. You can save the corner of Slab A as `G54` and Slab B as `G55`.

**How it works:**
The ESP32 stores an array of 6 sets of offsets in persistent Flash memory (NVS). When you select a system, it subtracts those offsets from your G-code coordinates to find the true "Machine" position.

**Usage Example:**
`G55`

**Expected Output:**
`[WCS] Selected: G55 (Offset X:100.0 Y:50.0)`

**Industrial Use Case:**
Positioning two slabs on one table and switching between them with a single command without ever having to physically re-zero the machine.

---

## ⚡ UTILITIES (NON-MODAL)

### `G4` - Dwell (Wait)
**Command Syntax:**
`G4 P<ms>` or `G4 S<sec>`

**Description:**
Forces the machine to stop and wait for a count of time.

**Parameters:**
- `P`: Time in milliseconds.
- `S`: Time in seconds.

**How it works:**
The controller enters the `MOTION_DWELL` state and ignores all new commands until the internal timer reaches the target. This does NOT block safety logic—E-Stops still work during a dwell.

**Usage Example:**
`G4 S5` (Wait 5 seconds)

**Expected Output:**
```text
ok
[MOTION] Dwelling for 5000ms...
ok
```

**Industrial Use Case:**
Allowing the water pump to flood the cutting area for 5 seconds before the blade actually starts moving into the stone.

---

### `G10` - Save Offsets
**Command Syntax:**
`G10 L20 P<sys> [X<val>] [Y<val>]`

**Description:**
Tells the machine: "Where you are currently standing, call that coordinate X-Value."

**How it works:**
Calculates the math: `New_Offset = Current_Machine_Pos - Goal_Value`. This is written to the Flash memory immediately so it survives a power-off.

**Usage Example:**
`G10 L20 P1 X0`

**Expected Output:**
`ok`

**Industrial Use Case:**
Bringing the blade to the corner of a slab and declaring it as the "Zero" point ($X=0, Y=0$) for a new job.

---

### `G28` - Homing Sequence
**Command Syntax:**
`G28 [X Y Z A]`

**Description:**
The primary "Find Zero" routine. It searches for the physical metal limit switches to calibrate the machine's internal map.

**How it works:**
1.  **Safety Lift**: Moves Z axis to the upper limit first.
2.  **Sequential Logic**: Homes axes one by one as permitted by the shared PLC contactors.
3.  **Latch**: Once a switch is hit, it records that exact pulse count as "Zero."

**Usage Example:**
`G28`

**Expected Output:**
```text
ok
[MOTION] G28 Homing: X=1 Y=1 Z=1 A=0(manual)
[MOTION] Homing sequence completed
ok
```

**Industrial Use Case:**
Run every morning upon power-up to ensure the machine knows its absolute travel limits.

---

### `G30` - Safe Spots
**Command Syntax:**
`G30 [P0|P1]`

**Description:**
Moves to a predefined "Parking Spot."

**Parameters:**
- `P0`: Go to "Machine Safe" position (Park).
- `P1`: Go to "Tool Change/Clean" position.

**How it works:**
Loads fixed coordinates from the `config_keys.h` definitions and executes a standard `G0` rapid move.

**Usage Example:**
`G30 P1`

**Expected Output:**
`ok`

**Industrial Use Case:**
Retracting the blade to a far corner so the operator can safely clean the table with a hose.

---

### `G53` - Direct Machine Command
**Command Syntax:**
`G53 G0 X100`

**Description:**
Tells the machine: "Ignore my saved Slab Zeros for just one move and use the physical machine ruler."

**How it works:**
Temporarily disables the WCS offset calculation in the parser for a single line of code.

**Usage Example:**
`G53 G0 X0`

**Expected Output:**
`ok`

**Industrial Use Case:**
Programmatically moving the bridge to a "Safe Zone" far away from the workpiece regardless of where the slab origin was set.

---

### `G92` - Coordinate Displacement
**Command Syntax:**
`G92 X0 Y0`

**Description:**
A temporary, non-permanent "Zero" reset.

**How it works:**
Applies a math shift to the parser's reporting engine. Unlike `G10`, these values are often lost when the machine is rebooted.

**Usage Example:**
`G92 X0`

**Expected Output:**
`ok`

**Industrial Use Case:**
Quickly setting a new zero for a small "one-off" cut without disturbing the permanent work coordinates stored in `G54`.

---

## 🔌 CONTROL CODES (M-CODES)

### `M226` - Intelligent Pin Wait
**Command Syntax:**
`M226 P<pin> S<state> [A<type>] [T<timeout>]`

**Description:**
The "Wait for Signal" command. Pauses the machine until a sensor or switch is triggered.

**Parameters:**
- `P<pin>`: The input number (0–7).
- `S<state>`: Wait for ON (1) or OFF (0).
- `A<type>` (Optional): `0` for PLC board, `1` for Controller board.
- `T<timeout>` (Optional): Seconds before giving up (Alarm).

**How it works:**
Enters the `MOTION_WAIT_PIN` state. It performs a loop checking the I2C inputs every 10ms. G-code execution is frozen until the condition is met.

**Usage Example:**
`M226 P4 S1 T10`

**Expected Output:**
```text
ok
[MOTION] Waiting for Pin 4 (State: 1, Interface: PLC)...
[MOTION] Pin condition met. Resuming.
ok
```

**Industrial Use Case:**
Waiting for a "VFD Ready" or "Compressed Air pressure OK" signal before starting the cut.

---

### `M0` / `M1` - Program Stop
**Description:**
Pauses the job. The operator must physically press "Start" to continue.

**Usage Example:**
`M0`

**Expected Output:**
`[PAUSE] Program paused - press resume to continue`

**Industrial Use Case:**
Stopping the machine to allow the operator to change the blade angle or turn the stone slab.

---

### `M2` - Job Finished
**Description:**
Stops all activities and clears all pending moves. 

**How it works:**
Resets the parser state, flushes the motion buffers, and puts the controller back into `IDLE` mode.

---

### `M3` / `M5` - Spindle Management
**Command Syntax:**
`M3` (ON), `M5` (OFF)

**Description:**
Turns the 22kW Saw Blade motor ON or OFF.

**How it works:**
Specifically triggers the PCF8574 output pin labeled `SPEED_FAST` which acts as the Spindle Run signal in the current hardware configuration.

**Usage Example:**
`M3`

**Expected Output:**
`ok`

---

### `M112` - Emergency Halt
**Description:**
Digital E-Stop. Halts EVERYTHING immediately.

**How it works:**
Forcefully kills the `RUN` signal to the PLC and locks the motion engine in an `ALARM` state.

---

### `M114` - Report Position
**Description:**
Asks the machine: "Where are you standing right now?"

**Expected Output:**
`[POS:X:500.125 Y:200.000 Z:-10.000 A:0.000]`

**Industrial Use Case:**
Checking the exact depth of a cut remotely from a PC.

---

### `M115` - Serial Identification
**Description:**
Asks the machine: "Who are you?" 

**Expected Output:**
`[VER:3.5.25 BISSO-E350 CAPABILITY:4-axis,M154]`

---

### `M117` - LCD Message
**Command Syntax:**
`M117 <text>`

**Description:**
Prints a custom message on the 20x4 LCD screen for the operator.

**Usage Example:**
`M117 CUT FINISHED`

---

### `M154` - Automatic Reporting
**Description:**
Starts sending position updates to the computer every second automatically.

**Usage Example:**
`M154 S1`

---

### `M255` - Energy Save
**Description:**
Sets the timer for the LCD backlight to turn off.

**Usage Example:**
`M255 S60` (Sleep after 1 minute)

---

### `M999` - System Resync
**Command Syntax:**
`M999`

**Description:**
The "Fix-It" command. If the parser gets confused or an error message won't go away, use this to reboot the G-code engine.

**How it works:**
1.  **Clear Flags**: Resets the pause, incremental, and machine-coordinate flags.
2.  **Buffer Flush**: Clears the internal string processing buffers.
3.  **State Re-sync**: Polls the motion state to ensure truth between the parser and the motors.

**Usage Example:**
`M999`

**Expected Output:**
`[GCODE] Parser resynced. Ready.`

**Industrial Use Case:**
Recovering from a "Parser Timeout" or a malformed G-code file error without having to cycle the main power switch.

---

## 🕹️ JOGGING & REAL-TIME CONTROLS

### `$` - Grbl Settings Report
**Command Syntax:**
`$`

**Description:**
The "Configuration Inspector." It prints a list of all internal tuning values (like motor scaling and speed limits) using the standard Grbl `$ID=Value` format.

**How it works:**
Queries the NVS system for all keys starting with `ppm_` and `limit_`. It formats them into a numbered list that third-party CNC software can understand.

**Expected Output:**
```text
$100=200.000
$101=200.000
$110=1500.000
...
ok
```

---

### `?` - Real-Time Status Report
**Command Syntax:**
`?` (Single character, no Enter required)

**Description:**
The "Heartbeat" of the machine. It provides a one-line summary of where the machine is and what it is currently doing (Moving, Homing, Alarmed, or Idle).

**How it works:**
This character is intercepted by the Serial Interrupt *instantly*. It bypasses the command buffer. The CPU immediately prints the current state string, Machine Position (MPos), Work Position (WPos), and Buffer status.

**Expected Output:**
`<Run|MPos:100.000,50.000,0.000,0.000|WPos:0.000,0.000,0.000,0.000|Bf:30,127|FS:1200,0>`

---

### `!` - Feed Hold (Pause)
**Command Syntax:**
`!` (Single character)

**Description:**
The "Soft Stop." It tells the machine to stop whatever it is doing immediately, but without losing its place in the program.

**How it works:**
Instantly clears the `RUN` bit to the PLC and freezes the internal motion segment. It preserves the "Distance Remaining" so that you can resume the cut later.

---

### `~` - Cycle Start (Resume)
**Command Syntax:**
`~` (Single character)

**Description:**
The "Go" button. It resumes execution after a `!` hold or an `M0` pause.

---

### `Ctrl-X` - Soft Reset
**Command Syntax:**
`0x18` (Hex character)

**Description:**
The "Panic Button." It stops all motion, clears all buffers, and re-initializes the controller without a full hardware reboot.

---

### `$J=` - Safe Jogging
**Command Syntax:**
`$J=G91 X<dist> F<speed>`

**Description:**
The "manual control" mode. Allows you to move the machine using a joystick or arrow keys in small steps. Unlike `G0/G1`, Jogging moves can be cancelled instantly without leaving the machine in an error state.

**Usage Example:**
`$J=G91 X100 F1500` (Jog X right by 100mm)

**Industrial Use Case:**
Aligning the blade precisely with a chalk mark on the stone slab before starting a cut.

---

## ⚙️ CONFIGURATION SETTINGS ($)

### `$100-$103` - Axis Scaling (Pulses per mm)
**Syntax:** `$100=Value` (X), `$101=Value` (Y), `$102=Value` (Z), `$103=Value` (A)

**Description:**
The "Ruler Calibration." Tells the controller how many electrical clicks from the encoder equal one millimeter of movement.

**How it works:**
The motion engine uses this as a multiplication factor. If `$100=200`, and you ask for 1mm, the engine waits for 200 pulses from the WJ66 interface.

---

### `$110-$113` - Speed Calibration
**Syntax:** `$110=Value` (X), `$111=Value` (Y), `$112=Value` (Z), `$113=Value` (A)

**Description:**
Records the "Real World" speed of the FAST speed profile in mm/min. Used to ensure the time-remaining estimates are accurate.

---

### `$120` - Default Acceleration
**Description:**
Sets how "aggressively" the internal software calculates the lead-in to a move. 

---

### `$130-$132` - Hard Workspace Limits
**Syntax:** `$130=Value` (X), `$131=Value` (Y), `$132=Value` (Z)

**Description:**
Defines the "End of the Rails." If the machine travels beyond these numbers, it will trigger an immediate emergency stop to prevent physical damage.

---

**Version:** 1.7 Ultimate Master  
**Author:** Antigravity (DeepMind Advanced Agentic Coding)  
**Machine:** BISSO E350 PosiPro CNC  
**Source Truth:** firmware:v3.5.25 / cli_base.cpp / gcode_parser.cpp

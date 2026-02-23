# 📟 BISSO E350 - THE DEFINITIVE G-CODE MASTER REFERENCE (V3.0) 📟

```text
   ____  ____  ___  ____  _____     ____  _____ _____ 
  / ___|/ ___/ _ \|  _ \| ____|   |  _ \| ____|  ___|
 | |  _| |  | | | | | | |  _|     | |_) |  _| | |_   
 | |_| | |__| |_| | |_| | |___    |  _ <| |___|  _|  
  \____|\____\___/|____/|_____|   |_| \_\_____|_|    
                                                     
          MOTION CONTROL & G-CODE SPECIFICATION
          =======================================
          4-AXIS SEQUENTIAL BRIDGE SAW CONTROLLER
```

---

## 📜 Overview

Welcome to the **DEFINITIVE** G-Code reference for the BISSO E350 (PosiPro) CNC Controller. This document is a high-verbosity technical manual and educational guide designed to bridge the gap between CNC theory and industrial bridge saw reality.

> [!IMPORTANT]
> Even if you are familiar with standard G-code, please read this reference carefully. The BISSO E350 has unique hardware architectural constraints—such as a **Single Shared VFD** and **Sequential Axis Motion**—that differ from traditional 3-axis CNC mills or routers.

---

## 🏗️ Hardware Architecture & Constraints

Understanding the BISSO E350 hardware is **CRITICAL** for effective G-code programming:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    BISSO E350 SYSTEM ARCHITECTURE                    │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   ┌─────────────┐     RS-485      ┌─────────────────────────────┐   │
│   │   ESP32     │─────────────────│  SIEMENS S7-200 PLC        │   │
│   │  Controller │                 │  (Motion Sequencer)         │   │
│   └─────────────┘                 └───────────┬─────────────────┘   │
│          │                                    │                      │
│          │ I2C                         Contactor Control             │
│          │                                    │                      │
│   ┌──────┴──────┐                    ┌────────┴─────────┐           │
│   │  PCF8574    │                    │ DUAL ALTIVAR 31  │           │
│   │  I/O Expdr  │                    │   VFD SYSTEM     │           │
│   └─────────────┘                    └────────┬─────────┘           │
│                                               │                      │
│                              ┌────────────────┼────────────────┐    │
│                              │                │                │    │
│                         ┌────┴────┐      ┌────┴────┐     ┌─────┴──┐ │
│                         │ X MOTOR │      │ Y MOTOR │     │Z MOTOR │ │
│                         │(Carriage)│      │(Bridge) │     │(Blade) │ │
│                         └─────────┘      └─────────┘     └────────┘ │
│                                                                      │
│            ★ DUAL VFD: Independent Spindle and Axis Control ★         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Hardware Constraints

| Constraint | Description | Impact on G-Code |
|------------|-------------|------------------|
| **Dual VFD** | Independent VFDs for Spindle and Axis motors | Spindle can run during all motions |
| **PLC Contactors** | Motor selection happens in PLC, not ESP32 | Automatic sequencing for standard moves |
| **Coordinated X/Y**| Supported via Dual-Driver Logic | **G2/G3 Arcs** and **M402** movement |
| **22kW Spindle** | Dedicated VFD (VFD1) | M3/M5/M113 controls spindle |
| **A-Axis Manual** | Rotary head has no motor; encoder-only | G28 A is skipped; manual positioning only |
| **WJ66 Encoders** | Hall-effect pulse counting at 1000 Hz | High-precision positioning (0.01mm) |

---

## 🎓 Key CNC Concepts Explained

Before diving into commands, understand these fundamental concepts:

### 1. Modal vs. Non-Modal Commands

```
MODAL (Sticky) Commands:             NON-MODAL (One-Shot) Commands:
┌──────────────────────────────┐     ┌──────────────────────────────┐
│ G0  - Rapid mode             │     │ G4  - Dwell (wait)           │
│ G1  - Linear feed mode       │     │ G10 - Save offsets           │
│ G90 - Absolute coordinates   │     │ G28 - Home cycle             │
│ G91 - Incremental coords     │     │ G30 - Go to preset           │
│ G54-G59 - Work coord select  │     │ G53 - Machine coords move    │
└──────────────────────────────┘     └──────────────────────────────┘
        ↓                                      ↓
  Stays active until changed!       Only affects the current line!
```

### 2. Machine Coordinates (MCS) vs Work Coordinates (WCS)

```
                         MACHINE ORIGIN (0,0)
                              ↓
    ┌─────────────────────────★─────────────────────────────────┐
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ MACHINE TABLE ▓▓▓▓▓▓▓▓│
    │                                                           │
    │                    SLAB CORNER → ☆ ← YOUR WORK ORIGIN     │
    │                              ┌─────────────────┐          │
    │   G54 offset = 500mm ───────>│    GRANITE     │          │
    │                              │     SLAB       │          │
    │                              │                │          │
    │                              └─────────────────┘          │
    │                                                           │
    └───────────────────────────────────────────────────────────┘
    
    G-Code says: G0 X100 Y50
    
    With G54 selected:
    - Work Position:    X=100,  Y=50   ← What you see in UI
    - Machine Position: X=600, Y=150   ← Where machine actually is
          (100 + 500 offset)   (50 + 100 offset)
```

### 3. Feedrate (F Value)

The `F` value specifies cutting speed in **mm/min**:

```
F value → PLC Speed Profile Mapping:

  F50-500 mm/min  → SLOW Profile  (V/S - Vari-Speed, precise cuts)
  F500-2000       → MEDIUM Profile (Standard cutting)
  F2000+          → FAST Profile  (Rapid traverse, blade in air)
  
  Note: The PLC has fixed speed profiles. The F value maps to the 
  closest available profile, not infinitely variable speed.
```

---

## 🏃 MOTION CODES (MODAL GROUP 1)

---

### `G0` - Rapid Positioning

**Command Syntax:**
```gcode
G0 [X<pos>] [Y<pos>] [Z<pos>] [A<pos>]
```

**Description:**
`G0` tells the machine to move as **fast as possible** to a specific location. Used when the blade is **NOT** in contact with material—think "Transport Mode" or "Aircut Movement."

**Parameters:**
| Parameter | Description | Valid Range | Default |
|-----------|-------------|-------------|---------|
| `X<pos>` | Carriage position (left/right) | -500 to 5000 mm | Current X |
| `Y<pos>` | Bridge position (forward/back) | -500 to 3500 mm | Current Y |
| `Z<pos>` | Blade height (up/down) | 0 to 1000 mm | Current Z |
| `A<pos>` | Rotary head angle (if encoder-equipped) | 0 to 90° | Current A |

**How It Works (Internal Mechanics):**

```
Step 1: COORDINATE RESOLUTION
┌─────────────────────────────────────────────────────────────┐
│ User sends: G0 X1500 Y2000                                   │
│                                                              │
│ Parser applies WCS offset (if G54-G59 active):               │
│   Machine_X = User_X + G54_Offset_X                          │
│   Machine_Y = User_Y + G54_Offset_Y                          │
│                                                              │
│ If G91 (incremental): Machine_X = Current_X + User_X         │
└─────────────────────────────────────────────────────────────┘
                              ↓
Step 2: SAFETY SEQUENCING
┌─────────────────────────────────────────────────────────────┐
│ Order of movement for SAFETY (blade must lift first):        │
│                                                              │
│   1. Z-axis raise (if Z target is HIGHER than current)       │
│   2. Y-axis move (after Z is safe)                           │
│   3. X-axis move (after Y is complete)                       │
│   4. Z-axis lower (if Z target is LOWER than current)        │
│                                                              │
│ This prevents the blade from crashing into the stone!        │
└─────────────────────────────────────────────────────────────┘
                              ↓
Step 3: PLC SPEED SELECTION
┌─────────────────────────────────────────────────────────────┐
│ ESP32 sends command to PLC via I2C:                          │
│   - Assert SPEED_FAST relay (maximum traverse speed)         │
│   - Assert direction relay (FWD or REV)                      │
│   - Assert AXIS_SELECT relay (X, Y, or Z)                    │
│   - Assert RUN signal                                        │
└─────────────────────────────────────────────────────────────┘
                              ↓
Step 4: ENCODER FEEDBACK LOOP
┌─────────────────────────────────────────────────────────────┐
│ WJ66 encoder provides pulse feedback at 1000 Hz:             │
│                                                              │
│   Current_Pulses = Read_Encoder(axis)                        │
│   Current_MM = Current_Pulses / $100  (pulses per mm)        │
│                                                              │
│   When |Target_MM - Current_MM| < 0.01 mm:                   │
│     → Clear RUN signal                                       │
│     → Report "ok"                                            │
└─────────────────────────────────────────────────────────────┘
```

**Usage Examples:**

```gcode
; Example 1: Move to absolute position
G90          ; Ensure absolute coordinates
G0 X1500     ; Move X to 1500mm

; Example 2: Multi-axis rapid
G0 X1500 Y2000 Z50   ; Move to position (sequential: Z→Y→X)

; Example 3: Incremental rapid
G91          ; Switch to incremental
G0 X100      ; Move 100mm RIGHT from current position
G90          ; Switch back to absolute

; Example 4: Return to start
G0 X0 Y0     ; Go to work coordinate origin
```

**Expected Console Output:**
```text
ok
[MOTION] G0 Rapid: X=1500.000 Y=2000.000 (Fast Profile)
[MOTION] Sequence: Z↑ → Y → X
ok
```

**Possible Errors & Solutions:**

| Error | Meaning | Solution |
|-------|---------|----------|
| `error:9` | Limit switch triggered | Home the machine (`$H`), clear alarm |
| `error:3` | Motion rejected (busy) | Wait for current move to complete |
| `error:22` | Soft limit exceeded | Reduce target value within `$130-$132` limits |
| `[ESTOP]` | Emergency stop active | Release physical E-Stop button, reset with `M999` |

**Industrial Use Cases:**
- Moving blade from garage position to slab start before cutting
- Repositioning between different slabs on the table
- Returning to home position after job completion

---

### `G1` - Linear Cutting Move (Controlled Feed)

**Command Syntax:**
```gcode
G1 [X<pos>] [Y<pos>] [Z<pos>] [A<pos>] [F<feed>]
```

**Description:**
`G1` is the **precision cutting** command. It moves the blade at a controlled feedrate specified by `F`, used when the blade is **IN CONTACT** with stone.

**Parameters:**
| Parameter | Description | Valid Range | Default |
|-----------|-------------|-------------|---------|
| `X<pos>` | Target X position | Machine limits | Current X |
| `Y<pos>` | Target Y position | Machine limits | Current Y |
| `Z<pos>` | Target Z position | Machine limits | Current Z |
| `A<pos>` | Target A position | 0-90° | Current A |
| `F<feed>` | Cutting speed (mm/min) | 10-5000 | Last F value (modal) |

**How It Works:**

```
FEEDRATE → PLC PROFILE MAPPING:
┌────────────────────────────────────────────────────────────────┐
│                                                                 │
│    F value (mm/min)         PLC Speed Profile                  │
│    ────────────────         ─────────────────                  │
│    10 - 499                 SLOW (V/S Motor Start)             │
│    500 - 1999               MEDIUM (Standard Cutting)          │
│    2000+                    FAST (Rapid Traverse)              │
│                                                                 │
│    NOTE: F is MODAL - once set, it applies to all G1 commands  │
│          until changed!                                         │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

### `G2 / G3` - Circular / Arc Motion

**Command Syntax:**
```gcode
G2 [X<target>] [Y<target>] [I<offset>] [J<offset>] [F<feed>]  ; Clockwise Arc
G2 [X<target>] [Y<target>] [R<radius>] [F<feed>]              ; Clockwise Arc (Radius)
G3 [X<target>] [Y<target>] [I<offset>] [J<offset>] [F<feed>]  ; Counter-Clockwise Arc
G3 [X<target>] [Y<target>] [R<radius>] [F<feed>]              ; Counter-Clockwise (Radius)
```

**Description:**
`G2` and `G3` perform coordinated circular interpolation between X and Y axes. This requires the **Dual VFD** hardware where the ESP32 can control both X and Y drivers simultaneously through the software-based Coordinated Motion Driver.

**Parameters:**
| Parameter | Description | Valid Range | Default |
|-----------|-------------|-------------|---------|
| `X<target>`| Target X coordinate | Machine limits | Current X |
| `Y<target>`| Target Y coordinate | Machine limits | Current Y |
| `I<offset>`| X distance from start to center (Incremental) | Millimeters | 0 |
| `J<offset>`| Y distance from start to center (Incremental) | Millimeters | 0 |
| `R<radius>`| Arc radius (Positive = Small arc, Negative = Large arc) | Millimeters | N/A |
| `F<feed>`  | Cutting speed | 10-5000 | Last F |

**How It Works:**
The controller uses **Arc Decomposition**. It breaks the geometric arc into many tiny linear segments (G1 segments) based on `$12` (Arc Tolerance). These segments are then executed at the requested feedrate, driving both X and Y VFDs in sync.

**Usage Examples:**
```gcode
; Example 1: 90-degree corner clockwise using I/J
G90 X0 Y0
G2 X100 Y100 I100 J0 F800 ; Arc starts at 0,0 ends at 100,100, center is 100,0

; Example 2: Semi-circle using Radius
G3 X200 Y0 R100 F1000      ; CCW semi-circle with 100mm radius
```

**Industrial Use Case:**
- Cutting sink cutouts with rounded corners.
- Profiling slab edges with custom radii.
- Precise circular holes (using two semi-arcs).

---

### `M402` - Coordinated Motion Mode Control

**Command Syntax:**
```gcode
M402 P<mode>
```

**Description:**
Enables or disables **Coordinated Motion Mode**. This is a safety and functional toggle that determines how the machine handles multi-axis motion commands.

**Parameters:**
| Parameter | Description | Valid Range |
|-----------|-------------|-------------|
| `P0`      | **Disable** Coordinated Mode (Default) |
| `P1`      | **Enable** Coordinated Mode |

**How It Works:**
1. **P0 (Sequential Mode)**: For safety, standard G1 moves involving multiple axes (e.g., `G1 X100 Y100`) are split into sequential moves (X then Y). This minimizes the risk of diagonal crashes if the operator is not expecting simultaneous movement.
2. **P1 (Coordinated Mode)**: Allows simultaneous X and Y motion. This is **REQUIRED** for diagonal cuts and is automatically enabled/disabled internally by arc commands (`G2`/`G3`).

**Usage Examples:**
```gcode
; Example: Perform a diagonal move
M402 P1         ; Enable coordinated mode
G1 X100 Y100 F800 ; Both axes move together
M402 P0         ; Return to safe sequential mode
```

**Expected Console Output:**
```text
[DRV] Coordinated Motion Mode: ENABLED
ok
```

> [!WARNING]
> Use `M402 P1` with caution. Ensure the path is clear of obstructions as the machine will move in a straight diagonal line.

---

### `M403` - VFD Frequency Control (Hz)

**Command Syntax:**
```gcode
M403 S<hertz> P<vfd_id>
```

**Description:**
Sets the output frequency of a specific VFD directly in Hertz. This allows for precise manual speed control bypassing the standard feedrate-to-frequency mapping.

**Parameters:**
| Parameter | Description | Valid Range | Default |
|-----------|-------------|-------------|---------|
| `S<hertz>` | Target frequency in Hz | 0.0 to 100.0 | N/A |
| `P<vfd_id>`| VFD Identifier | 1 (Spindle), 2 (X), 4 (YZA) | 2 (X) |

**How It Works:**
The value is sent directly to the VFD's internal frequency reference register via Modbus. This will override any existing motion-based speed calculations until the next `G1` command with an `F` value occurs.

**Usage Examples:**
```gcode
; Example: Run X VFD at exactly 35.5 Hz
M403 S35.5 P2
```

**Note:** Frequencies are still subject to the `LSP` (Low Speed) and `HSP` (High Speed) limits programmed into the VFD firmware for hardware protection.

---

**Usage Examples:**

```gcode
; Example 1: Simple cutting pass
G1 Y3000 F800    ; Feed Y at 800 mm/min (medium speed)

; Example 2: Z plunge into material
G1 Z-50 F100     ; Slow plunge at 100 mm/min

; Example 3: Multiple moves (F is sticky)
G1 Y1000 F1200   ; Set feed to 1200, move Y
G1 Y2000         ; Still at F1200 (modal)
G1 Y3000 F500    ; Change to slower feed

; Example 4: Cutting sequence
G0 Z5            ; Rapid to safe Z height
G0 X100 Y0       ; Position at cut start
G1 Z-30 F50      ; Plunge into stone slowly
G1 Y3000 F1000   ; Make the cut!
G0 Z5            ; Retract blade
```

**Expected Console Output:**
```text
ok
[MOTION] G1 Linear: Y=3000.000 F=1000.000 (Medium Profile)
ok
```

**Industrial Use Case:**
Feeding the bridge forward at a steady 1200 mm/min to cut through a 3cm slab of granite without overheating the blade or stalling the motor.

---

## 📏 DISTANCE MODES (MODAL GROUP 3)

---

### `G90` - Absolute Positioning Mode

**Command Syntax:**
```gcode
G90
```

**Description:**
Sets **absolute** coordinate mode. All subsequent coordinates are measured from the **work coordinate origin** (defined by G54-G59).

**How It Works:**
```
ABSOLUTE MODE (G90):
                    
    Work Origin (0,0,0)
           ↓
           ★───────────────────────────→ +X
           │
           │  G0 X100 means: "Go to 100mm on the X ruler"
           │       ↓
           │   ┌───●───┐
           │   │Target │
           ↓   └───────┘
          +Y
          
    Current position: X=50
    Command: G0 X100
    Result: Machine moves to X=100 (NOT X=150!)
```

**Usage Example:**
```gcode
G90              ; Absolute mode
G0 X100 Y200     ; Go to position (100, 200)
G0 X200 Y300     ; Go to position (200, 300) - NOT (100+200, 200+300)
```

**Expected Output:**
```text
ok
```

**Industrial Use Case:**
Most G-code files generated by CAM software use G90 (absolute) because every point on the slab is defined as a known location on the coordinate grid.

---

### `G91` - Incremental Positioning Mode

**Command Syntax:**
```gcode
G91
```

**Description:**
Sets **incremental** coordinate mode. All coordinates are treated as **distances to add** to the current position.

**How It Works:**
```
INCREMENTAL MODE (G91):

    Current Position
           ↓
           ●═══════════════→ +X
           ║                ↑
           ║     G0 X50 means: "Move 50mm from HERE"
           ║                ↓
           ║            ┌───●───┐
           ║            │New Pos│
           ↓            └───────┘
          +Y
          
    Current position: X=100
    Command: G0 X50
    Result: Machine moves to X=150 (100 + 50)
```

**Usage Example:**
```gcode
G91              ; Incremental mode
G0 X10           ; Move 10mm to the right
G0 X10           ; Move another 10mm (now at original+20)
G0 X-5           ; Move 5mm LEFT
G90              ; Return to absolute mode
```

**Industrial Use Case:**
- Manual jogging: "Move 2mm to the right to align with the chalk mark"
- Repeated patterns: "Make 5 cuts, each 100mm apart"
- Fine adjustment: "The cut is 1.5mm off—nudge it over"

---

## 📍 COORDINATE SYSTEMS (MODAL GROUP 12)

---

### `G54` through `G59` - Work Coordinate System Selection

**Command Syntax:**
```gcode
G54    ; Select Work Coordinate System 1 (default)
G55    ; Select Work Coordinate System 2
G56    ; Select Work Coordinate System 3
G57    ; Select Work Coordinate System 4
G58    ; Select Work Coordinate System 5
G59    ; Select Work Coordinate System 6
```

**Description:**
These commands select one of **six** stored work coordinate systems. Each WCS stores X, Y, Z, and A offsets, allowing you to define multiple "Zero" points for different slabs or fixtures.

**How It Works:**
```
WORK COORDINATE SYSTEMS:
┌──────────────────────────────────────────────────────────────────┐
│                       MACHINE TABLE                               │
│                                                                   │
│   Machine (0,0)                                                   │
│        ★─────────────────────────────────────────────────────→   │
│        │                                                          │
│        │    G54 ☆ (Offset: X=500, Y=100)                         │
│        │         ┌──────────────┐                                │
│        │         │   SLAB #1    │                                │
│        │         └──────────────┘                                │
│        │                                                          │
│        │              G55 ☆ (Offset: X=1800, Y=100)              │
│        │                   ┌──────────────┐                      │
│        │                   │   SLAB #2    │                      │
│        │                   └──────────────┘                      │
│        ↓                                                          │
└──────────────────────────────────────────────────────────────────┘

With G54 active: G0 X0 Y0 → Machine goes to (500, 100)
With G55 active: G0 X0 Y0 → Machine goes to (1800, 100)
```

**Usage Example:**
```gcode
G54              ; Select first slab origin
G0 X0 Y0         ; Move to slab 1 corner
G1 Y1000 F800    ; Cut slab 1

G55              ; Select second slab origin
G0 X0 Y0         ; Move to slab 2 corner (different physical location!)
G1 Y1000 F800    ; Cut slab 2
```

**Expected Output:**
```text
[WCS] Selected: G55 (Offset X:1800.0 Y:100.0)
ok
```

**Industrial Use Case:**
When you have multiple slabs on the table, set up G54 for slab 1, G55 for slab 2, etc. You can then run the same G-code file on each slab by simply changing the WCS selector.

---

### `G10 L20` - Set Work Coordinate Offset

**Command Syntax:**
```gcode
G10 L20 P<system> [X<val>] [Y<val>] [Z<val>] [A<val>]
```

**Description:**
Sets the work coordinate offset so that the **current machine position** becomes the specified value. "Where I am standing right now—call it X=0, Y=0."

**Parameters:**
| Parameter | Description | Valid Values |
|-----------|-------------|--------------|
| `L20` | Required—L-word specifying "set offset" mode | Must be 20 |
| `P<system>` | WCS system number | 1-6 (G54-G59) |
| `X<val>` | What X should read at current position | Any number |
| `Y<val>` | What Y should read at current position | Any number |
| `Z<val>` | What Z should read at current position | Any number |
| `A<val>` | What A should read at current position | Any number |

**How It Works:**
```
SETTING THE ZERO POINT:

  Step 1: Physically jog machine to the CORNER of your slab
  
  Step 2: Send G10 L20 P1 X0 Y0
  
  Step 3: Math that happens internally:
          ┌────────────────────────────────────────────────────┐
          │ Current Machine Position = (1523.456, 847.123)    │
          │ You want this to read as  = (0, 0)                │
          │                                                    │
          │ New G54 Offset = Current - Desired                │
          │                = (1523.456 - 0, 847.123 - 0)      │
          │                = (1523.456, 847.123)              │
          │                                                    │
          │ Offset is saved to NVS Flash (survives reboot!)    │
          └────────────────────────────────────────────────────┘
          
  Step 4: Now G0 X0 Y0 will always return to that corner!
```

**Usage Examples:**
```gcode
; Zero the current position in G54
G10 L20 P1 X0 Y0

; Set current position as X=100 in G55
G10 L20 P2 X100

; Set all axes (rare, but possible)
G10 L20 P1 X0 Y0 Z0 A0
```

**Expected Output:**
```text
[GCODE] Updated G54 Offsets
ok
```

**Industrial Use Case:**
Operator brings the blade directly over the corner of a new slab, then commands `G10 L20 P1 X0 Y0` to establish that point as the origin for all subsequent cuts.

---

## ⚡ UTILITIES (NON-MODAL)

---

### `G4` - Dwell (Pause/Wait)

**Command Syntax:**
```gcode
G4 P<milliseconds>
G4 S<seconds>
```

**Description:**
Pauses the machine for a specified time. The spindle continues running and coolant stays on—only axis motion pauses.

**Parameters:**
| Parameter | Description | Valid Range |
|-----------|-------------|-------------|
| `P<ms>` | Dwell time in **milliseconds** | 1 - 600000 (10 min) |
| `S<sec>` | Dwell time in **seconds** | 0.1 - 600 |

> [!NOTE]
> If both P and S are specified, S (seconds) takes precedence.

**How It Works:**
```
DWELL STATE MACHINE:
┌──────────────────────────────────────────────────────────────┐
│                                                               │
│   1. Parser receives G4 S5                                    │
│   2. Motion controller enters DWELL state                     │
│   3. Timer starts counting                                    │
│   4. All G-code processing PAUSES (queued commands wait)      │
│   5. SAFETY STILL ACTIVE! E-Stop will still trigger!          │
│   6. After 5 seconds, exit DWELL → "ok"                       │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

**Usage Examples:**
```gcode
; Wait for 5 seconds (let water flood the cut area)
G4 S5

; Wait for 500 milliseconds (half second pause between cuts)
G4 P500

; 10-second pause for blade change
M0                   ; Optional: M0 pauses indefinitely
; OR
G4 S10              ; Fixed 10-second pause
```

**Expected Output:**
```text
ok
[MOTION] Dwell: 5000 ms
ok
```

**Industrial Use Case:**
- Allowing the water pump to flood the cutting area before the blade enters the stone
- Brief pause after plunge to let blade reach full RPM
- Timing delay in automated sequences

---

### `G28` - Homing Sequence

**Command Syntax:**
```gcode
G28 [X] [Y] [Z] [A]
```

**Description:**
Runs the automatic homing sequence to find the **physical limit switches** and establish the machine coordinate origin. This is how the machine "knows" where it is.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| No params | Home ALL axes (Z→Y→X) |
| `X` | Home X axis only |
| `Y` | Home Y axis only |
| `Z` | Home Z axis only |
| `A` | **Skipped** - A-axis has no motor |

**How It Works:**
```
HOMING SEQUENCE (G28 with no parameters):
┌────────────────────────────────────────────────────────────────────┐
│                                                                     │
│  PHASE 1: Z-AXIS HOMING (CRITICAL - LIFT BLADE FIRST!)             │
│  ═══════════════════════════════════════════════════════════════   │
│     1. Move Z in positive direction (UP) at SLOW speed             │
│     2. When Z limit switch triggers:                                │
│        → Stop motor immediately                                     │
│        → Record encoder position as Z_MAX                           │
│        → Set Z machine position = $132 (Z max travel)               │
│                                                                     │
│  PHASE 2: Y-AXIS HOMING                                             │
│  ═══════════════════════════════════════════════════════════════   │
│     1. Move Y toward home switch (configurable direction)           │
│     2. On switch trigger: set Y = 0                                 │
│                                                                     │
│  PHASE 3: X-AXIS HOMING                                             │
│  ═══════════════════════════════════════════════════════════════   │
│     1. Move X toward home switch                                    │
│     2. On switch trigger: set X = 0                                 │
│                                                                     │
│  NOTE: A-axis is SKIPPED - operator must manually position          │
│                                                                     │
└────────────────────────────────────────────────────────────────────┘
```

**Usage Examples:**
```gcode
; Home all axes (recommended at power-up)
G28

; Home only Z (useful after E-stop recovery)
G28 Z

; Home X and Y (leave Z where it is)
G28 X Y
```

**Expected Output:**
```text
ok
[MOTION] G28 Homing: X=1 Y=1 Z=1 A=0(manual)
[MOTION] Homing Z...
[MOTION] Z home found at 952.500 mm
[MOTION] Homing Y...
[MOTION] Y home found at 0.000 mm
[MOTION] Homing X...
[MOTION] X home found at 0.000 mm
[MOTION] Homing sequence completed successfully
ok
```

**Possible Errors:**

| Error | Meaning | Solution |
|-------|---------|----------|
| `G28 homing disabled` | `home_enable` config is off | Set `config set home_enable 1` |
| `Homing failed - timeout` | Limit switch not triggered in time | Check switch wiring, increase timeout |
| `A homing skipped` | Normal - A-axis has no motor | Position A manually |

**Industrial Use Case:**
Run `G28` every morning at power-up to ensure the machine knows its absolute position. This is required before any automatic cutting operations.

---

### `G30` - Go to Predefined Position

**Command Syntax:**
```gcode
G30 [P<id>]
```

**Description:**
Moves to a **predefined "parking" position** stored in configuration. Useful for tool changes, cleaning, or end-of-job parking.

**Parameters:**
| Parameter | Description | Valid Values |
|-----------|-------------|--------------|
| `P0` or none | Safe Position (blade parking) | Default |
| `P1` | Predefined Position 1 (tool change) | Configurable |

**How It Works:**
```
PREDEFINED POSITIONS (stored in NVS config):
┌────────────────────────────────────────────────────────────────┐
│                                                                 │
│  P0 (Safe Position):                                            │
│      KEY_POS_SAFE_X, KEY_POS_SAFE_Y, KEY_POS_SAFE_Z, _A         │
│      Typical: X=0, Y=0, Z=500 (blade fully raised, at corner)   │
│                                                                 │
│  P1 (Tool Change Position):                                     │
│      KEY_POS_1_X, KEY_POS_1_Y, KEY_POS_1_Z, _A                  │
│      Typical: X=1500, Y=1500, Z=500 (center of table, high)     │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

**Usage Example:**
```gcode
; Park at safe position (after job completion)
G30

; Go to tool change position
G30 P1
```

**Expected Output:**
```text
[GCODE] G30 Go to Safe Position: X:0.0 Y:0.0 Z:500.0 A:0.0
ok
```

**Industrial Use Case:**
At the end of every job, send `G30` to park the blade in the safe corner so the operator can hose down the table without hitting the machine.

---

### `G53` - Machine Coordinates Mode (One-Shot)

**Command Syntax:**
```gcode
G53 G0 X<pos> Y<pos> Z<pos>
G53 G1 X<pos> Y<pos> Z<pos> F<feed>
```

**Description:**
Temporarily **ignores all WCS offsets** for a single move, using raw machine coordinates. The next command returns to normal WCS mode.

**How It Works:**
```
MACHINE COORDINATES BYPASS:
┌────────────────────────────────────────────────────────────────┐
│                                                                 │
│  Normal:     G0 X100 → Move to Work_X=100 + WCS_Offset         │
│                                                                 │
│  With G53:   G53 G0 X100 → Move to Machine_X=100 (no offset!)  │
│                                                                 │
│  ONLY affects the command on the SAME LINE as G53!              │
│  Next command returns to normal WCS mode.                       │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

**Usage Example:**
```gcode
G54                    ; Normal work coordinates
G0 X0 Y0               ; Go to work origin (with offset)
G53 G0 X0 Y0           ; Go to MACHINE origin (ignoring offset!)
G0 X0 Y0               ; Back to work origin (WCS restored)
```

**Industrial Use Case:**
Moving to the machine's physical corner regardless of which slab is on the table—useful for maintenance or recalibration.

---

### `G92` - Set Position (Coordinate Shift)

**Command Syntax:**
```gcode
G92 [X<val>] [Y<val>] [Z<val>] [A<val>]
```

**Description:**
Sets the **current position** to the specified values WITHOUT moving. This is a temporary, session-based zero that may be lost on reboot (unlike G10 L20).

> [!WARNING]
> G92 offsets are additive to WCS offsets and can cause confusion. Prefer `G10 L20` for permanent offset changes.

**Usage Example:**
```gcode
; Set current position as X=0 (temporary)
G92 X0

; Set all axes to origin
G92 X0 Y0 Z0 A0
```

**Industrial Use Case:**
Quick, temporary zero for a single job—used when you don't want to disturb the permanent G54 offset.

---

## 🔌 MACHINE CONTROL CODES (M-CODES)

---

### `M0` / `M1` - Program Stop (Pause)

**Command Syntax:**
```gcode
M0    ; Mandatory stop - always pauses
M1    ; Optional stop - pauses unless "skip optional stops" is enabled
```

**Description:**
Pauses the job. The operator must **press the physical Resume button** or send `~` to continue.

**How It Works:**
```
PAUSE STATE MACHINE:
┌────────────────────────────────────────────────────────────────┐
│                                                                 │
│   1. M0/M1 received                                             │
│   2. Motion controller enters PAUSED state                      │
│   3. All axes stop (decelerate gracefully)                      │
│   4. LCD displays: "PAUSED: Resume?"                            │
│   5. Controller WAITS for:                                       │
│      - Physical Resume button OR                                 │
│      - Serial '~' character OR                                   │
│      - Web UI "Resume" button                                    │
│   6. On resume: continue from exact pause point                  │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

**Expected Output:**
```text
[PAUSE] Program paused - press resume to continue
```

**Industrial Use Case:**
- Blade change during a long job
- Material inspection before continuing
- Operator coffee break 😄

---

### `M2` - Program End

**Command Syntax:**
```gcode
M2
```

**Description:**
Signals the **end of the G-code program**. Clears all pending motion, resets parser state, returns to idle.

**How It Works:**
```
PROGRAM END SEQUENCE:
┌────────────────────────────────────────────────────────────────┐
│  1. Complete any queued moves                                      │
│  2. Stop spindle (implicit M5)                                     │
│  3. Reset parser modal states to defaults                         │
│  4. Clear motion planner buffer                                   │
│  5. Set machine state to IDLE                                     │
│  6. Ready for next program                                        │
└────────────────────────────────────────────────────────────────┘
```

**Industrial Use Case:**
Always place `M2` at the end of your G-code files to signal proper program termination.

---

### `M3` / `M5` - Spindle Control

**Command Syntax:**
```gcode
M3    ; Spindle ON (blade motor start)
M5    ; Spindle OFF (blade motor stop)
```

**Description:**
Controls the 22kW saw blade motor through the VFD.

> [!CAUTION]
> On the BISSO E350, the spindle and axis motors share the same VFD. M3 enables "speed mode" but the spindle typically runs only when axes are NOT moving. The PLC handles this interlock.

**How It Works:**
- `M3` sets the `SPEED_1` relay on the PCF8574 I/O expander
- The PLC interprets this as "spindle run request"
- Actual motor start depends on VFD ready signal

---

### `M112` - Emergency Stop (Software E-Stop)

**Command Syntax:**
```gcode
M112
```

**Description:**
Triggers an **immediate software emergency stop**. All motion halts, system enters ALARM state.

**How It Works:**
```
SOFTWARE E-STOP SEQUENCE:
┌────────────────────────────────────────────────────────────────┐
│  1. IMMEDIATE motor disable (all axes)                            │
│  2. VFD stop command sent                                         │
│  3. Set ALARM state flag                                          │
│  4. Clear motion planner queue                                    │
│  5. Log event to fault history                                    │
│  6. Require M999 or power cycle to clear                          │
└────────────────────────────────────────────────────────────────┘
```

> [!WARNING]
> This is equivalent to pressing the physical E-Stop button. Machine must be reset with `M999` or power cycle before continuing.

---

### `M114` - Report Position

**Command Syntax:**
```gcode
M114
```

**Description:**
Reports the current position of all axes in both work and machine coordinates.

**How It Works:**
```
POSITION REPORT:
┌────────────────────────────────────────────────────────────────┐
│  1. Read current encoder counts from WJ66                         │
│  2. Convert to mm using $100/$101/$102/$103 scale                 │
│  3. Calculate Work Position = Machine Position - WCS Offset       │
│  4. Format and output to serial                                   │
└────────────────────────────────────────────────────────────────┘
```

**Expected Output:**
```text
[POS:X:1234.567 Y:890.123 Z:50.000 A:0.000]
```

---

### `M115` - Report Firmware Info

**Command Syntax:**
```gcode
M115
```

**Description:**
Reports firmware version and machine capabilities.

**Expected Output:**
```text
[VER:3.5.26 BISSO-E350]
[OPT:B#,M,T#]
[CAPABILITY:4-axis]
[CAPABILITY:adaptive-feed]
[CAPABILITY:G4-dwell]
[CAPABILITY:M114-position]
[CAPABILITY:M154-auto-report]
[CAPABILITY:M117-lcd-msg]
[CAPABILITY:WCS-6-system]
[CAPABILITY:soft-limits]
```

---

### `M117` - Display LCD Message

**Command Syntax:**
```gcode
M117 <text>
```

**Description:**
Displays a custom message on the 20x4 LCD screen. Message displays for 10 seconds then clears.

**Usage Examples:**
```gcode
M117 CUTTING EDGE 1 OF 5
M117 JOB COMPLETE!
M117 CHANGE BLADE NOW
```

**How It Works:**
```
LCD MESSAGE DISPLAY:
┌────────────────────────────────────────────────────────────────┐
│  20x4 LCD Character Display:                                       │
│                                                                    │
│  Row 0: Normal status line                                        │
│  Row 1: Position X/Y                                              │
│  Row 2: Position Z/A                                              │
│  Row 3: → M117 MESSAGE APPEARS HERE (20 chars max)               │
│                                                                    │
│  Message auto-clears after 10 seconds                             │
└────────────────────────────────────────────────────────────────┘
```

---

### `M154` - Position Auto-Report

**Command Syntax:**
```gcode
M154 S<interval>
```

**Description:**
Enables automatic position reporting at the specified interval.

**Parameters:**
| Parameter | Description | Valid Range |
|-----------|-------------|-------------|
| `S0` | Disable auto-report | - |
| `S1` | Report every 1 second | 1-60 seconds |

**Usage Example:**
```gcode
M154 S1    ; Report position every second
M154 S0    ; Stop auto-reporting
```

**How It Works:**
```
AUTO-REPORT MECHANISM:
┌────────────────────────────────────────────────────────────────┐
│  Timer-based position broadcast:                                   │
│                                                                    │
│  • Creates FreeRTOS timer at specified interval                   │
│  • On tick: queries encoder positions                             │
│  • Outputs M114-format position string                            │
│  • Used by G-code senders for real-time DRO                       │
│  • S0 stops and deletes the timer                                 │
└────────────────────────────────────────────────────────────────┘
```

---

### `M226` - Wait for Pin State

**Command Syntax:**
```gcode
M226 P<pin> S<state> [A<type>] [T<timeout>]
```

**Description:**
Pauses program execution until an input pin reaches a specified state. Used for sensor-based interlocks.

**Parameters:**
| Parameter | Description | Valid Values |
|-----------|-------------|--------------|
| `P<pin>` | Pin number | 0-7 (I2C) or 0-39 (GPIO) |
| `S<state>` | Wait for this state | 0=LOW, 1=HIGH |
| `A<type>` | Pin interface type | 0=I73, 1=Board, 2=GPIO |
| `T<timeout>` | Timeout in seconds | 0=never, 1-300 |

**Usage Example:**
```gcode
; Wait for VFD ready signal (pin 3 goes HIGH)
M226 P3 S1 T10

; Wait for coolant pressure OK
M226 P5 S1 A1 T5
```

**How It Works:**
```
PIN WAIT STATE MACHINE:
┌────────────────────────────────────────────────────────────────┐
│  1. Parse pin number, state, type, timeout                        │
│  2. Start timeout timer (if T>0)                                  │
│  3. Poll pin via appropriate interface:                           │
│     • A0: I73 input expander (PCF8574 @ 0x20)                     │
│     • A1: Board GPIO pins                                         │
│     • A2: ESP32 GPIO direct                                       │
│  4. Pin matches state → Continue program                          │
│  5. Timeout expires → ALARM (operator must clear)                 │
└────────────────────────────────────────────────────────────────┘
```

---

### `M255` - LCD Sleep Timeout

**Command Syntax:**
```gcode
M255 S<seconds>
```

**Description:**
Sets the LCD backlight auto-off timeout for energy saving.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `S0` | Never sleep (always on) |
| `S60` | Sleep after 60 seconds |
| `S300` | Sleep after 5 minutes |

**How It Works:**
```
LCD SLEEP MANAGEMENT:
┌────────────────────────────────────────────────────────────────┐
│  Backlight control to extend LCD lifespan:                        │
│                                                                    │
│  • Timer resets on any machine activity                          │
│  • Serial commands wake display                                   │
│  • Button presses wake display                                    │
│  • Motion starting wakes display                                  │
│  • Timeout reached → backlight OFF (content remains)             │
└────────────────────────────────────────────────────────────────┘
```

---

### `M999` - System Reset/Resync

**Command Syntax:**
```gcode
M999
```

**Description:**
Resets the G-code parser and clears error/alarm states without rebooting. The "fix-it" command.

**When To Use:**
- After E-stop recovery
- Parser timeout errors
- After M112 software E-stop
- Any "stuck" condition

**Expected Output:**
```text
[GCODE] Parser resynced. Ready.
ok
```

**How It Works:**
```
PARSER RESYNC SEQUENCE:
┌────────────────────────────────────────────────────────────────┐
│  1. Clear ALARM state flags                                       │
│  2. Reset parser modal states (G0 G54 G90 G94 M5)                 │
│  3. Clear motion planner queue                                    │
│  4. Reset serial input buffer                                     │
│  5. Re-sync encoder positions                                     │
│  6. Return to IDLE state                                          │
│                                                                    │
│  NOTE: Does NOT re-home. Position may be unknown after E-stop.    │
└────────────────────────────────────────────────────────────────┘
```

---

## 🕹️ JOGGING & REAL-TIME CONTROLS

These commands are processed **immediately** (not queued) for real-time machine control.

---

### `?` - Real-Time Status Report

**Command Syntax:**
```text
?
```
(Single character, no Enter required)

**Description:**
Returns instant status report showing machine state, position, and buffer status.

**Response Format:**
```text
<State|MPos:X,Y,Z,A|WPos:X,Y,Z,A|Bf:buffer,127|FS:feed,spindle>
```

**State Values:**
| State | Meaning |
|-------|---------|
| `Idle` | Ready for commands |
| `Run` | Executing motion |
| `Hold:0` | Paused (M0/M1) |
| `Hold:1` | Safety pause (alarm) |
| `Home` | Homing in progress |
| `Alarm` | Error condition |

**Example Output:**
```text
<Run|MPos:1234.000,567.000,50.000,0.000|WPos:100.000,50.000,50.000,0.000|Bf:28,127|FS:1200,0>
```

---

### `!` - Feed Hold (Pause)

**Command Syntax:**
```text
!
```
(Single character)

**Description:**
**Instantly** stops axis motion, preserving the current program position. Resume with `~`.

---

### `~` - Cycle Start (Resume)

**Command Syntax:**
```text
~
```
(Single character)

**Description:**
Resumes motion after `!` (feed hold) or `M0/M1` (program pause).

---

### `Ctrl-X` / `0x18` - Soft Reset

**Command Syntax:**
```text
Ctrl-X    (ASCII 0x18)
```

**Description:**
Performs a **software reset**—stops all motion, clears buffers, re-initializes. Does not reboot the ESP32.

---

### `$J=` - Safe Jogging

**Command Syntax:**
```gcode
$J=G91 X<dist> F<speed>
$J=G90 X<pos> F<speed>
```

**Description:**
Special jogging mode for manual axis positioning. Unlike G0/G1:
- Can be cancelled instantly with `!` or `0x85`
- Does not affect G-code program state
- Safe for pendant/joystick control

**Usage Examples:**
```gcode
; Jog X right by 100mm at 1500 mm/min
$J=G91 X100 F1500

; Jog Y forward by 50mm
$J=G91 Y50 F1000

; Jog to absolute work position
$J=G90 X500 Y300 F2000
```

**Error Codes:**
| Error | Meaning |
|-------|---------|
| `error:8` | Not idle (already moving) |
| `error:3` | Motion rejected |
| `error:33` | Invalid axis value |

---

## ⚙️ CONFIGURATION SETTINGS ($)

These Grbl-compatible settings configure machine parameters. Changes are saved to NVS Flash.

---

### Reading Settings

**Command:** `$` (show all settings)

**Output:**
```text
$100=200.000      ; X pulses per mm
$101=200.000      ; Y pulses per mm
$102=200.000      ; Z pulses per mm
$103=200.000      ; A pulses per mm (if applicable)
$110=1500.000     ; X max speed (mm/min)
$111=1500.000     ; Y max speed (mm/min)
$112=500.000      ; Z max speed (mm/min)
$113=500.000      ; A max speed (mm/min)
$120=100.000      ; Default acceleration
$130=5000.000     ; X max travel (mm)
$131=3500.000     ; Y max travel (mm)
$132=1000.000     ; Z max travel (mm)
ok
```

---

### Setting Values

**Command:** `$<id>=<value>`

**Example:**
```text
$100=250          ; Set X pulses per mm to 250
$130=4500         ; Set X max travel to 4500mm
```

---

### Setting Reference Table

| Setting | Description | Default | Unit |
|---------|-------------|---------|------|
| `$100` | X-axis encoder scale (pulses/mm) | 200 | pulses/mm |
| `$101` | Y-axis encoder scale | 200 | pulses/mm |
| `$102` | Z-axis encoder scale | 200 | pulses/mm |
| `$103` | A-axis encoder scale | 200 | pulses/deg |
| `$110` | X-axis maximum speed | 1500 | mm/min |
| `$111` | Y-axis maximum speed | 1500 | mm/min |
| `$112` | Z-axis maximum speed | 500 | mm/min |
| `$113` | A-axis maximum speed | 500 | deg/min |
| `$120` | Default acceleration | 100 | mm/sec² |
| `$130` | X-axis maximum travel (soft limit) | 5000 | mm |
| `$131` | Y-axis maximum travel | 3500 | mm |
| `$132` | Z-axis maximum travel | 1000 | mm |

---

### Other Grbl Commands

| Command | Description |
|---------|-------------|
| `$H` | Run homing cycle (same as G28) |
| `$G` | Show parser state (active modal codes) |

---

## 🔧 ERROR CODES REFERENCE

When something goes wrong, the controller returns error codes in Grbl format:

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| `error:1` | Unknown command | Command not recognized | Check spelling |
| `error:3` | Unsupported | Feature not available | Check firmware version |
| `error:8` | Not idle | Command requires idle state | Wait for motion to complete |
| `error:9` | Limit switch | Soft or hard limit triggered | Move away from limit, clear alarm |
| `error:20` | G-code parse error | Invalid G-code syntax | Check command format |
| `error:22` | Soft limit exceeded | Target outside $130-$132 range | Reduce target value |
| `error:33` | Invalid value | Numeric parameter out of range | Check parameter format |

---

## 📋 COMPLETE COMMAND QUICK REFERENCE

### Motion Commands
| Command | Description |
|---------|-------------|
| `G0 X Y Z A` | Rapid positioning |
| `G1 X Y Z A F` | Linear cutting move |
| `G28 X Y Z A` | Homing sequence |
| `G30 P0/P1` | Go to predefined position |
| `$J=G91 X F` | Safe jogging |

### Coordinate Commands
| Command | Description |
|---------|-------------|
| `G54-G59` | Select work coordinate system |
| `G90` | Absolute coordinates |
| `G91` | Incremental coordinates |
| `G92 X Y Z A` | Set position (temporary) |
| `G10 L20 P X Y Z A` | Set WCS offset (permanent) |
| `G53 G0 X Y Z` | Machine coordinates (one-shot) |

### Utility Commands
| Command | Description |
|---------|-------------|
| `G4 P/S` | Dwell (wait) |
| `M0/M1` | Program stop/pause |
| `M2` | Program end |
| `M3/M5` | Spindle on/off |
| `M112` | Emergency stop |
| `M999` | System reset |

### Reporting Commands
| Command | Description |
|---------|-------------|
| `?` | Status report |
| `$` | Show settings |
| `$G` | Parser state |
| `M114` | Position report |
| `M115` | Firmware info |

### Real-Time Controls
| Command | Description |
|---------|-------------|
| `!` | Feed hold |
| `~` | Resume |
| `Ctrl-X` | Soft reset |

---

**Document Version:** 3.1.0  
**Last Updated:** 2026-02-23  
**Firmware Compatibility:** v3.6.x+  
**Author:** Antigravity (DeepMind Advanced Agentic Coding)  
**Machine:** BISSO E350 PosiPro 4-Axis CNC Bridge Saw

---

> [!TIP]
> For CLI commands (config, diagnostics, wifi, etc.), see [CLI_COMMANDS_REFERENCE.md](CLI_COMMANDS_REFERENCE.md)

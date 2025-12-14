# BISSO E350 Stone Bridge Saw Controller - Operator Quickstart Manual

**Version:** 3.0
**Document Date:** December 14, 2025
**Controller Firmware:** Current
**Web Interface:** Multi-File Architecture (v3.0)

---

## Table of Contents

1. [Safety Warnings](#safety-warnings)
2. [System Overview](#system-overview)
3. [Pre-Startup Checklist](#pre-startup-checklist)
4. [Initial Power-On Sequence](#initial-power-on-sequence)
5. [System Verification and Diagnostics](#system-verification-and-diagnostics)
6. [Encoder Calibration Procedure](#encoder-calibration-procedure)
7. [Basic Axis Operation](#basic-axis-operation)
8. [Web Interface Operation](#web-interface-operation)
9. [Troubleshooting Guide](#troubleshooting-guide)
10. [Maintenance Schedule](#maintenance-schedule)
11. [Emergency Procedures](#emergency-procedures)

---

## Safety Warnings

⚠️ **CRITICAL SAFETY INFORMATION**

### Before Operating the BISSO E350:

1. **Rotating Equipment Hazard**: The stone bridge saw operates with rotating cutting wheels and linear axes capable of rapid movement. Never place hands or objects in the path of moving parts.

2. **Electrical Hazard**: The system operates on 3-phase AC power via an Altivar 31 VFD. Ensure all electrical connections are secure and grounded. Only trained electrical personnel should perform maintenance.

3. **Lockout/Tagout**: Before any maintenance, disconnect power at the main breaker and install a lockout device. Do not proceed without proper LOTO procedures.

4. **Emergency Stop**: The E-Stop button (red mushroom switch) on the pendant is your emergency control. Familiarize yourself with its location and test it monthly.

5. **Contactor Hazard**: The PLC controls three contactors to select which axis receives power. Never disable the safety interlocks that prevent simultaneous multi-axis motion.

6. **Thermal Management**: The VFD cooling fan must operate continuously during saw operation. If the VFD temperature exceeds 90°C, the system will trigger a thermal alarm and halt operation. This is normal protection.

7. **Noise Level**: The cutting wheel generates 85-95 dB of noise. Wear hearing protection rated for this environment (minimum NRR 20).

---

## System Overview

### Hardware Architecture

The BISSO E350 is a **stone bridge saw with multiplexed motion control**:

- **Single Altivar 31 Variable Frequency Drive (VFD)**: Controls frequency from 0-105 Hz for all three axes
- **Three 3-Phase AC Induction Motors**: One for each axis (X, Y, Z)
- **PLC Contactor System**: Selects which motor receives VFD power (only ONE axis moves at a time)
- **Controller Firmware**: ESP32-based embedded system that signals the PLC via GPIO pins
- **Encoder Feedback**: WJ66 optical encoders on each axis provide position verification (100 pulses per mm typical calibration)
- **Web Dashboard**: Real-time monitoring at `http://<controller-ip>/` (default: http://192.168.1.100/)

### Key Specifications

| Component | Specification |
|-----------|--------------|
| **VFD Model** | Schneider Altivar 31 |
| **VFD Frequency Range** | 0-105 Hz (hardware capability) |
| **Max Output Frequency** | 105 Hz (tFr setting) |
| **High Speed Setting** | 105 Hz (HSP setting) |
| **Recommended Stone Cutting** | 5-20 Hz (safe speed for precision) |
| **Baseline Velocity** | 1 Hz VFD = 15 mm/s at motor shaft |
| **Encoder Resolution** | 100 PPR (pulses per revolution) |
| **Default Calibration** | 100 pulses/mm (adjustable per axis) |
| **Contactor Switching** | <50 ms per axis change |
| **Safety Response Time** | <100 ms for emergency stop |

### VFD Parameter Configuration

The Altivar 31 VFD is pre-configured with the following parameters for optimal operation:

| Parameter | Setting | Purpose |
|-----------|---------|---------|
| **ACC** | 0.6 sec | Motor acceleration ramp time (smooth speed-up) |
| **DEC** | 0.4 sec | Motor deceleration ramp time (smooth speed-down) |
| **LSP** | 1 Hz | Low speed limit (prevents uncontrolled creeping) |
| **HSP** | 105 Hz | High speed limit (maximum frequency available) |
| **iTH** | 3.5 A | Motor thermal protection current threshold |
| **UFr** | 20/30 | Voltage boost (improves torque at low speeds) |
| **FLG** | 0/30 | Frequency loop gain (encoder feedback stability) |
| **slp** | 0 | Slip compensation (disabled for optimal encoder accuracy) |
| **Vns** | 380 V | Nominal motor voltage (3-phase supply) |
| **FrS** | 50 Hz | Nominal motor frequency (European motor standard) |
| **tFr** | 105 Hz | Maximum output frequency (hardware limit) |

**⚠️ IMPORTANT - Safe Operating Speeds for Different Applications**:

| Application | Recommended Speed | VFD Frequency | Notes |
|-------------|-------------------|----------------|-------|
| **Precision Stone Cutting** | Slow | 5-20 Hz | ✅ **DEFAULT MODE** - Best quality cuts, most control |
| **Standard Stone Cutting** | Medium | 20-40 Hz | Normal production speed, balance of speed and precision |
| **Fast Material Removal** | Fast | 40-70 Hz | ⚠️ Use only for non-stone materials or rough operations |
| **Polishing/Finishing** | Variable | 10-30 Hz | Slower speeds for fine surface finishing |
| **Positioning (No Cut)** | Fast | 60-105 Hz | ✅ SAFE - Moving blank without cutting wheel spinning |

**DO NOT:**
- ❌ Run stone cutting operations above 40 Hz without explicit authorization
- ❌ Use maximum frequency (105 Hz) with cutting wheel engaged
- ❌ Attempt to change VFD settings without supervisor approval
- ❌ Run continuous operation above 50 Hz for more than 10 minutes (thermal stress)

---

## Pre-Startup Checklist

### Physical Inspection (Complete BEFORE powering on)

Complete this checklist every morning before operation:

```
□ Visual Inspection of Mechanics
  - Examine all axis motion paths for obstructions
  - Check that cutting wheel is properly installed and secured
  - Verify all cables and hoses are not pinched or damaged
  - Ensure water circulation hose (if wet cutting) is connected
  - Check that stone blank is properly positioned in fixture

□ VFD Cooling System
  - Verify VFD cooling fan spins freely (no grinding noise)
  - Check air intake vents are not blocked by dust
  - Ensure room temperature is below 35°C (fan capacity limit)

□ Electrical Connections
  - Visually inspect main power connector for corrosion or loose pins
  - Check ESP32 controller for visible damage (no burnt components)
  - Verify pendant wired connector is fully seated and latches secure
  - Confirm emergency stop button is easily accessible and red

□ Encoder Verification
  - Check all three encoder cables (X, Y, Z) for damage
  - Verify encoder LED indicators are visible (small green light)
  - Look for dust accumulation on encoder lenses (clean with soft brush if needed)

□ Contactor Switches
  - Listen for any audible clicking from the PLC enclosure
  - Verify contactor indicator lights (if equipped) all show green
```

### Electrical Power Verification

Before proceeding to power-on, verify:

```bash
# If using networked monitoring:
ping 192.168.1.100
# Expected response: Reply from 192.168.1.100: bytes=32 time=<5ms

# Verify main power supply voltage (use multimeter on main breaker)
# Expected: 380V ±10% for 3-phase, 230V ±10% for single-phase
```

---

## Initial Power-On Sequence

### Step 1: Engage Main Disconnect

**Location**: Main electrical panel, left side of machine

**Action**:
1. Locate the main disconnect switch (large red/black lever)
2. Verify it is currently in the DOWN position (OFF)
3. Listen for any audible discharge sounds
4. Wait 5 seconds for residual power to dissipate
5. Move switch to UP position (ON)

**Expected Results**:
- Soft "click" sound from main contactor closing
- Green LED lights on the VFD indicator panel (lower right corner)
- Fan in VFD cooling chamber begins running (might take 2-3 seconds to spin up)
- ESP32 controller green LED lights up
- No error sounds or alarms

**If Results Are Unexpected**:
- ❌ No fan spin after 5 seconds → Check breaker hasn't tripped, verify 380V supply present
- ❌ VFD shows red LED → Emergency shutdown activated; check safety interlocks, consult supervisor
- ❌ Loud buzzing sound → Electrical fault; turn off immediately, do not operate

### Step 2: Allow Warm-Up Period

**Duration**: 30-60 seconds minimum

**What's Happening**:
- VFD capacitors are charging
- ESP32 firmware is loading from flash memory
- Web server is starting and binding to network port 80
- Encoder systems are initializing and performing self-test

**During This Time**:
- Do NOT press buttons on the pendant
- Do NOT move any physical jog controls
- Do NOT attempt to connect to web dashboard yet
- Listen for any error beeps (3 beeps = fault condition)

**End of Warm-Up Indicators**:
- VFD power-on light stable and bright green
- Controller status LED on ESP32 blinking slowly (every 2 seconds)
- No alarm sounds for 10 consecutive seconds

### Step 3: Initial Web Dashboard Connection

**URL**: Open browser and navigate to `http://192.168.1.100/`

**Expected Response** (takes 3-5 seconds):
```
Status: "Online" (green dot indicator in top-left)
System Information showing:
- Firmware Version: [Current version]
- Hardware Version: E350 Rev A
- System Uptime: 0 days 0 hours 0 minutes
- Free Memory: 180-200 KB
- WiFi Signal: -40 to -60 dBm (Excellent to Good)
```

**If Dashboard Won't Load**:
1. Verify correct IP address: `ping 192.168.1.100` should reply
2. Try full browser refresh: `Ctrl+F5` (Chrome) or `Cmd+Shift+R` (Safari)
3. If still blank after 10 seconds, check browser console for errors: `F12` → Console tab
4. Common error: "Connection refused" → Controller not booted yet, wait another 30 seconds

### Step 4: Verify System Status

In the web dashboard, navigate to **Settings** tab (⚙️ icon):

**Check System Information Section**:

```
Firmware Version:        [Should be: v3.0 or later]
Hardware Version:        E350 Rev A
System Uptime:          0 days 0 hours 1 minute  ← Should be counting up
Free Memory:            180-200 KB               ← Should be stable (not decreasing)
WiFi Signal Strength:   -45 dBm                  ← Should be "Good" (green)
Last Connection:        00:00:15 (just now)      ← Should be "now"
```

**If Any Value Is Abnormal**:
- "Free Memory" continuously decreasing → Memory leak, restart system
- "WiFi Signal" showing "Very Weak" (-85 dBm) → Move router closer or check WiFi interference
- "Last Connection" showing old timestamp → Network connectivity issue

---

## System Verification and Diagnostics

### Comprehensive System Diagnostics Test

Before attempting any cutting operations, run a full diagnostic to verify all components:

#### Using Web Dashboard:

**Step 1: Navigate to Diagnostics Page**

Click **Diagnostics** tab (🔍 icon) in left navigation menu.

You should see three sections: **Per-Axis Diagnostics**, **Encoder Health**, and **VFD Diagnostics**.

#### Step 2: Check Each Axis Status

**X Axis Card** should display:

```
Quality Score:        100% (bright green bar)        ← Healthy at startup
Motion Jitter:        0.0 mm/s                       ← Should be zero when idle
Active Duration:      0 ms                           ← Zero when not moving
VFD/Encoder Error:    0%                             ← Should be 0% when idle
```

**Y Axis Card** and **Z Axis Card** should show identical values (all zeros/green)

**If Any Axis Shows Red**:
- Quality Score < 50% → Encoder may need calibration (see [Encoder Calibration](#encoder-calibration-procedure))
- VFD/Encoder Error > 10% → Possible encoder malfunction or loose cable

#### Step 3: Check Encoder Health Status

In the **Encoder Health** section, you should see:

```
Status:        OPTIMAL (green)
Last Reading:  [timestamp just now]
Signal Quality: Excellent
Drift Rate:    0.0 mm/min
```

**Acceptable Status Values**:
- ✅ OPTIMAL - Encoder working perfectly
- ✅ NORMAL - Encoder functional, minor calibration drift possible
- ⚠️ DEGRADED - Encoder signal weak, schedule calibration soon
- ❌ CRITICAL - Encoder non-functional, do NOT operate until fixed

#### Step 4: Check VFD Status

In the **VFD Diagnostics** section:

```
Current Output:        0.0 A                   ← Should be near zero (no load)
Frequency:             0.0 Hz                  ← Should be zero (not running)
Thermal Status:        30°C (Cool)             ← Should be room temperature +5°C
Fault Code:            0x0000                  ← Should be all zeros
```

**If Fault Code Is Not 0x0000**:

| Fault Code | Meaning | Action |
|-----------|---------|--------|
| 0x0001 | Overvoltage | Check input voltage is 380V ±10% |
| 0x0002 | Undervoltage | Check input voltage is 380V ±10%, verify no voltage sag |
| 0x0004 | Overcurrent | Check motor leads for short circuit; inspect contactor contacts |
| 0x0008 | Thermal Overshoot | Cool down VFD; reduce duty cycle; check fan operation |
| 0x0010 | Communication Loss | Restart controller and VFD |
| 0x0020 | Parameter Error | Contact factory support |

---

### Using Command-Line Diagnostics (Optional)

If web interface shows issues, use **Serial Console** for detailed diagnostics.

**Connect to controller**:
```bash
# Using USB-to-serial cable connected to ESP32
miniterm.py /dev/ttyUSB0 115200
# or on Windows: use PuTTY, set speed to 115200 baud

# Expected startup messages:
[SYSTEM] BISSO E350 Controller v3.0 booting
[MOTION] Initializing motion system...
[ENCODER] WJ66 encoder self-test passing...
[VFD] Altivar 31 connection established
[NETWORK] WiFi connected to "BISSO_5GHz" IP: 192.168.1.100
[WEB] Web server listening on port 80
[SYSTEM] Boot complete, ready for operation
```

**Run axis diagnostics**:
```bash
# Type this command in serial console:
axis status

# Expected output:
[AXIS] === Motion Quality Status (All Axes) ===
[AXIS] X Axis: Quality=100%, Duration=0ms, Stalled=No
[AXIS] Y Axis: Quality=100%, Duration=0ms, Stalled=No
[AXIS] Z Axis: Quality=100%, Duration=0ms, Stalled=No
[AXIS] Active Axis: None (0xFF)
```

**Run VFD diagnostics**:
```bash
# Type this command in serial console:
vfd status

# Expected output:
[VFD] === Altivar 31 Status ===
[VFD] Connection: OK
[VFD] Current Frequency: 0.0 Hz
[VFD] Output Current: 0.0 A
[VFD] Thermal Status: 28°C (OK)
[VFD] Fault Code: 0x0000 (No Faults)
[VFD] Last Update: 245ms ago
```

---

## Encoder Calibration Procedure

### Why Calibration Matters

Encoders measure axis position by counting pulses as the motor spins. If the encoder is not properly calibrated:
- Position displays will be incorrect
- Cutting dimensions will be wrong
- The system may detect false "stalls" when the axis is actually moving

Each axis must be individually calibrated because:
- Different motor gear ratios may be used
- Encoder mounting angles may vary
- Mechanical wear may affect each axis differently

### Calibration Quick Test (2 minutes)

**Before Running Full Calibration, Check**:

1. Move the X axis slowly using the **Motion** tab (🎮 icon)
   - Click the **left arrow button** 5 times (each click moves 1mm)
   - Watch the "Current Position" display - it should show -5.0mm
   - Click the **right arrow button** 5 times
   - Position should return to 0.0mm
   - ✅ If this works, X axis calibration is acceptable

2. Repeat for Y and Z axes using up/down buttons

3. If any axis position doesn't match button clicks, proceed to full calibration below

### Full Encoder Calibration Procedure

#### Step 1: Access Calibration Tool

Using **Serial Console** (see connection instructions above):

```bash
# Start X axis calibration
axis calibrate X 10

# What this command does:
# - "axis" = command group
# - "calibrate" = calibration operation
# - "X" = which axis (can also be Y or Z)
# - "10" = distance to move in millimeters

# Expected output:
[AXIS] Starting X axis calibration...
[AXIS] Moving 10.0mm forward
[AXIS] Initial position: 0.00mm
[AXIS] Target position: 10.00mm
[AXIS] MOTOR WILL START IN 3 SECONDS - CLEAR THE AREA
[AXIS] 3... 2... 1... Moving now
[MOTION] X motor activated
[AXIS] Final encoder position: 10.02mm
[AXIS] Calibration error: 0.2%
[AXIS] Calibration ACCEPTED (error < 1%)
[AXIS] New calibration factor: 100.0 pulses/mm
```

**What's Happening**:
1. System records current encoder position (should be 0)
2. Motor starts and moves axis 10mm as measured by motion controller
3. Encoder counts pulses and reports distance
4. System compares: Did encoder agree the motion was 10mm?
5. If error < 1%, calibration is accepted
6. If error > 1%, system will ask to recalibrate

#### Step 2: Success Indicators

If you see:
```
[AXIS] Calibration ACCEPTED
[AXIS] Error: 0.2%
[AXIS] New calibration factor saved to EEPROM
```

Then calibration succeeded! The encoder and mechanical system agree.

#### Step 3: Troubleshooting Failed Calibration

**Problem**: "Calibration error > 5%"

**Possible Causes**:
1. Loose encoder cable → Check connectors on both encoder and controller
2. Encoder LED not lit → Encoder may be faulty, replace required
3. Mechanical resistance → Check for binding in axis motion, lubricate if needed
4. Pulley slippage → Check motor coupler and pulley bolt torque

**Solution**:
```bash
# First, verify encoder is responding:
encoder status X

# Expected:
[ENCODER] X Axis Status:
[ENCODER] Connected: Yes
[ENCODER] Last Reading: 0.00mm
[ENCODER] Health: OPTIMAL
[ENCODER] Pulses: 0 (idle)

# If it says "Connected: No", check cables and restart

# Then try calibration again:
axis calibrate X 10

# If still failing, try smaller distance:
axis calibrate X 5

# Or try moving in opposite direction:
axis calibrate X -10
```

#### Step 4: Repeat for All Axes

```bash
# Calibrate Y axis
axis calibrate Y 10

# Expected output same as above
[AXIS] Calibration ACCEPTED (error < 1%)

# Calibrate Z axis
axis calibrate Z 10

# Expected output same as above
[AXIS] Calibration ACCEPTED (error < 1%)
```

#### Step 5: Verify All Axes

```bash
# Final verification
axis status

# Expected output:
[AXIS] === Motion Quality Status (All Axes) ===
[AXIS] X Axis: Quality=100%, Duration=0ms, Stalled=No, Error=0.2%
[AXIS] Y Axis: Quality=100%, Duration=0ms, Stalled=No, Error=0.1%
[AXIS] Z Axis: Quality=100%, Duration=0ms, Stalled=No, Error=0.3%
[AXIS] All axes calibrated and ready for operation
```

---

## Basic Axis Operation

### Safe Jogging Procedure

**Jogging** means moving an axis a small distance by pressing buttons. It's used to position the stone blank before making cuts.

#### Using Web Dashboard

**Step 1: Navigate to Motion Tab**

Click **Motion** (🎮 icon) in left navigation menu.

You should see:
- XY Jog Controls (5-button directional pad)
- Z Jog Controls (Up/Down buttons)
- Step Size Selector (dropdown with 1mm, 5mm, 10mm, 25mm options)
- Current Position Display (X: 0.00mm, Y: 0.00mm, Z: 0.00mm)

**Step 2: Select Safe Step Size**

For initial operation, use **1mm** steps:
1. Click Step Size dropdown
2. Select "1 mm"
3. Verify dropdown now shows "1 mm"

**Step 3: Perform Safe Jog Movement**

**Example: Move X axis right by 5mm**

1. Click the **RIGHT arrow button** (→) **5 times**
2. Between each click, wait for movement to complete (2-3 seconds)
3. Watch Current Position display: X should count up from 0.00 → 1.00 → 2.00 → 3.00 → 4.00 → 5.00

**Expected Behavior**:
- Each button press causes one click/beep from pendant
- Motor hums softly for 2-3 seconds
- Position increments by exactly 1mm
- No jerking or grinding sounds
- Movement is smooth and predictable

**If Movement Unexpected**:
- ❌ Position doesn't increment → Encoder may not be responding, verify calibration
- ❌ Motor makes grinding sound → Axis may be jammed, stop immediately and check
- ❌ Only moves slightly then stops → Contactor may have lost power, verify supply voltage

**Step 4: Return to Home Position**

1. Click **HOME** button (or press H on keyboard)
2. Motor moves axis back to 0.00mm

**Expected Result**:
- Position display shows X: 0.00mm
- Movement completes in 5-10 seconds depending on distance traveled

#### Using Serial Console Commands

For advanced operators, you can also jog using the command line:

```bash
# Move X axis right 10mm
motion jog X 10.0

# Expected output:
[MOTION] Moving X axis 10.0mm
[AXIS] Active: X (0)
[MOTION] Motion complete, position: 10.00mm

# Move Y axis left 5mm (negative value = left)
motion jog Y -5.0

# Expected output:
[MOTION] Moving Y axis -5.0mm
[AXIS] Active: Y (1)
[MOTION] Motion complete, position: -5.00mm

# Move Z axis down 20mm
motion jog Z -20.0

# Expected output:
[MOTION] Moving Z axis -20.0mm
[AXIS] Active: Z (2)
[MOTION] Motion complete, position: -20.00mm
```

### Understanding Motion Quality Scores

After moving an axis, check the **Diagnostics** tab to see the quality of that motion:

**Quality Score 100%**:
- ✅ Perfect motion
- Encoder agreed exactly with commanded motion
- No detected stalls, jitter, or slippage
- Healthy system

**Quality Score 95-99%**:
- ✅ Excellent motion
- Minor encoder drift (< 1% error)
- System is operating normally
- No action needed

**Quality Score 80-94%**:
- ⚠️ Acceptable but degrading
- Jitter or slight encoder errors detected
- Monitor closely; schedule encoder calibration
- Continue operation but plan maintenance

**Quality Score < 80%**:
- ❌ Poor motion quality
- Significant encoder errors or mechanical stalls detected
- DO NOT USE for precision cutting
- Must troubleshoot and repair before operation

### VFD Speed Control Procedure

The VFD frequency controls how fast the motors move. Different stone cutting applications require different speeds. This section shows how to select and monitor appropriate speeds.

#### Understanding VFD Frequency and Axis Speed

The relationship between VFD frequency and actual axis movement:

```
VFD Frequency = Motor Speed Control
↓
1 Hz VFD = 15 mm/s at motor shaft (baseline)
↓
Actual axis speed depends on motor gearbox ratio
↓
Example: 10 Hz VFD ≈ 150 mm/s (about 9 m/min) depending on transmission
```

**What This Means**:
- Increasing VFD frequency makes axes move faster
- Decreasing VFD frequency makes axes move slower
- The relationship is approximately linear

#### Safe Speed Selection for Stone Cutting

**Before Any Cutting Operation**:

1. **Determine Material Type**:
   - Granite, marble, limestone → Use **5-20 Hz** (precision range)
   - Engineered stone → Use **15-35 Hz** (standard range)
   - Non-stone materials → Use **20-50 Hz** (fast range)

2. **Determine Operation Type**:
   - Detailed trim cuts → Use **5-10 Hz** (slow, most control)
   - Standard production cuts → Use **10-20 Hz** (recommended default)
   - Rough material removal → Use **20-40 Hz** (acceptable)
   - Axis positioning (no cut) → Use **60-105 Hz** (safe when blade off)

3. **Default Recommendation**:
   - **For First-Time Operations: Always start at 10 Hz**
   - Monitor results and quality score
   - Adjust incrementally if needed (±2-3 Hz)

#### Using Serial Console to Set Speed

You can set a target frequency using the serial console:

```bash
# Set VFD frequency to 15 Hz
motion speed 15

# Expected output:
[VFD] Setting frequency to 15.0 Hz
[VFD] Ramp time: 0.6 seconds
[MOTION] Frequency set, ready to operate

# Set to slow precision speed
motion speed 8

# Expected output:
[VFD] Setting frequency to 8.0 Hz
[MOTION] Frequency set, ready to operate

# Set to fast positioning (no cut)
motion speed 80

# Expected output:
[VFD] Setting frequency to 80.0 Hz (positioning mode)
[MOTION] Frequency set, ready to operate
```

#### Monitoring Speed During Operation

**Real-Time Speed Monitoring**:

1. Open **Diagnostics** tab in web dashboard
2. Look at **VFD Diagnostics** section, "Frequency" field
3. Verify frequency matches what you set

**Example: Monitoring a 10 Hz Cut**:

```
Before Cut:
  Frequency: 0.0 Hz          ← Motor idle
  Current Output: 0.0 A      ← No load
  Thermal Status: 30°C       ← Cool

During Cut:
  Frequency: 10.0 Hz         ← Running at target speed
  Current Output: 5.0 A      ← Normal load on motor
  Thermal Status: 45°C       ← Warming up (normal)
  Quality Score: 98%         ← Excellent motion

After Cut:
  Frequency: 0.0 Hz          ← Motor stopped
  Current Output: 0.0 A      ← No load
  Thermal Status: 40°C       ← Cooling down
```

#### Speed Adjustment Guidelines

**If Cut Quality Is Poor (Quality Score < 85%)**:

Try **reducing speed by 2-3 Hz**:
- 15 Hz cut has poor quality → Try 12 Hz
- 20 Hz cut shows jitter → Try 17 Hz
- Slower speeds give better control and precision

**If Cut Is Too Slow (Production Falling Behind)**:

Try **increasing speed by 3-5 Hz**:
- 10 Hz cut is too slow → Try 13-15 Hz
- 15 Hz cut acceptable but slow → Try 18-20 Hz
- Always verify quality score remains above 80%

**If VFD Is Getting Hot (Temperature > 75°C)**:

**Reduce speed immediately**:
- Current speed 50 Hz → Reduce to 35 Hz
- Wait 5-10 minutes for cooling
- Resume cutting at reduced speed
- If thermal stress continues, stop operation

#### Speed Ramp Behavior

The VFD is configured with acceleration and deceleration ramps:

- **Acceleration Ramp**: 0.6 seconds to reach target frequency
- **Deceleration Ramp**: 0.4 seconds to stop from full speed

**What This Means**:
- Motor accelerates smoothly (no sudden jerks)
- Motor decelerates smoothly (no sudden stops)
- Stone receives consistent pressure during cuts
- Protects mechanical components from shock loads

**You Don't Need to Do Anything** - these ramps are automatic and controlled by the VFD.

---

## Web Interface Operation

### Dashboard Tab (📊)

**Purpose**: Monitor overall system health and performance

**Key Metrics to Check Before Each Cut**:

1. **System Health Card**
   - CPU Usage: Should be < 50%
   - Memory Free: Should be > 150 KB
   - Temperature: Should be room temperature ±5°C

2. **System Trends Chart**
   - Click time range selector: 1m, 5m, 1h, 24h
   - Chart should show stable, flat lines
   - No sudden spikes indicate stable operation

3. **X/Y/Z Axis Quality Cards**
   - All three should show green bars at 100% (before operation)
   - Jitter should be 0.0 mm/s (when idle)
   - All show "Not Stalled"

4. **Motion Status Card**
   - Should show "Ready for motion" (green)
   - Active axis should show "None"

5. **VFD Status Card**
   - Temperature: 25-40°C (room temperature)
   - Frequency: 0.0 Hz (when idle)
   - Fault Code: 0x0000 (no faults)

### Motion Tab (🎮)

**Purpose**: Manually move axes to position stone blank

See section [Basic Axis Operation](#basic-axis-operation) for detailed jogging instructions.

**Quick Reference**:
```
LEFT/RIGHT arrows    → Move X axis (left/right on stone table)
UP/DOWN arrows       → Move Y axis (forward/backward on stone table)
W/S or UP/DOWN       → Move Z axis (up/down for blade height)
SPACE bar            → Emergency stop (halts all motion immediately)
```

### Diagnostics Tab (🔍)

**Purpose**: Check detailed axis health and encoder performance

**Use This Tab To**:
- Verify encoder calibration before first cut
- Monitor axis quality during operation
- Detect early signs of mechanical wear or encoder drift
- Troubleshoot motion problems

**Green = Good, Red = Problem**

#### Monitoring VFD Frequency During Operation

The **VFD Diagnostics** section in the Diagnostics tab shows real-time frequency and performance:

**During Stone Cutting Operations, Verify**:

```
Current Output:        2.5-10.0 A              ← Normal load on motor
Frequency:             5.0-20.0 Hz             ← Precision cutting range
Thermal Status:        35-65°C                 ← Normal operating temperature
Fault Code:            0x0000                  ← No faults
```

**Frequency Interpretation**:

- **0.0 Hz** = Motor stopped (idle)
- **1-5 Hz** = Slow creep movement (calibration, positioning)
- **5-20 Hz** = Precision stone cutting (default safe range)
- **20-40 Hz** = Standard cutting speed (acceptable)
- **40-70 Hz** = Fast material removal (use only for non-stone)
- **70-105 Hz** = Rapid axis positioning without cutting wheel

**If Frequency Exceeds Safe Range**:

1. **Check Dashboard** - Is cutting wheel still engaged?
   - If YES → Reduce speed immediately (may damage stone)
   - If NO → OK for positioning at high speed

2. **Reduce Frequency**:
   - Use Motion tab jog controls (smaller step sizes)
   - Or use Serial Console: `motion speed 15` (set to 15 Hz)

3. **Monitor Thermal Status**:
   - If temperature approaching 80°C, reduce speed
   - If reaching 90°C, stop operation and allow cooling

**Example: Safe Speed Selection for Precision Cut**

```
Task: Cut marble stone slab (precision required)
↓
Set VFD Frequency: 10 Hz (mid-range of safe 5-20 Hz)
↓
Monitor Quality Score: Should stay 95%+
↓
Monitor Jitter: Should stay < 0.5 mm/s
↓
Monitor Temperature: Should stay 40-60°C
↓
If quality drops below 80%, reduce to 8 Hz
```

### Logs Tab (📋)

**Purpose**: Review system history for troubleshooting

**What To Look For**:
- Check "ERROR" and "CRITICAL" level messages
- Click filter dropdown to see only "CRITICAL" if problems occurred
- Each log entry shows timestamp, what happened, and system status

**Example Log Entries You Should See**:
```
2025-12-14 12:30:00 | INFO | system | System initialized
2025-12-14 12:30:15 | INFO | encoder | Encoder calibration successful
2025-12-14 12:35:00 | INFO | motion | X axis motion complete, quality=98%
```

**Example Log Entries That Indicate Problems**:
```
2025-12-14 12:31:00 | ERROR | motion | X axis stall detected!
2025-12-14 12:31:05 | CRITICAL | vfd | VFD thermal alarm (92°C)
2025-12-14 12:31:10 | CRITICAL | encoder | Encoder signal loss on Y axis
```

### Maintenance Tab (🔧)

**Purpose**: Track component wear and maintenance schedule

**Key Information**:
- **Wear Prediction Cards**: Shows estimated remaining life of each axis
- **Component Lifetime**: Tracks motor and VFD hours
- **Service History**: Log of previous maintenance events
- **Maintenance Calendar**: Shows when next service is due

**Before First Cut**: All components should show > 90% remaining life

### Settings Tab (⚙️)

**Purpose**: Configure system preferences and view system information

**Important Settings for New Operators**:

1. **Appearance**
   - Theme: Choose your preference (light/dark/high-contrast)
   - Font Size: Increase to 110-120% if hard to read

2. **Alert Thresholds**
   - Quality Score Alert: 50% (triggers warning when axis quality drops below 50%)
   - Jitter Alert: 1.0 mm/s (triggers warning when jitter exceeds 1.0 mm/s)
   - Temperature Alert: 80% (triggers warning when VFD reaches 80°C)
   - Sound Alerts: ✅ Enable (you'll hear beep if problem detected)

3. **System Information**
   - Verify Firmware Version is current
   - Confirm Hardware is "E350 Rev A"
   - Monitor WiFi Signal Strength (should be "Good" or "Excellent")

---

## Troubleshooting Guide

### Problem: Web Dashboard Won't Load

**Symptom**: Browser shows "Cannot reach server" or blank white page

**Step 1: Verify Physical Power**
```bash
# Check that main power switch is ON (up position)
# Verify VFD green light is on
# Listen for cooling fan running
# Wait 30 more seconds for web server to start
```

**Step 2: Check Network Connection**
```bash
# Ping the controller
ping 192.168.1.100

# Expected: "Reply from 192.168.1.100" every 1 second
# If "Destination unreachable", controller network interface is not responding
```

**Step 3: Verify URL Is Correct**
```
Wrong: http://192.168.0.100  ← Wrong IP range
Wrong: https://192.168.1.100 ← Should be HTTP, not HTTPS
Right: http://192.168.1.100  ← Correct format
```

**Step 4: Check Browser Cache**
- Clear browser cookies and cache: `Ctrl+Shift+Delete`
- Try in Incognito/Private mode
- Try different browser (Chrome, Firefox, Safari)

**Step 5: Force Restart Controller**
- Turn main power switch OFF (down)
- Wait 10 seconds
- Turn main power switch ON (up)
- Wait 60 seconds for boot
- Try URL again

---

### Problem: Axis Won't Move

**Symptom**: Click jog button but motor doesn't spin

**Step 1: Check Dashboard Status**

Navigate to **Dashboard** tab:
- Does "Motion Status" card show "Ready for motion" (green)?
- If red, click it and read the error message
- Common errors:
  - "Emergency Stop Active" → Check if E-Stop button is pressed (should pop out)
  - "Safety Interlock Violated" → Check pendant connection
  - "Power Not Available" → Check contactor lights

**Step 2: Verify Power Supply**
```bash
# Using multimeter on main connector terminals:
Phase A-B: Should read 380V ±10% (361-418V acceptable)
Phase B-C: Should read 380V ±10%
Phase C-A: Should read 380V ±10%

# If voltage is wrong, check:
- Main breaker is ON
- Power company has not cut service (check other equipment in building)
- Incoming power cables are not damaged
```

**Step 3: Check Encoder Cable**

For the axis that won't move:
1. Locate encoder cable (usually labeled on controller)
2. Inspect connector:
   - Check for loose or corroded pins
   - Reseat connector (unplug and replug firmly)
3. Check cable for visible damage (cuts, kinks, exposed wires)
4. Try motion again

**Step 4: Verify Contactor Is Engaged**

Listen to the PLC enclosure:
1. Click a jog button
2. You should hear a quiet "click" sound from a relay closing
3. If no click, contactor may have failed:
   - Turn off power
   - Contact technical support
   - Do not attempt to repair

**Step 5: Restart Controller**
```bash
# From serial console:
system restart

# Expected output:
[SYSTEM] Shutting down gracefully...
[SYSTEM] Saving state to EEPROM
[SYSTEM] Restarting in 3... 2... 1...
[SYSTEM] BISSO E350 Controller v3.0 booting
...
[SYSTEM] Boot complete, ready for operation
```

---

### Problem: Position Display Doesn't Match Button Clicks

**Symptom**: Click RIGHT 5 times but position only shows +2mm (should be +5mm)

**Step 1: Check Encoder Health**

Navigate to **Diagnostics** tab:
- Find the affected axis (X in above example)
- Check "Encoder Health" status:
  - ✅ OPTIMAL or NORMAL → Encoder is working
  - ⚠️ DEGRADED → Encoder signal weak, needs calibration
  - ❌ CRITICAL → Encoder may be faulty

**Step 2: Verify Encoder Cable Connection**

1. Locate encoder cable for affected axis
2. Check both connectors (one on encoder, one on controller)
3. Look for loose or corroded pins
4. Reseat connectors firmly (unplug and replug)
5. Try jog movement again

**Step 3: Run Encoder Calibration**

```bash
# From serial console:
axis calibrate X 10

# Expected: Calibration ACCEPTED

# If error > 1%, try again:
axis calibrate X 5

# If still failing, encoder may need replacement
```

**Step 4: Check for Mechanical Binding**

Manually (with power off):
1. Try to move the axis by hand
2. It should move smoothly without grinding
3. If sticky or grinding:
   - Check for obstructions
   - Lubricate linear guides (use machine way oil)
   - Check motor coupling for looseness

---

### Problem: Motor Makes Grinding Noise During Movement

**Symptom**: Strange grinding or squealing sound when axis moves

**IMMEDIATE ACTION**:
- Click STOP button or press SPACE bar
- Turn off power at main switch
- **DO NOT CONTINUE OPERATING**

**Possible Causes**:

1. **Motor Bearings Need Lubrication**
   - Last lubricated: Check Maintenance tab for "last service" date
   - If > 6 months ago, motor bearings likely need lubrication
   - Solution: Schedule maintenance with technician

2. **Pulley Slippage**
   - Motor turns but axis doesn't move (motor spins faster than axis moves)
   - Solution: Check pulley bolt torque, may need tightening

3. **Mechanical Obstruction**
   - Something is blocking axis motion
   - Solution: Remove obstruction, try movement again in opposite direction

4. **Encoder Malfunction**
   - Encoder generates false motion signals
   - Solution: Try recalibration, may need replacement if still grinding

**What NOT To Do**:
- ❌ Continue operating and hope it goes away
- ❌ Lubricate the motor yourself (could cause short circuit)
- ❌ Try to manually free a jammed axis while power is on

---

### Problem: VFD Shows Fault Code

**Symptom**: Web dashboard shows VFD Fault Code as something other than "0x0000"

**Step 1: Note the Fault Code**

Navigate to **Diagnostics** tab, "VFD Diagnostics" section:
- Record the exact fault code (e.g., 0x0008)
- Note the timestamp

**Step 2: Clear the Fault** (if temporary)

```bash
# From serial console:
vfd reset

# Expected output:
[VFD] Attempting to clear fault...
[VFD] Fault code cleared
[VFD] Status: Ready for operation

# Check dashboard:
[Fault Code] should now show "0x0000"
```

**Step 3: If Fault Code Returns**

| Fault Code | Meaning | Action |
|-----------|---------|--------|
| 0x0001 | Overvoltage (input > 440V) | Check incoming power voltage, may be utility issue |
| 0x0002 | Undervoltage (input < 340V) | Check incoming power voltage, verify wire connections |
| 0x0004 | Overcurrent (motors drawing > 30A) | Check motor for short circuits, inspect contactor |
| 0x0008 | Thermal Overshoot (> 90°C) | Stop operation, let cool to 50°C, check cooling fan |
| 0x0010 | Communication Loss | Restart controller, restart VFD |
| 0x0020 | Parameter Error | Contact factory support |

**Example: Recovering from Thermal Overshoot**

```bash
# Stop all operations
# Turn off main power switch
# Wait 10-15 minutes for VFD to cool down
# Verify cooling fan is spinning (should hear it running)
# Turn power back on

# Check status:
vfd status

# Expected:
[VFD] Thermal Status: 35°C (OK)
[VFD] Fault Code: 0x0000
```

---

### Problem: System Won't Boot After Power Loss

**Symptom**: Turned off power, turned back on, but web dashboard never loads

**Step 1: Check Physical Indicators**

```
VFD Green Light: Should be ON
VFD Fan: Should be running
Controller LED: Should be blinking slowly (every 2 seconds)
```

**If Green Light Not On**:
- Check main breaker is ON
- Check incoming power with multimeter (should be 380V)
- If breaker trips when turning on, there's a serious fault → Contact service

**If Fan Not Running**:
- VFD may have overheated and entered safety shutdown
- Wait 15 minutes with power on (do not turn off)
- Fan should resume running
- If not, VFD may be faulty → Contact service

**Step 2: Wait Longer for Boot**

Web server startup can take 60-90 seconds after power-on:
- Wait a full 2 minutes
- Try pinging controller: `ping 192.168.1.100`
- Try accessing dashboard: `http://192.168.1.100/`

**Step 3: Check Serial Console for Errors**

Connect to controller via USB serial:
```bash
miniterm.py /dev/ttyUSB0 115200

# Look for error messages like:
[ERROR] EEPROM corruption detected
[ERROR] Encoder initialization failed
[ERROR] VFD Modbus timeout
```

**If You See Error Messages**: Contact technical support with exact error text

**Step 4: Force Factory Reset** (Last Resort)

```bash
# From serial console:
system factory-reset

# Warning: This will erase all calibration data!
# Expected output:
[SYSTEM] Factory reset initiated
[SYSTEM] Erasing EEPROM
[SYSTEM] Restarting in 3... 2... 1...

# After restart, you will need to re-run encoder calibration
```

---

### Problem: WiFi Connection Unstable

**Symptom**: Dashboard keeps disconnecting and reconnecting (status light flickering red/green)

**Step 1: Check Signal Strength**

Navigate to **Settings** tab, "System Information" section:
- WiFi Signal Strength should show between -40 and -70 dBm
- Display should say "Good" or "Excellent"

**If Signal Shows -85 dBm ("Very Weak")**:
- Move WiFi router closer to machine (ideally within 10 meters)
- Check for interference sources (microwaves, cordless phones, other WiFi networks)
- Try WiFi on 5GHz band instead of 2.4GHz (less crowded)

**Step 2: Restart WiFi Connection**

```bash
# From serial console:
network restart

# Expected output:
[NETWORK] Disconnecting from WiFi
[NETWORK] Scanning for networks...
[NETWORK] Found "BISSO_5GHz" signal strength -45dBm
[NETWORK] Connecting...
[NETWORK] Connected with IP: 192.168.1.100
```

**Step 3: Check for IP Address Conflicts**

```bash
# From your computer:
arp -a | grep 192.168.1.100

# Expected: One entry for "192.168.1.100"
# If multiple entries, IP address conflict exists

# Solution: Assign static IP in router DHCP settings
```

**Step 4: Check Network Logs**

Navigate to **Logs** tab:
- Filter by Source: "network"
- Look for repeated "Connection lost" messages
- If appearing every 30 seconds, WiFi hardware may be faulty

---

## Maintenance Schedule

### Daily Checks (Before First Startup)

Complete the [Pre-Startup Checklist](#pre-startup-checklist) every morning.

**Time Required**: 5 minutes

### Weekly Maintenance

**Monday Morning**:
1. Check VFD cooling fan for dust accumulation
   - Gently brush away any dust with soft brush
   - Do NOT use compressed air (can clog intake filters)

2. Inspect all visible cables and connectors
   - Look for damage, corrosion, or loose pins
   - Reseat any loose connectors

**Friday Afternoon**:
1. Review Logs tab for any ERROR or CRITICAL messages
   - Note any recurring problems
   - Schedule repairs if needed

### Monthly Maintenance

**First Day of Month**:

1. Run full system diagnostics:
   ```bash
   axis status
   encoder status all
   vfd status
   ```

2. Record all quality scores and thermal readings
   - Compare to previous month
   - Look for trends (gradually degrading performance)

3. Verify encoder calibration still acceptable:
   ```bash
   axis calibrate X 10
   axis calibrate Y 10
   axis calibrate Z 10

   # All should show: Calibration ACCEPTED
   ```

4. Clean VFD:
   - Turn off power
   - Gently wipe VFD enclosure exterior with damp cloth
   - Ensure cooling vents are clear

**Time Required**: 30 minutes

### Quarterly Maintenance (Every 3 Months)

**Monthly + Following Tasks**:

1. Lubricate motor bearings
   - Apply machine way oil to each axis motor bearing
   - Rotate motor by hand several times after applying
   - Wipe away excess oil

2. Inspect and clean encoder lenses:
   - Locate encoder LED windows
   - Gently clean with soft, lint-free cloth
   - Do NOT use solvents (alcohol is okay)

3. Check PLC contactor contacts:
   - Ensure contacts are shiny (not black/burnt)
   - If contacts appear burned, contact service
   - Do NOT attempt to clean contacts yourself (requires careful handling)

**Time Required**: 1 hour

### Annual Maintenance (Every 12 Months)

**All Quarterly Tasks + Following**:

1. Professional VFD inspection:
   - Visual inspection of all solder joints (no cold joints)
   - Verification of cooling system performance
   - Comprehensive electrical safety testing
   - **Must be performed by qualified technician**

2. Full motion system audit:
   - Check motor coupling alignment
   - Verify pulley belt tension
   - Inspect mechanical wear on linear guides
   - Check for any signs of rust or corrosion

3. Encoder replacement if needed:
   - If quality scores have degraded to 70-80%, replace encoder
   - Perform calibration after replacement
   - Verify operation at full speed

4. Update firmware:
   - Check GitHub for latest firmware release
   - Follow update procedures (preserve calibration data)

**Time Required**: 4-6 hours, **requires qualified technician**

### Maintenance Record Template

Use this template to track maintenance:

```
Date: 2025-12-14
Time: 09:00 AM

CHECKS PERFORMED:
[ ] Pre-startup physical inspection
[ ] VFD cooling fan operation
[ ] Encoder cable visual inspection
[ ] Network connectivity
[ ] System diagnostics (axis status)
[ ] Thermal monitoring

QUALITY SCORES BEFORE:
  X Axis: 100%
  Y Axis: 100%
  Z Axis: 100%

QUALITY SCORES AFTER:
  X Axis: 100%
  Y Axis: 100%
  Z Axis: 100%

REPAIRS/ISSUES FOUND:
  None

NEXT SCHEDULED MAINTENANCE:
  Weekly checks: 2025-12-21
  Monthly calibration: 2026-01-14

Technician Name: ___________
Signature: ___________
```

---

## Emergency Procedures

### Emergency Stop (E-Stop)

**Location**: Red mushroom button on pendant (easily accessible)

**When To Use**:
- Unexpected motor sounds or grinding
- Axis moving in wrong direction
- Person in danger of struck by moving parts
- Any unintended motion

**How To Use**:
1. Press the red mushroom button firmly
2. Motor will stop within 100 milliseconds
3. Button will pop out when activated

**After Emergency Stop**:

1. **Verify Safety**: Confirm no parts are moving
2. **Investigate**: Determine what triggered the stop
   - Check dashboard for error messages
   - Review serial console output
   - Check logs for fault entries

3. **Clear the Fault**:
   ```bash
   # From serial console:
   vfd reset

   # Then check status:
   vfd status
   ```

4. **Resume Operation**: Once fault is cleared and understood

### Thermal Emergency Shutdown

**Symptom**: VFD thermal alarm activates, cutting stops

**Automatic Response**:
- All motors immediately de-energized
- VFD enters emergency cooling mode
- System generates loud beep (3 x long beeps)
- Dashboard shows alarm: "VFD Thermal Overshoot"

**What To Do**:

1. **Stop All Operations**: Already halted automatically
2. **Turn Off Power**: Move main switch to OFF position
3. **Allow Cooling**: Wait 15-20 minutes with power off
4. **Check Cooling System**:
   - Verify fan spins freely (by hand, power off)
   - Check air intake vents not blocked
   - Clear any dust from cooling vents
5. **Resume**: Turn power back on after cooling

**Prevention**:
- Monitor VFD temperature on Dashboard before it reaches 85°C
- Reduce cutting speed if thermal trend is rising
- Ensure room is well-ventilated (max 35°C ambient)

### Power Loss Emergency

**What Happens**:
1. All motion stops immediately
2. Contactor relays de-energize (no holding circuit)
3. Encoder data remains valid (stored locally)

**Recovery After Power Restored**:

1. **Wait 60 seconds** for boot sequence
2. **Verify All Systems**:
   ```bash
   axis status          # Check all axes ready
   encoder status all   # Check encoder readings
   vfd status          # Check VFD ready
   ```
3. **Verify Position**: Position data is preserved:
   ```
   Expected: Position should match where you left off
   ```
4. **Resume Operation**: System is safe to continue

**Important**: Encoder positions are preserved because they are stored in EEPROM (non-volatile memory). Your stone blank will be in the exact same position as when power was lost.

---

## Quick Reference Guide

### Keyboard Shortcuts (Web Dashboard)

```
T          → Toggle theme (light/dark/high-contrast/colorblind)
Space      → Emergency stop all motion
Arrow Keys → Jog X/Y axes (← → for X, ↑ ↓ for Y)
W/S        → Jog Z axis (W = up, S = down)
H          → Move all axes to home position (0, 0, 0)
```

### Common Command Examples

```bash
# Jog operations
motion jog X 10.0           # Move X right 10mm
motion jog Y -5.0           # Move Y left 5mm
motion jog Z -20.0          # Move Z down 20mm

# Calibration
axis calibrate X 10         # Calibrate X axis with 10mm motion
axis calibrate all 10       # Calibrate all axes with 10mm motion

# Status checks
axis status                 # Show all axis health
encoder status X            # Show X encoder health
vfd status                  # Show VFD status
network status              # Show WiFi connection status

# Diagnostics
logs show ERROR             # Show only error messages
logs show CRITICAL          # Show only critical messages
system info                 # Show system information
```

### Normal Startup Sequence

```
1. Turn main power switch ON (up)
   ├─ Wait 5 seconds for soft start
   ├─ Listen for fan to spin up
   └─ Verify green light on VFD

2. Wait 60 seconds for boot
   ├─ ESP32 LED should blink slowly
   ├─ Web server starting
   └─ Encoder self-test running

3. Access web dashboard
   ├─ Open http://192.168.1.100/
   ├─ Verify all metrics show green
   └─ Check no error messages

4. Run encoder calibration
   ├─ axis calibrate all 10
   ├─ Verify all show "ACCEPTED"
   └─ Check quality scores are 100%

5. Ready for operation
   ├─ Jog axes to position stone blank
   ├─ Verify position displays match button clicks
   └─ Begin cutting operations
```

### Troubleshooting Decision Tree

```
System won't start?
├─ Power switch OFF → Turn ON, wait 60 seconds
├─ VFD light not on → Check breaker, check incoming voltage
└─ Still not starting → Contact service

Dashboard loads but shows errors?
├─ Red X for encoder → Verify cable connected, run calibration
├─ Red X for VFD → Check power voltage, reset VFD
└─ Red X for motion → Check emergency stop, verify power

Axis won't move?
├─ Check E-Stop button (should pop out) → Press to reset
├─ Check motion status → Should show "Ready for motion"
├─ Verify power voltage → Should be 380V ±10%
└─ Try different axis → If others move, isolated problem

Position doesn't match clicks?
├─ Check encoder health → Should be OPTIMAL
├─ Run calibration → axis calibrate X 10
├─ Check mechanical binding → Try moving by hand (power off)
└─ Check encoder cable → Reseat connectors
```

---

## Contact Information for Support

**For Technical Issues**:
- Serial Console Error Codes → Note exact error message and system state
- Hardware Failures → Contact service with:
  - Photograph of any burned/damaged components
  - Serial number of affected component
  - Date of last maintenance
  - Description of what happened before failure

**Documentation**:
- Full firmware source code available on GitHub
- Hardware schematic available from manufacturer
- VFD manual: Schneider Altivar 31, manual available online

---

## Document Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-06-01 | Initial release for Phase 5.0 |
| 2.0 | 2025-09-15 | Updated for Phase 5.6 per-axis validation |
| 3.0 | 2025-12-14 | Complete rewrite for multi-file web interface, detailed maintenance procedures |

---

**Document Owner**: BISSO E350 Controller Development Team
**Last Updated**: December 14, 2025
**Next Review**: March 14, 2026

---

END OF QUICKSTART MANUAL


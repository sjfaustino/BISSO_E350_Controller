# Universal GCode Sender (UGS) Setup Guide
**For BISSO E350 Stone Bridge Saw Controller**
**Firmware Version:** Gemini v3.5.25+
**Document Date:** 2025-12-08

---

## Table of Contents
1. [Prerequisites](#prerequisites)
2. [UGS Installation](#ugs-installation)
3. [Hardware Connection](#hardware-connection)
4. [UGS Configuration](#ugs-configuration)
5. [First Connection](#first-connection)
6. [Feature Testing](#feature-testing)
7. [Troubleshooting](#troubleshooting)
8. [Operational Workflow](#operational-workflow)
9. [Safety Checklist](#safety-checklist)

---

## 1. Prerequisites

### Software Requirements
- **Java Runtime Environment (JRE) 11 or newer**
  - Download: https://adoptium.net/
  - Required for running UGS

- **Universal GCode Sender Platform**
  - Download: https://winder.github.io/ugs_website/download/
  - Recommended version: 2.0.17 or newer
  - File: `ugs-platform-app-<version>.zip`

### Hardware Requirements
- **USB Cable** - Type A to USB-C (or appropriate for ESP32-S3)
- **Serial Port** - ESP32 creates virtual COM port when connected
- **Power** - Controller must be powered via external 12-24V supply

### Driver Installation (Windows Only)
- ESP32-S3 typically uses **CP210x or CH340** USB-to-Serial chip
- Windows may require driver installation
- Download from: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

---

## 2. UGS Installation

### Step-by-Step Installation:

**Windows:**
1. Extract `ugs-platform-app-<version>.zip` to `C:\Program Files\UGS\`
2. Navigate to `C:\Program Files\UGS\bin\`
3. Run `ugsplatform64.exe` (or `ugsplatform.exe` for 32-bit)
4. Create desktop shortcut for easy access

**macOS:**
1. Extract ZIP file
2. Copy `ugsplatform.app` to `/Applications/`
3. Open from Applications folder
4. If blocked by Gatekeeper: Right-click → Open → Confirm

**Linux:**
1. Extract ZIP file to `/opt/ugs/`
2. Make executable: `chmod +x /opt/ugs/bin/ugsplatform`
3. Run: `/opt/ugs/bin/ugsplatform`
4. Add user to dialout group: `sudo usermod -a -G dialout $USER`
5. Log out and log back in for group change to take effect

---

## 3. Hardware Connection

### Connection Sequence (IMPORTANT - Follow Order):

1. **POWER OFF** - Ensure controller has no power
2. **Connect USB Cable** - ESP32 to computer
3. **Power ON Controller** - Apply 12-24V power to controller
4. **Wait for Boot** - Allow 5 seconds for controller to initialize
5. **Check Device Manager** (Windows) or `ls /dev/ttyUSB*` (Linux)

### Identify COM Port:

**Windows:**
- Open Device Manager (Win+X → Device Manager)
- Expand "Ports (COM & LPT)"
- Look for "USB Serial Port (COM3)" or similar
- Note the COM port number (e.g., COM3, COM7)

**macOS:**
- Open Terminal
- Run: `ls /dev/cu.*`
- Look for `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`

**Linux:**
- Open Terminal
- Run: `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`
- Usually shows as `/dev/ttyUSB0`

---

## 4. UGS Configuration

### Initial Setup:

1. **Launch UGS Platform**
   - First time: May show setup wizard (click Next through defaults)

2. **Configure Connection Settings:**

   **Method 1: Using GUI (Recommended for beginners)**
   - Top toolbar → Firmware: Select `GRBL`
   - Baud rate: `115200`
   - Port: Select your COM port from dropdown
   - Click `Connect` button (or Ctrl+C)

   **Method 2: Using Preferences (Advanced)**
   - Menu: `File` → `Preferences` → `UGS`
   - Firmware: `GRBL`
   - Port: Your COM port
   - Baud Rate: `115200`
   - Enable Status Updates: `ON`
   - Status Update Rate: `100ms` (10 Hz)
   - Enable Verbose Output: `OFF` (unless debugging)

3. **Configure Machine Settings:**
   - Menu: `Machine` → `Firmware Settings`
   - These will load from controller after connection

4. **Configure Visualizer (Optional):**
   - Menu: `Window` → `Classic` → `Visualizer`
   - Shows 3D toolpath preview
   - Useful for verifying G-code before running

---

## 5. First Connection

### Step-by-Step First Connection:

1. **Power on Controller**
   - Wait for controller to boot (5 seconds)
   - Green LED should be solid or blinking

2. **Connect in UGS**
   - Select COM port from dropdown
   - Ensure Firmware is `GRBL`
   - Ensure Baud is `115200`
   - Click `Connect` button

3. **Expected Welcome Message:**
   ```
   Grbl 1.1h ['$' for help]
   ```
   - This appears in the Console window at bottom
   - If you see this, connection is successful!

4. **Check Status Display:**
   - Top right should show: `State: Idle`
   - Position should show: `MPos: 0.000, 0.000, 0.000, 0.000`
   - This means controller is ready

5. **Verify Communication:**
   - Type `?` in console and press Enter
   - Should see status report:
     ```
     <Idle|MPos:0.000,0.000,0.000,0.000|WPos:0.000,0.000,0.000,0.000|Bf:31,127|FS:100,0>
     ```
   - This confirms real-time status reporting works

### Troubleshooting First Connection:

**Problem: "Could not open port"**
- Solution: Check if another program is using the port
- Solution: Restart UGS
- Solution: Unplug/replug USB cable

**Problem: "No response from controller"**
- Solution: Check baud rate is 115200
- Solution: Try different COM port
- Solution: Check USB cable is data-capable (not charge-only)
- Solution: Press reset button on controller

**Problem: Garbled text in console**
- Solution: Wrong baud rate - ensure 115200
- Solution: Cable interference - try different cable

---

## 6. Feature Testing

### Test 1: Basic Commands

**Type in Console:**
```gcode
G90          # Set absolute mode
```
**Expected:** `ok`

```gcode
$$           # View settings
```
**Expected:** List of settings like:
```
$100=1000.000
$101=1000.000
$102=1000.000
...
ok
```

### Test 2: Status Reporting

**Type in Console:**
```gcode
?            # Query status
```
**Expected:**
```
<Idle|MPos:0.000,0.000,0.000,0.000|WPos:0.000,0.000,0.000,0.000|Bf:31,127|FS:100,0>
```

**Automatic Status Updates:**
- UGS should automatically query status every 100ms
- Position display should update in real-time
- State should show: Idle / Run / Hold / Alarm

### Test 3: Jogging (CAUTION: Machine will move!)

**Using Jog Controls:**
1. In UGS, find the "Jog Controller" panel (Window → Classic → Jog Controller)
2. Set jog distance: `5 mm`
3. Set jog feed rate: `100 mm/min`
4. Click `X+` button
5. **Expected:** X axis moves +5mm, returns "ok"

**Using Console:**
```gcode
$J=G91 X5 F100     # Jog X+5mm relative
```
**Expected:** Axis moves, console shows `ok`

### Test 4: Work Coordinate Systems

**Set Work Zero (Current position = 0):**
```gcode
G10 L20 P1 X0 Y0 Z0 A0    # Set G54 zero at current position
```
**Expected:** `ok`

**Verify Offset:**
```gcode
?           # Check status
```
**Expected:** WPos should now show 0.000 for all axes

**Move in Work Coordinates:**
```gcode
G54         # Select coordinate system 1
G0 X50 Y50  # Move to X=50, Y=50 in work coordinates
```
**Expected:** Machine moves, `ok` after each command

### Test 5: G-Code File (Use Test File Below)

1. Load test file: `File` → `Open` → Select `BISSO_E350_Test.gcode`
2. View in Visualizer: `Window` → `Classic` → `Visualizer`
3. **VERIFY:** Machine is clear of obstacles
4. **VERIFY:** Emergency stop is accessible
5. Click `Send` button (or press `Alt+S`)
6. **Expected:** Machine executes program, status shows "Run"

---

## 7. Troubleshooting

### Common Issues and Solutions:

#### Issue: "ALARM" state after connection
**Cause:** E-stop activated, or limit violation, or previous emergency stop
**Solution:**
```gcode
$X          # Unlock alarm (ONLY if safe to do so)
```
Or press reset button on controller

#### Issue: Position jumps or incorrect
**Cause:** Encoder communication issue
**Solution:**
- Check encoder cable connections
- Restart controller
- Run encoder diagnostics via CLI

#### Issue: Motion doesn't start
**Possible Causes:**
1. **Alarm active** - Check status, clear with `$X`
2. **Safety interlock** - Check guards, e-stop
3. **Soft limit** - Move is beyond configured limits
4. **Emergency stop** - Reset required

**Debug Steps:**
```gcode
?           # Check state
$$          # Check limits ($130, $131, $132)
```

#### Issue: Jerky or stuttering motion
**Cause:** Low buffer, USB communication issues
**Solution:**
- Check USB cable quality
- Reduce status update rate in preferences
- Enable streaming mode in UGS settings

#### Issue: G-code commands fail
**Check Error Code:**
- `error:1` - Unknown command (check syntax)
- `error:3` - Invalid statement (check parameters)
- `error:8` - Not idle (wait for motion to stop)
- `error:20` - Unsupported G-code

#### Issue: Position drift over time
**Cause:** Encoder calibration, mechanical slippage
**Solution:**
- Recalibrate PPM (Pulses Per Millimeter)
- Check mechanical connection to encoders
- Verify encoder wiring

---

## 8. Operational Workflow

### Standard Operating Procedure:

#### Pre-Operation Checklist:
- [ ] Visual inspection of machine
- [ ] Check material is secured
- [ ] Verify blade is in good condition
- [ ] Check water supply (manual operation)
- [ ] Confirm all guards are in place
- [ ] Test emergency stop button

#### Startup Sequence:
1. **Power On** - Apply power to controller
2. **Connect UGS** - Establish USB connection
3. **Home Machine (Optional)** - If homing is configured
   ```gcode
   $H    # Home all axes
   ```
4. **Set Work Zero** - Position over material
   ```gcode
   G10 L20 P1 X0 Y0 Z0    # Set G54 zero
   G54                    # Select G54
   ```

#### Running a Job:
1. **Load G-code File** - File → Open
2. **Preview in Visualizer** - Verify toolpath
3. **Check Estimated Time** - Shows in UGS status bar
4. **Start Water** - Manually turn on coolant (REQUIRED)
5. **Start Spindle** - Manually start blade (REQUIRED)
6. **Send Program** - Click Send button
7. **Monitor Progress** - Watch status and position
8. **Emergency Stop if needed** - Click Stop or press `!`

#### Shutdown Sequence:
1. **Complete Job** - Wait for "Idle" state
2. **Stop Spindle** - Manually turn off blade
3. **Stop Water** - Manually turn off coolant
4. **Return to Safe Position** - Jog or G0 to clear
5. **Disconnect UGS** - Click Disconnect
6. **Power Off** - Remove controller power

---

## 9. Safety Checklist

### CRITICAL SAFETY REQUIREMENTS:

#### Before Every Cut:
- [ ] **Water is flowing** - NEVER run blade dry
- [ ] **Blade guard is closed** - Required for operation
- [ ] **Material is secured** - Check clamps/vacuum
- [ ] **Clear work area** - Remove tools and debris
- [ ] **Operator positioned safely** - Away from blade path
- [ ] **Emergency stop tested** - Press and verify motion stops

#### During Operation:
- [ ] **Monitor water flow** - Verify continuous flow
- [ ] **Monitor blade sound** - Listen for binding
- [ ] **Watch position display** - Verify correct path
- [ ] **Keep hands clear** - Never reach into work area
- [ ] **Emergency stop accessible** - Within reach at all times

#### Emergency Procedures:

**If Blade Binds or Unusual Sound:**
1. Press `!` in UGS (Feed Hold) - Immediate pause
2. Press emergency stop button - Cuts power
3. Manually stop spindle
4. Investigate cause before resuming

**If Water Stops:**
1. **IMMEDIATELY** stop spindle manually
2. Press emergency stop in UGS
3. Do NOT resume until water restored
4. Check blade for damage before restarting

**If Position Error:**
1. Press emergency stop
2. Manually stop spindle
3. Check encoder connections
4. Restart controller and re-zero

---

## 10. UGS Keyboard Shortcuts

### Essential Shortcuts:

| Action | Shortcut | Description |
|--------|----------|-------------|
| **Connect** | `Ctrl+C` | Connect to controller |
| **Disconnect** | `Ctrl+D` | Disconnect from controller |
| **Send File** | `Alt+S` | Start G-code program |
| **Pause** | `Space` | Pause motion (Feed Hold) |
| **Resume** | `Space` | Resume motion (Cycle Start) |
| **Stop** | `Ctrl+Alt+S` | Cancel current job |
| **Soft Reset** | `Ctrl+X` | Reset controller |

### Jog Shortcuts (When Jog Panel is Active):
- `Arrow Keys` - Jog X/Y axes
- `Page Up/Down` - Jog Z axis
- `+` / `-` - Increase/decrease jog distance

---

## 11. Customization and Advanced Settings

### Recommended UGS Settings:

**Menu: File → Preferences → UGS → Controller Options**
- [x] Enable Status Updates
- Status Update Rate: `100ms` (10 Hz for smooth display)
- Command Length: `128` (matches your buffer)
- [ ] Single Step Mode (disable for normal operation)
- [x] Enable Verbose Console Output (for debugging only)

**Menu: File → Preferences → UGS → Sender Options**
- [x] Use Arc Expander (for G2/G3 if needed)
- Arc Segment Length: `0.5mm`
- [ ] Use Duration Estimates (optional)

**Toolbar Customization:**
- Right-click toolbar → Customize
- Add frequently used commands as buttons
- Drag to reorder

---

## 12. Macros (Custom Commands)

### Create Custom Macros:

**Menu: Machine → Macros**

**Example: Set Work Zero Macro**
```gcode
; Name: Set Work Zero
G10 L20 P1 X0 Y0 Z0 A0
```
**Assigns to button for quick access**

**Example: Safe Position Macro**
```gcode
; Name: Go to Safe Position
G90                 ; Absolute mode
G0 Z0               ; Raise Z to zero
G0 X0 Y0            ; Move to origin
```

**Example: Probe Macro (if touch probe added)**
```gcode
; Name: Z Probe
G91                 ; Relative mode
G38.2 Z-50 F25      ; Probe down max 50mm
G92 Z0              ; Set Z zero at contact
G0 Z5               ; Lift 5mm
G90                 ; Absolute mode
```

---

## 13. Firmware Settings Reference

### View All Settings:
```gcode
$$    # Display all settings
```

### Key Settings (Your Controller):

| Setting | Parameter | Default | Units | Description |
|---------|-----------|---------|-------|-------------|
| `$100` | X PPM | 1000.000 | steps/mm | X axis calibration |
| `$101` | Y PPM | 1000.000 | steps/mm | Y axis calibration |
| `$102` | Z PPM | 1000.000 | steps/mm | Z axis calibration |
| `$103` | A PPM | 1000.000 | steps/deg | A axis calibration |
| `$110` | X Speed | 1000.000 | mm/min | X axis speed |
| `$111` | Y Speed | 1000.000 | mm/min | Y axis speed |
| `$112` | Z Speed | 1000.000 | mm/min | Z axis speed |
| `$113` | A Speed | 1000.000 | mm/min | A axis speed |
| `$120` | Accel | 100.000 | mm/s² | Acceleration |
| `$130` | X Max | 500.000 | mm | X soft limit |
| `$131` | Y Max | 500.000 | mm | Y soft limit |
| `$132` | Z Max | 100.000 | mm | Z soft limit |

### Modify Setting:
```gcode
$100=1234.567    # Set X axis PPM
```

---

## 14. Tips and Best Practices

### Optimize Performance:
1. **Use G-code buffering** - Smoother motion for complex paths
2. **Reduce status update rate** - If USB bandwidth is limited (50-100ms)
3. **Close unused UGS windows** - Reduces CPU load
4. **Simplify G-code** - Fewer short segments = smoother motion

### Prevent Issues:
1. **Always home/zero before starting** - Prevents crashes
2. **Test new G-code in air** - Jog Z up, run without material
3. **Keep firmware updated** - Check repository for updates
4. **Regular encoder checks** - Verify position accuracy periodically
5. **Save work zeros** - Document G54-G59 setups for repeat jobs

### Sequential Motion Awareness:
- **Remember:** Your controller moves one axis at a time
- **G1 X100 Y100** will move X first, then Y (not diagonal)
- **Plan toolpaths accordingly** - Approach/depart perpendicular
- **Avoid continuous direction changes** - Causes many start/stops

---

## 15. Maintenance and Calibration

### Weekly Checks:
- [ ] Verify position accuracy (test move 100mm, measure actual)
- [ ] Check encoder communication (no timeouts in diagnostics)
- [ ] Test emergency stop function
- [ ] Inspect USB cable for damage

### Monthly Calibration:
1. **PPM Calibration:**
   ```gcode
   G90              # Absolute mode
   G0 X0            # Move to zero
   G0 X100          # Move to 100mm
   # Measure actual distance with caliper
   # If actual is 98.5mm:
   # New PPM = Current PPM * (100 / 98.5)
   $100=1015.228    # Set corrected value
   ```

2. **Speed Calibration:**
   - Time a 100mm move at each speed profile
   - Update speed settings to match actual
   - Use CLI command: `calibrate speed X FAST 100`

---

## 16. Support and Resources

### Documentation:
- **Firmware Docs:** `/docs/` folder in repository
- **CLI Reference:** `README.md` in repository
- **UGS Documentation:** https://winder.github.io/ugs_website/

### Troubleshooting:
- **Check firmware logs** - Connect serial monitor at 115200 baud
- **Run diagnostics** - CLI command: `debug all`
- **Review fault log** - CLI command: `faults show`

### Community:
- **UGS Forum:** https://github.com/winder/Universal-G-Code-Sender/discussions
- **Grbl Wiki:** https://github.com/gnea/grbl/wiki

---

## Quick Reference Card

### Connection:
1. Power on controller
2. Connect USB
3. UGS: Select port, 115200 baud, GRBL firmware
4. Click Connect
5. Wait for "Grbl 1.1h ['$' for help]"

### Set Work Zero:
```gcode
G10 L20 P1 X0 Y0 Z0    # Set current position = 0
G54                    # Select coordinate system
```

### Emergency Stop:
- Press `!` in UGS (Feed Hold)
- Press physical E-stop button
- Click Stop button in UGS

### Check Status:
```gcode
?        # Manual status query
$$       # View settings
$G       # View parser state
```

### Reset Controller:
- Press Ctrl+X in UGS
- Or click Soft Reset button
- Or press physical reset button

---

**End of Setup Guide**

For test G-code files and validation procedures, see:
- `BISSO_E350_Test.gcode` - Comprehensive test program
- `VALIDATION_CHECKLIST.md` - Testing procedures

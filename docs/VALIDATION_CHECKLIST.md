# BISSO E350 Controller - Validation Checklist
**Firmware Version:** Gemini v3.5.25+
**Test Date:** _______________
**Tester Name:** _______________
**Controller Serial:** _______________

---

## Pre-Test Setup

### Hardware Preparation
- [ ] Controller powered with 12-24V supply
- [ ] USB cable connected to computer
- [ ] All encoders (WJ66) connected and functioning
- [ ] I2C expanders (PCF8574) responding
- [ ] Emergency stop button tested and working
- [ ] All axes have >200mm clearance for testing
- [ ] Machine guards in place (if applicable)

### Software Preparation
- [ ] Universal GCode Sender (UGS) installed
- [ ] USB drivers installed (CP210x or CH340)
- [ ] COM port identified: _______________
- [ ] Test file downloaded: `BISSO_E350_Test.gcode`

---

## Section 1: Initial Connection Test

### Test 1.1: USB Connection
**Procedure:**
1. Open UGS Platform
2. Select COM port
3. Set baud rate to 115200
4. Set firmware to GRBL
5. Click Connect

**Expected Results:**
- [ ] Connection established without errors
- [ ] Console shows: `Grbl 1.1h ['$' for help]`
- [ ] No timeout or connection error messages

**Actual Results:**
```
_________________________________________________________________
```

---

### Test 1.2: Status Reporting
**Procedure:**
1. Type `?` in console
2. Press Enter
3. Observe response

**Expected Results:**
- [ ] Status report appears: `<Idle|MPos:...|WPos:...|Bf:...|FS:...>`
- [ ] Position values displayed (may be non-zero)
- [ ] State shows "Idle"
- [ ] Buffer status shown (Bf:31,127 typical)

**Actual Results:**
```
_________________________________________________________________
```

---

### Test 1.3: Settings Display
**Procedure:**
1. Type `$$` in console
2. Press Enter
3. Review settings list

**Expected Results:**
- [ ] Settings list displayed
- [ ] $100-$103 show PPM values (e.g., 1000.000)
- [ ] $110-$113 show speed values
- [ ] $130-$132 show limit values
- [ ] Each line ends with "ok"

**Actual Results:**
```
$100 = ___________ (X PPM)
$101 = ___________ (Y PPM)
$102 = ___________ (Z PPM)
$103 = ___________ (A PPM)
```

---

## Section 2: Real-Time Command Test

### Test 2.1: Status Query (?)
**Procedure:**
1. Type `?` multiple times rapidly
2. Observe responses

**Expected Results:**
- [ ] Each query returns status immediately
- [ ] No delays or timeouts
- [ ] Position updates if axis is moving

**Pass:** ☐ Yes ☐ No

---

### Test 2.2: Feed Hold (!)
**Procedure:**
1. Send command: `G1 X100 F50` (slow move)
2. Immediately type `!` (Feed Hold)
3. Observe motion

**Expected Results:**
- [ ] Motion stops immediately (within 100ms)
- [ ] Status changes to "Hold:0"
- [ ] Position maintained

**Actual Position When Stopped:** X = ___________

---

### Test 2.3: Cycle Start (~)
**Procedure:**
1. With motion on hold (from 2.2), type `~`
2. Observe motion

**Expected Results:**
- [ ] Motion resumes from stopped position
- [ ] Status changes to "Run"
- [ ] Move completes to X=100

**Pass:** ☐ Yes ☐ No

---

### Test 2.4: Soft Reset (Ctrl+X)
**Procedure:**
1. Start a move: `G1 X50 F100`
2. Press Ctrl+X (or type 0x18)
3. Observe response

**Expected Results:**
- [ ] Motion stops immediately
- [ ] Console shows: `Grbl 1.1h ['$' for help]`
- [ ] Controller resets to idle state
- [ ] Position preserved (not homed)

**Pass:** ☐ Yes ☐ No

---

## Section 3: G-Code Parser Test

### Test 3.1: Absolute Mode (G90)
**Procedure:**
```gcode
G90           # Set absolute mode
G0 X10        # Move to X=10
G0 X20        # Move to X=20
G0 X0         # Return to X=0
```

**Expected Results:**
- [ ] Each command returns "ok"
- [ ] Final position: X=0.000 (verify in UGS)
- [ ] No error messages

**Actual Final Position:** X = ___________

---

### Test 3.2: Relative Mode (G91)
**Procedure:**
```gcode
G0 X0         # Go to X=0 first
G91           # Set relative mode
G0 X10        # Move +10 (now at X=10)
G0 X10        # Move +10 (now at X=20)
G0 X-20       # Move -20 (back to X=0)
G90           # Return to absolute
```

**Expected Results:**
- [ ] Each command returns "ok"
- [ ] Final position: X=0.000
- [ ] Relative moves accumulate correctly

**Actual Final Position:** X = ___________

---

### Test 3.3: Feed Rates
**Procedure:**
```gcode
G1 X30 F100   # Slow feed
G1 X60 F500   # Medium feed
G1 X90 F1500  # Fast feed
```

**Expected Results:**
- [ ] Slow move visibly slower than fast move
- [ ] No jerking or stuttering
- [ ] Each command returns "ok"

**Pass:** ☐ Yes ☐ No

**Notes:**
```
_________________________________________________________________
```

---

## Section 4: Work Coordinate System Test

### Test 4.1: Set Work Zero (G10 L20)
**Procedure:**
1. Move to arbitrary position
2. Send: `G10 L20 P1 X0 Y0 Z0 A0`
3. Query status: `?`
4. Check WPos in status report

**Expected Results:**
- [ ] Command returns "ok"
- [ ] WPos shows: 0.000, 0.000, 0.000, 0.000
- [ ] MPos unchanged (machine position preserved)

**Actual WPos:** X=___, Y=___, Z=___, A=___

---

### Test 4.2: Multiple Coordinate Systems (G54-G59)
**Procedure:**
```gcode
G54                      # Select G54
G0 X0 Y0                 # Go to G54 origin
G0 X50 Y0                # Move 50mm
G10 L20 P2 X0 Y0 Z0      # Set G55 zero here
G55                      # Switch to G55
```

**Expected Results:**
- [ ] Each command returns "ok"
- [ ] In G55, WPos shows: 0.000, 0.000 (at current location)
- [ ] Switching back to G54 shows WPos: 50.000, 0.000

**G54 Position:** X=___, Y=___
**G55 Position:** X=___, Y=___

---

## Section 5: Multi-Axis Motion Test

### Test 5.1: Sequential Motion
**Procedure:**
```gcode
G90
G0 X0 Y0 Z0              # Start at origin
G0 X10 Y10 Z5            # Multi-axis command
```

**Expected Results:**
- [ ] Command returns "ok"
- [ ] Axes move sequentially (X, then Y, then Z)
- [ ] NOT simultaneous/diagonal
- [ ] Final position: X=10, Y=10, Z=5

**Observed Sequence:**
1st axis moved: ___
2nd axis moved: ___
3rd axis moved: ___

---

### Test 5.2: Rectangle Pattern
**Procedure:**
```gcode
G0 X0 Y0
G0 X50 Y0
G0 X50 Y30
G0 X0 Y30
G0 X0 Y0
```

**Expected Results:**
- [ ] All commands return "ok"
- [ ] Machine traces rectangle (sequentially)
- [ ] Returns to exact 0,0 position
- [ ] No position drift

**Final Position:** X=___, Y=___

**Position Accuracy:**
- [ ] Within 0.1mm of expected
- [ ] Within 0.5mm of expected
- [ ] Greater than 0.5mm error (needs calibration)

---

## Section 6: M-Code Test

### Test 6.1: Spindle Commands (M3/M5)
**Procedure:**
```gcode
M3            # Spindle ON (manual control)
M5            # Spindle OFF (manual control)
```

**Expected Results:**
- [ ] Both commands return "ok"
- [ ] No error messages
- [ ] No actual spindle control (manual operation)
- [ ] Commands parsed for G-code compatibility

**Pass:** ☐ Yes ☐ No

---

### Test 6.2: Program Control (M0/M2)
**Procedure:**
```gcode
G0 X10
M0            # Program pause
# Press Resume in UGS
G0 X0
M2            # Program end
```

**Expected Results:**
- [ ] M0 pauses program, waits for resume
- [ ] Resume button continues execution
- [ ] M2 ends program successfully

**Pass:** ☐ Yes ☐ No

---

## Section 7: Jogging Test

### Test 7.1: Jog Commands ($J=)
**Procedure:**
```gcode
$J=G91 X5 F100     # Jog X +5mm
$J=G91 X-5 F100    # Jog X -5mm
$J=G91 Y5 F100     # Jog Y +5mm
$J=G91 Y-5 F100    # Jog Y -5mm
```

**Expected Results:**
- [ ] Each jog returns "ok"
- [ ] Motion stops at exact distance
- [ ] Final position: X=0, Y=0 (returned to start)

**Pass:** ☐ Yes ☐ No

---

### Test 7.2: Jog Safety Check
**Procedure:**
1. Start a move: `G1 X100 F100`
2. While moving, try: `$J=G91 Y5 F100`

**Expected Results:**
- [ ] Jog command rejected with `error:8` (Not Idle)
- [ ] Original move continues uninterrupted
- [ ] Jog only works when idle

**Pass:** ☐ Yes ☐ No

---

## Section 8: Full Test Program

### Test 8.1: Load Test File
**Procedure:**
1. In UGS: File → Open
2. Select `BISSO_E350_Test.gcode`
3. View in Visualizer window

**Expected Results:**
- [ ] File loads without errors
- [ ] Visualizer shows toolpath
- [ ] Estimated time displayed

**File Size:** __________ lines
**Estimated Time:** __________ minutes

---

### Test 8.2: Execute Test Program
**Procedure:**
1. Ensure 200mm clearance in all directions
2. Set work zero: `G10 L20 P1 X0 Y0 Z0 A0`
3. Select G54: `G54`
4. Click Send in UGS
5. Monitor execution

**Expected Results:**
- [ ] Program completes without errors
- [ ] All sections execute in sequence
- [ ] No emergency stops required
- [ ] Final position: 0.000, 0.000, 0.000, 0.000

**Actual Final Position:**
- X = ___________
- Y = ___________
- Z = ___________
- A = ___________

**Position Drift:** __________ mm (should be < 0.1mm)

**Execution Time:** __________ minutes

---

## Section 9: Error Handling Test

### Test 9.1: Invalid Command
**Procedure:**
```gcode
G999          # Invalid G-code
```

**Expected Results:**
- [ ] Returns: `error:1` or `error:20`
- [ ] Console shows error message
- [ ] Controller remains responsive

**Actual Response:**
```
_________________________________________________________________
```

---

### Test 9.2: Out of Range Value
**Procedure:**
```gcode
$100=-50      # Invalid negative PPM
```

**Expected Results:**
- [ ] Returns error code
- [ ] Setting NOT changed
- [ ] Verify: `$$` shows original value

**Pass:** ☐ Yes ☐ No

---

## Section 10: Performance Test

### Test 10.1: Status Update Rate
**Procedure:**
1. Start slow move: `G1 X100 F10`
2. Observe position updates in UGS
3. Count updates per second

**Expected Results:**
- [ ] Position updates smoothly (10 Hz typical)
- [ ] No jumps or freezing
- [ ] Consistent update rate

**Observed Update Rate:** __________ Hz

---

### Test 10.2: Command Throughput
**Procedure:**
1. Load test program
2. Send program at maximum speed
3. Monitor buffer status (Bf:X,Y in status)

**Expected Results:**
- [ ] Buffer fills and drains smoothly
- [ ] No buffer overflow errors
- [ ] Motion is smooth, not jerky

**Minimum Buffer Availability:** __________ blocks

---

## Section 11: Safety Systems Test

### Test 11.1: Emergency Stop Button
**Procedure:**
1. Start a move: `G1 X100 F50`
2. Press physical E-stop button
3. Observe behavior

**Expected Results:**
- [ ] Motion stops immediately
- [ ] Status shows "Alarm"
- [ ] Controller enters safe state
- [ ] Requires reset to clear

**Pass:** ☐ Yes ☐ No

---

### Test 11.2: Soft Limit Test (If Configured)
**Procedure:**
```gcode
$$                        # Check $130 (X max limit)
G0 X<beyond_limit>        # Try to exceed limit
```

**Expected Results:**
- [ ] Move rejected if beyond limit
- [ ] Error message displayed
- [ ] Machine doesn't crash into limit

**X Limit:** __________ mm
**Test Passed:** ☐ Yes ☐ No ☐ N/A (limits not set)

---

## Section 12: Encoder Test

### Test 12.1: Position Accuracy
**Procedure:**
1. Move to zero: `G0 X0`
2. Move 100mm: `G0 X100`
3. Measure actual distance with caliper
4. Return: `G0 X0`
5. Verify returns to exact zero

**Expected Results:**
- [ ] Measured distance: 100.0mm ± 0.2mm
- [ ] Returns to zero position
- [ ] No accumulating error

**Measured Distance:** __________ mm
**Return Accuracy:** __________ mm deviation

**Calibration Quality:**
- [ ] Excellent (< 0.1mm error)
- [ ] Good (0.1-0.5mm error)
- [ ] Needs Calibration (> 0.5mm error)

---

### Test 12.2: Repeatability Test
**Procedure:**
```gcode
# Repeat 5 times:
G0 X100
G0 X0
```

**Expected Results:**
- [ ] Returns to 0.000 all 5 times
- [ ] No drift over multiple cycles

**Cycle Results:**
1. Final X = ___________
2. Final X = ___________
3. Final X = ___________
4. Final X = ___________
5. Final X = ___________

**Maximum Deviation:** __________ mm

---

## Section 13: Communication Test

### Test 13.1: USB Stability
**Procedure:**
1. Send long test program (5+ minutes)
2. Monitor for disconnections
3. Check for communication errors

**Expected Results:**
- [ ] No disconnections during program
- [ ] No timeout errors
- [ ] Consistent communication

**Pass:** ☐ Yes ☐ No

---

### Test 13.2: Noise Immunity
**Procedure:**
1. Start a move
2. Wiggle USB cable (gently)
3. Turn on nearby motors/equipment

**Expected Results:**
- [ ] Communication remains stable
- [ ] No false commands from noise
- [ ] Position tracking maintained

**Pass:** ☐ Yes ☐ No

---

## Section 14: Overall System Assessment

### Functional Test Summary
- [ ] All basic moves (X/Y/Z/A) work correctly
- [ ] Status reporting functions properly
- [ ] Work coordinate systems operational
- [ ] Real-time commands responsive
- [ ] G-code parsing correct
- [ ] Error handling appropriate
- [ ] Emergency stop functional
- [ ] Position accuracy acceptable
- [ ] No communication issues

### Performance Rating
**Rate each category 1-5 (5 = Excellent):**

| Category | Rating | Notes |
|----------|--------|-------|
| Position Accuracy | ___/5 | |
| Motion Smoothness | ___/5 | |
| Response Time | ___/5 | |
| Communication Stability | ___/5 | |
| Error Handling | ___/5 | |
| Overall Reliability | ___/5 | |

**Total Score:** ___/30

**Overall Assessment:**
- [ ] 25-30: Excellent - Ready for production
- [ ] 20-24: Good - Minor tuning needed
- [ ] 15-19: Fair - Calibration required
- [ ] <15: Poor - Troubleshooting needed

---

## Section 15: Issues and Recommendations

### Issues Encountered:
```
Issue #1: ___________________________________________________

Resolution: _________________________________________________


Issue #2: ___________________________________________________

Resolution: _________________________________________________


Issue #3: ___________________________________________________

Resolution: _________________________________________________
```

### Recommendations:
```
[ ] No issues - system ready for use

[ ] Calibration needed:
    - [ ] X axis PPM
    - [ ] Y axis PPM
    - [ ] Z axis PPM
    - [ ] A axis PPM
    - [ ] Speed profiles

[ ] Hardware issues:
    - [ ] Encoder connections
    - [ ] USB cable quality
    - [ ] I2C expanders
    - [ ] Power supply

[ ] Software configuration:
    - [ ] Soft limits need setting
    - [ ] Feed rates need adjustment
    - [ ] Work coordinates need setup

[ ] Further testing required:
    _______________________________________________________
```

---

## Final Approval

### Test Results:
- **Pass:** ☐ System approved for production use
- **Conditional Pass:** ☐ System approved with noted conditions
- **Fail:** ☐ System requires fixes before use

### Signatures:

**Tester:** _____________________  **Date:** ___________

**Reviewer:** ___________________  **Date:** ___________

**Approved By:** ________________  **Date:** ___________

---

## Appendix A: Quick Troubleshooting

**If connection fails:**
1. Check COM port selection
2. Verify baud rate is 115200
3. Try different USB cable
4. Restart UGS and controller

**If position is inaccurate:**
1. Check encoder connections
2. Verify PPM calibration ($100-$103)
3. Run encoder diagnostics
4. Check mechanical slippage

**If motion is jerky:**
1. Check USB cable quality
2. Reduce status update rate
3. Verify buffer isn't overflowing
4. Check I2C communication

**If commands return errors:**
1. Check syntax (G-code format)
2. Verify machine is in Idle state
3. Clear alarms with $X if needed
4. Check soft limits aren't exceeded

---

**End of Validation Checklist**

For detailed setup instructions, see: `UGS_SETUP_GUIDE.md`
For test G-code file, see: `test_files/BISSO_E350_Test.gcode`

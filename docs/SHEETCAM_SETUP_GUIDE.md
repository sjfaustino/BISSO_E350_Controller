# SheetCAM to BISSO E350 Controller Setup Guide

**Version:** 1.0
**Date:** 2025-12-08
**For:** Stone Bridge Saw with Sequential Motion Architecture

---

## Table of Contents

1. [Overview](#overview)
2. [Critical Hardware Limitations](#critical-hardware-limitations)
3. [SheetCAM Configuration](#sheetcam-configuration)
4. [Post-Processor Setup](#post-processor-setup)
5. [Stone Cutting Parameters](#stone-cutting-parameters)
6. [Toolpath Strategies](#toolpath-strategies)
7. [Work Coordinate System Setup](#work-coordinate-system-setup)
8. [CAM to UGS Workflow](#cam-to-ugs-workflow)
9. [Testing and Validation](#testing-and-validation)
10. [Troubleshooting](#troubleshooting)

---

## 1. Overview

This guide explains how to configure SheetCAM TNG to generate G-code compatible with the BISSO E350 Controller's **sequential motion architecture** for stone bridge saw operations.

### Key Workflow

```
SheetCAM (CAD/CAM) → G-code File → UGS (Sender) → BISSO E350 Controller → Stone Cut
```

### What Makes This Controller Different

- **Sequential motion only** - one axis moves at a time
- **Three discrete speeds** - SLOW/MEDIUM/FAST (not variable)
- **Manual spindle control** - operator controls blade speed
- **Manual coolant/water** - operator controls water flow
- **Point-to-point motion** - no simultaneous multi-axis moves

---

## 2. Critical Hardware Limitations

### ⚠️ What the Controller CANNOT Do

| Feature | Status | Impact on CAM |
|---------|--------|---------------|
| Arc interpolation (G2/G3) | ❌ NOT SUPPORTED | Must convert arcs to line segments |
| Simultaneous multi-axis | ❌ NOT SUPPORTED | Only point-to-point moves |
| Variable spindle speed | ❌ NOT SUPPORTED | Fixed blade speed (manual) |
| Coolant control (M7/M8/M9) | ❌ NOT SUPPORTED | Manual water control |
| Canned cycles (G81-G89) | ❌ NOT SUPPORTED | Use explicit moves |
| Dwell (G4) | ❌ NOT SUPPORTED | Operator pauses manually |
| Tool changes (M6) | ❌ NOT SUPPORTED | Single blade only |

### ✅ What the Controller CAN Do

| Feature | Status | Usage |
|---------|--------|-------|
| Linear moves (G0/G1) | ✅ FULL SUPPORT | Primary motion command |
| Work coordinates (G54-G59) | ✅ FULL SUPPORT | Multiple part setups |
| Absolute/relative (G90/G91) | ✅ FULL SUPPORT | Flexible positioning |
| Feed rate control (F) | ✅ FULL SUPPORT | Three discrete speeds |
| Feed override (real-time) | ✅ FULL SUPPORT | 10%-200% adjustment |
| Jog mode ($J) | ✅ FULL SUPPORT | Safe manual positioning |
| Emergency stop (!) | ✅ FULL SUPPORT | Immediate halt |

### Speed Profiles

The controller maps commanded feed rates to three discrete PLC speeds:

| Feed Rate (F) | Profile | Typical Speed | Use Case |
|---------------|---------|---------------|----------|
| F1 - F10 | SLOW | 300 mm/min | Precision, corners, entry/exit |
| F11 - F30 | MEDIUM | ~1200 mm/min | General cutting |
| F31+ | FAST | 2400 mm/min | Rapids, traverses |

**Important:** The actual speeds are configured in the controller's calibration. These values are typical defaults.

---

## 3. SheetCAM Configuration

### 3.1 General Settings

**Tools → Options → General:**

```
Units: Millimeters (mm)
Angle: Degrees
Decimal places: 3
Grid spacing: 10mm
```

### 3.2 Machine Settings

**Tools → Options → Machine:**

```
Machine type: Router/Mill
Number of axes: 4 (X, Y, Z, A)
Work envelope:
  X: 0 to 3500mm  (adjust to your machine)
  Y: 0 to 2000mm
  Z: -300 to 0mm
  A: -360 to 360mm (rotary axis if used)

Safe Z: 50mm (clearance above material)
Rapid feed rate: 2400 mm/min (maps to FAST)
Default feed rate: 300 mm/min (maps to SLOW)
```

### 3.3 Post-Processor Selection

**Job → Machine:**

1. Select "Grbl" or use custom "BISSO_E350.scpost" (see Section 4)
2. Verify G-code dialect: Grbl 1.1h
3. Enable: "Use line numbers" (optional, helpful for debugging)
4. Disable: "Use canned cycles"
5. Disable: "Use tool changes"

### 3.4 Arc Settings (CRITICAL)

**Tools → Options → General → Arc fitting:**

```
☑ Convert arcs to lines
Line segment length: 2.0mm (for smooth curves)
Minimum segment length: 0.5mm
Maximum segment length: 5.0mm
```

**Why this matters:** The controller does not support G2/G3 arc commands. All circles, radii, and curves **must** be broken into straight line segments (G1).

**For stone cutting:**
- 2mm segments give smooth visual appearance
- Smaller segments (0.5-1mm) for tight radii <50mm
- Larger segments (5mm) acceptable for gentle curves
- More segments = smoother cut but longer G-code file

---

## 4. Post-Processor Setup

You have two options:

### Option A: Use Built-in "Grbl" Post-Processor (Quick Start)

SheetCAM's built-in Grbl post-processor works reasonably well:

1. **Job → Machine → Post processor:** Select "Grbl.scpost"
2. **Verify output:** Check generated G-code removes M7/M8/M9
3. **Test:** Run sample job and verify compatibility

### Option B: Custom BISSO E350 Post-Processor (Recommended)

A custom post-processor provides:
- Optimized output for sequential motion
- Automatic M3/M5 handling (comments only)
- Better feed rate management
- Work coordinate system support
- Informative comments for operator

**Installation:**

1. Copy `BISSO_E350.scpost` to SheetCAM's "posts" folder:
   - **Windows:** `C:\Program Files\SheetCam\posts\`
   - **Linux:** `~/.SheetCam/posts/` or `/usr/share/sheetcam/posts/`
   - **Mac:** `/Applications/SheetCam.app/Contents/Resources/posts/`

2. Restart SheetCAM

3. **Job → Machine → Post processor:** Select "BISSO E350 (Grbl 1.1h)"

**Post-processor features:**
- Outputs only G0, G1, G10, G54-G59, G90, G91, M0, M2
- Adds operator notes for spindle/water control
- Inserts safety comments at critical points
- Formats G-code for UGS compatibility

---

## 5. Stone Cutting Parameters

### 5.1 Feed Rate Guidelines

**For Granite/Marble (200-300mm diamond blade):**

| Operation | Feed Rate (F) | Profile | Typical Speed |
|-----------|---------------|---------|---------------|
| Plunge/Entry | F5 | SLOW | 300 mm/min |
| Fine detail | F8 | SLOW | 300 mm/min |
| Normal cut (10-30mm thick) | F15 | MEDIUM | 1200 mm/min |
| Fast cut (thin <10mm) | F25 | MEDIUM | 1200 mm/min |
| Rapids/positioning | F50 | FAST | 2400 mm/min |
| Exit move | F5 | SLOW | 300 mm/min |

**Adjust based on:**
- Stone hardness (softer = faster)
- Stone thickness (thinner = faster)
- Blade condition (worn = slower)
- Water flow (more water = can go faster)
- Finish quality (slower = smoother)

### 5.2 Blade Specifications

**Record your blade specifications:**

```
Blade diameter: _______ mm
Blade thickness: _______ mm (kerf width)
RPM: _______ (manual control)
Water flow: _______ L/min (manual control)
Material: Diamond segmented / Continuous rim
```

### 5.3 Safety Parameters

```
Safe Z height: 50mm minimum (blade fully clear)
Material thickness: _______ mm
Part zero: Top of material / Bottom of material
Clamp clearance: Mark clamp positions on drawing
Lead-in distance: 5-10mm before cut starts
Lead-out distance: 5-10mm after cut ends
```

---

## 6. Toolpath Strategies

### 6.1 Supported Operations

**✅ Profile (outside cut):**
```
- Use line tool
- Set lead-in/lead-out (5mm recommended)
- Direction: Climb vs conventional (blade rotation)
- Tabs: Use sparingly, manual break-off
```

**✅ Pocket:**
```
- Use rectangular pocket tool
- Convert arcs to lines
- Stepover: 80-90% of blade width
- Direction: Conventional for stone
```

**✅ Rectangle cut:**
```
- Perfect for sequential motion
- One axis at a time naturally
- Corners: Use corner slowdown
```

**✅ Drill (straight plunge):**
```
- Use drill tool
- Peck depth: 5-10mm per pass for thick stone
- Retract height: Full clear (Safe Z)
```

**❌ NOT Recommended:**
```
- Spiral toolpaths (requires simultaneous axes)
- Helical entry (requires G2/G3)
- Trochoidal milling (requires arcs)
- Adaptive clearing (too complex for sequential)
```

### 6.2 Entry/Exit Strategies

**Entry methods (start of cut):**

1. **Straight plunge (simple):**
   ```gcode
   G0 X10 Y20 Z50        (rapid to position above)
   F5                    (slow feed)
   G1 Z-25               (plunge to cut depth)
   F15                   (normal cutting feed)
   G1 X100               (start cutting)
   ```

2. **Ramped entry (gentler on blade):**
   ```gcode
   G0 X0 Y20 Z50         (rapid to start)
   F5                    (slow feed)
   G1 X10 Y20 Z-2.5      (ramp in 10mm, depth 2.5mm per axis)
   G1 X20 Y20 Z-5
   G1 X30 Y20 Z-7.5
   G1 X40 Y20 Z-10       (full depth reached)
   F15                   (switch to cutting speed)
   ```

**Exit methods (end of cut):**

```gcode
F5                       (slow down before exit)
G1 X200 Y100             (finish cut at slow speed)
G0 Z50                   (rapid to safe height)
```

### 6.3 Corner Handling

**Problem:** Abrupt direction changes can chip stone or damage blade.

**Solution:** Slow down before corners:

```gcode
G1 X100 Y50 F15          (approaching corner at medium speed)
F5                       (slow down)
G1 X100 Y100             (turn corner at slow speed)
F15                      (speed back up)
G1 X150 Y100             (continue cutting)
```

**In SheetCAM:**
- Tools → Define tools → Cutting → Advanced → "Corner slowdown"
- Set slowdown factor: 30-50% of cutting speed
- Set slowdown distance: 5-10mm before corner

---

## 7. Work Coordinate System Setup

The controller supports 6 work coordinate systems (G54-G59) for multiple part setups.

### 7.1 Single Part Setup (G54)

**Typical workflow:**

1. **Position slab on table**
   - Secure with clamps
   - Measure slab dimensions
   - Mark origin point (usually front-left corner)

2. **Jog to origin in UGS:**
   ```
   - Jog X/Y to front-left corner of slab
   - Jog Z to top surface of slab (or bottom, your choice)
   - Verify position is safe
   ```

3. **Zero the work coordinate:**
   ```
   In UGS command box:
   G10 L20 P1 X0 Y0 Z0

   This sets G54 so current position = (0,0,0)
   ```

4. **Verify:**
   ```
   ? (status query)

   Should show:
   WPos: 0.000,0.000,0.000  (work position at zero)
   MPos: 123.456,234.567,345.678  (machine position unchanged)
   ```

5. **In SheetCAM:**
   - Set origin to match (front-left-top)
   - Insert `G54` at start of program
   - All coordinates relative to this zero

### 7.2 Multiple Part Setup (G54-G59)

**Example: 3 slabs on table:**

```
Slab 1: Front-left  → G54
Slab 2: Center      → G55
Slab 3: Back-right  → G56
```

**Setup procedure:**

1. **Set G54 (Slab 1):**
   ```
   Jog to Slab 1 origin
   G10 L20 P1 X0 Y0 Z0
   ```

2. **Set G55 (Slab 2):**
   ```
   Jog to Slab 2 origin
   G10 L20 P2 X0 Y0 Z0
   ```

3. **Set G56 (Slab 3):**
   ```
   Jog to Slab 3 origin
   G10 L20 P3 X0 Y0 Z0
   ```

4. **In SheetCAM - create 3 separate G-code files:**
   ```
   slab1_part.gcode  → starts with G54
   slab2_part.gcode  → starts with G55
   slab3_part.gcode  → starts with G56
   ```

5. **Run in UGS:**
   ```
   Load slab1_part.gcode → Run → Complete
   Load slab2_part.gcode → Run → Complete
   Load slab3_part.gcode → Run → Complete
   ```

**All offsets persist in controller NVS (non-volatile storage)** - even after power cycle!

---

## 8. CAM to UGS Workflow

### Complete workflow from drawing to cut:

```
┌─────────────────────────────────────────────────────────┐
│ STEP 1: Design (CAD)                                    │
├─────────────────────────────────────────────────────────┤
│ • Create drawing in CAD software (AutoCAD, Fusion, etc) │
│ • Export as DXF or SVG                                  │
│ • Verify dimensions match actual slab                   │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 2: SheetCAM (CAM)                                  │
├─────────────────────────────────────────────────────────┤
│ • Import DXF/SVG                                        │
│ • Set material thickness                                │
│ • Create toolpaths (profile, pocket, drill)            │
│ • Set feed rates (F5-F50)                              │
│ • Preview toolpaths                                     │
│ • Post-process to G-code                               │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 3: G-code Review (Text Editor)                    │
├─────────────────────────────────────────────────────────┤
│ • Open .nc or .gcode file                              │
│ • Verify no G2/G3 commands                             │
│ • Check feed rates (F values)                          │
│ • Verify safe Z heights                                │
│ • Add operator notes if needed                         │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 4: UGS Preparation                                │
├─────────────────────────────────────────────────────────┤
│ • Connect UGS to controller                            │
│ • Load G-code file                                     │
│ • Visualize toolpath (check bounds)                    │
│ • Verify work coordinate (G54-G59)                     │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 5: Machine Setup                                  │
├─────────────────────────────────────────────────────────┤
│ • Secure slab on table                                 │
│ • Verify clamps clear toolpath                         │
│ • Jog to origin position                               │
│ • Set work coordinate: G10 L20 P1 X0 Y0 Z0            │
│ • Jog to Safe Z                                        │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 6: Pre-run Checks                                 │
├─────────────────────────────────────────────────────────┤
│ ☑ Emergency stop button tested                        │
│ ☑ Blade condition inspected                           │
│ ☑ Water system ready (manual control)                 │
│ ☑ Operator at machine (do not walk away!)            │
│ ☑ Safety glasses on                                   │
│ ☑ No loose clothing/jewelry                           │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 7: Cut Execution                                  │
├─────────────────────────────────────────────────────────┤
│ 1. Start blade (manual control)                        │
│ 2. Turn on water flow (manual control)                 │
│ 3. Verify blade at correct speed                       │
│ 4. In UGS: Click "Send"                                │
│ 5. Monitor cut continuously                            │
│ 6. Use feed override if needed (50-200%)              │
│ 7. Emergency stop (!) if anything wrong               │
│ 8. Wait for "ok" completion message                    │
│ 9. Stop blade (manual control)                         │
│ 10. Turn off water (manual control)                    │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ STEP 8: Post-cut                                       │
├─────────────────────────────────────────────────────────┤
│ • Inspect cut quality                                  │
│ • Measure finished part                                │
│ • Remove slab from table                               │
│ • Clean machine                                        │
│ • Log any issues for next time                         │
└─────────────────────────────────────────────────────────┘
```

### 8.1 File Naming Convention

Use clear, descriptive names:

```
projectname_partname_operation_date.gcode

Examples:
kitchen_countertop_profile_2025-12-08.gcode
bathroom_sink_cutout_2025-12-08.gcode
fireplace_surround_pockets_2025-12-08.gcode
```

### 8.2 G-code File Organization

**Recommended folder structure:**

```
/SheetCAM_Projects/
  /Kitchen_Remodel_2025/
    /CAD/
      countertop.dxf
      backsplash.svg
    /CAM/
      countertop.job (SheetCAM file)
    /GCode/
      countertop_profile.gcode
      countertop_sink_cutout.gcode
      backsplash_profile.gcode
    /Documentation/
      setup_notes.txt
      material_specs.txt
```

---

## 9. Testing and Validation

### 9.1 Dry Run (Air Cut)

**Always test new programs without cutting first!**

1. **Load G-code in UGS**
2. **Raise Z by 50mm:**
   ```
   G91              (relative mode)
   G0 Z50           (raise 50mm)
   G90              (back to absolute)
   ```
3. **Run program** - blade should stay clear of material
4. **Watch for:**
   - Unexpected rapids
   - Clamp collisions
   - Out-of-bounds moves
   - Strange toolpaths

5. **If all good:** Lower Z back down and run for real

### 9.2 Test Cut Checklist

**Before cutting expensive stone, test on scrap:**

- [ ] Load scrap material (same thickness)
- [ ] Set work coordinate (G54)
- [ ] Run simple rectangle cut
- [ ] Measure finished cut
- [ ] Check accuracy (±0.5mm typical)
- [ ] Inspect edge quality
- [ ] Verify all axes move correctly

### 9.3 Sample Test Programs

**Test 1: Simple Rectangle (50mm x 100mm)**

```gcode
(Test Rectangle - 50x100mm)
G21 (mm)
G90 (absolute)
G54 (work coordinate)
G0 Z50 (safe height)
G0 X0 Y0 (start position)

F5 (slow)
G1 Z-10 (plunge 10mm into material)

F15 (cutting speed)
G1 X100 (move right)
G1 Y50 (move forward)
G1 X0 (move left)
G1 Y0 (move back - complete rectangle)

F5 (slow)
G0 Z50 (retract to safe height)
M0 (program stop)
```

**Test 2: Speed Profile Test**

```gcode
(Speed Profile Test - verify SLOW/MED/FAST)
G21 G90 G54
G0 Z50
G0 X0 Y0

(SLOW profile - should engage SPEED_PROFILE_1)
F5
G1 X50

(MEDIUM profile - should engage SPEED_PROFILE_2)
F15
G1 X100

(FAST profile - should engage SPEED_PROFILE_3)
F50
G1 X150

G0 Z50
M0
```

**Watch the speed change at each F command!**

---

## 10. Troubleshooting

### 10.1 Common SheetCAM Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| G-code contains G2/G3 | Arc conversion disabled | Enable "Convert arcs to lines" |
| Program won't run on controller | Unsupported command | Review supported G/M codes |
| Blade speeds up/slows randomly | Feed rate fluctuation | Use consistent F values per section |
| Controller rejects M7/M8/M9 | Coolant commands | Use M3/M5 only (or remove) |
| Toolpath looks wrong | Wrong post-processor | Use Grbl or BISSO_E350.scpost |

### 10.2 G-code Validation

**Quick check - open G-code in text editor:**

```bash
# Search for unsupported commands:
grep "G2\|G3\|G4\|G28\|G30\|M6\|M7\|M8\|M9" file.gcode

# If found any → fix in SheetCAM and regenerate
```

**Supported commands checklist:**
- ✅ G0, G1 (motion)
- ✅ G10 (WCS)
- ✅ G54-G59 (WCS select)
- ✅ G90, G91 (abs/rel)
- ✅ M0, M2 (stop)
- ✅ M3, M5 (no-op, ok to include)
- ✅ Comments in parentheses ()

### 10.3 Feed Rate Issues

**Problem:** Controller uses wrong speed profile

**Check:** Feed rate mapping

```
If F=5 but controller runs FAST:
→ Check calibration ($100-$132 settings)
→ Verify speed mapping in motion_control.cpp

If cuts are too slow:
→ Increase F value (F15 → F25)
→ Adjust controller calibration for faster profiles
```

**Problem:** Sudden speed changes

**Cause:** F values jumping around in G-code

**Solution:** Use consistent feed rates per operation
```gcode
(BAD - speed changes every line)
F10
G1 X10
F25
G1 Y10
F5
G1 X20

(GOOD - consistent speed per section)
F10
G1 X10
G1 Y10
G1 X20
F25
G1 X30
```

### 10.4 Stone-Specific Issues

| Problem | Likely Cause | Solution |
|---------|--------------|----------|
| Chipping at entry | Too fast entry | Use F5 for first 10mm |
| Chipping at exit | Too fast exit | Slow to F5 before exit |
| Rough edge finish | Feed too fast | Reduce F value 25-50% |
| Blade binding | Feed too slow | Increase F value |
| Blade wandering | Not enough water | Check water flow (manual) |
| Blade overheating | Blade speed too low | Check blade RPM (manual) |

### 10.5 Controller Error Messages

**Common errors from UGS:**

```
error:2  → Bad number format
error:3  → Invalid $ command
error:8  → Not idle (can't jog while moving)
error:9  → G-code locked (emergency stop active)
error:20 → Unsupported command
error:21 → Soft limit exceeded
error:22 → Homing not complete
```

**If you get error:20 (unsupported command):**
1. Check G-code for G2/G3/G4/G28/G92
2. Check for invalid M-codes (M6, M7, M8, M9)
3. Verify post-processor settings

---

## Summary: Quick Start Checklist

### For Your First Cut:

1. **SheetCAM Setup:**
   - [ ] Units: mm
   - [ ] Arc fitting: Convert to lines (2mm segments)
   - [ ] Post-processor: Grbl or BISSO_E350
   - [ ] No canned cycles, no tool changes

2. **Design Simple Test Part:**
   - [ ] Rectangle or simple profile
   - [ ] 50-100mm size
   - [ ] Single depth (-10mm)

3. **Generate G-code:**
   - [ ] Preview toolpath in SheetCAM
   - [ ] Post-process to .gcode file
   - [ ] Review file in text editor
   - [ ] Verify no G2/G3 commands

4. **UGS Setup:**
   - [ ] Connect to controller
   - [ ] Load G-code file
   - [ ] Visualize (check bounds)
   - [ ] Jog to origin
   - [ ] Set G54: `G10 L20 P1 X0 Y0 Z0`

5. **Machine Prep:**
   - [ ] Secure scrap material
   - [ ] Verify clamps clear
   - [ ] Start blade (manual)
   - [ ] Start water (manual)

6. **Test Run:**
   - [ ] Raise Z +50mm for dry run
   - [ ] Run program (watch carefully!)
   - [ ] If OK → lower Z and run for real

7. **Monitor Cut:**
   - [ ] Watch continuously
   - [ ] Adjust feed override if needed
   - [ ] Emergency stop button ready
   - [ ] Wait for completion

8. **Measure Result:**
   - [ ] Check dimensions
   - [ ] Inspect edge quality
   - [ ] Adjust parameters if needed
   - [ ] Document settings for future

---

## Appendix: Supported G-code Reference

### Linear Motion
```gcode
G0 X__ Y__ Z__ A__ F__    (rapid positioning)
G1 X__ Y__ Z__ A__ F__    (linear feed move)
```

### Work Coordinates
```gcode
G54        (select work coordinate system 1)
G55        (select work coordinate system 2)
G56 ... G59 (WCS 3-6)
G10 L20 P1 X0 Y0 Z0  (set G54 offset)
```

### Distance Mode
```gcode
G90        (absolute positioning)
G91        (relative/incremental positioning)
```

### Program Control
```gcode
M0         (program stop - operator resume)
M2         (program end)
M112       (emergency stop)
```

### Comments
```gcode
(comment text)     (parentheses comments)
; comment text     (semicolon comments - entire line)
```

---

## Additional Resources

- **SheetCAM Manual:** [https://www.sheetcam.com/UserGuide/](https://www.sheetcam.com/UserGuide/)
- **UGS Setup Guide:** See `docs/UGS_SETUP_GUIDE.md`
- **Controller Documentation:** See `docs/` folder
- **Test G-code Files:** See `test_files/` folder

---

**For technical support or questions about this controller, contact your system integrator or refer to the project documentation.**

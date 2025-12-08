# SheetCAM to UGS Complete Workflow Guide

**Version:** 1.0
**Date:** 2025-12-08
**For:** BISSO E350 Stone Bridge Saw Controller

---

## Quick Start: 10-Minute First Cut

For experienced users who want to get cutting fast:

```
1. SheetCAM: Import DXF → Create toolpath → Post (Grbl) → Save .gcode
2. UGS: Connect → Home → Load file → Visualize → Check bounds
3. Machine: Clamp material → Jog to origin → G10 L20 P1 X0 Y0 Z0
4. Safety: Start blade (manual) → Start water (manual) → Emergency stop ready
5. Cut: Click Send in UGS → Monitor continuously → Adjust feed if needed
6. Finish: Wait for completion → Stop blade → Stop water → Inspect part
```

**For detailed step-by-step, continue reading...**

---

## Table of Contents

1. [Workflow Overview](#workflow-overview)
2. [Prerequisites](#prerequisites)
3. [Phase 1: Design (CAD)](#phase-1-design-cad)
4. [Phase 2: CAM (SheetCAM)](#phase-2-cam-sheetcam)
5. [Phase 3: G-code Review](#phase-3-g-code-review)
6. [Phase 4: Machine Setup](#phase-4-machine-setup)
7. [Phase 5: Cut Execution](#phase-5-cut-execution)
8. [Phase 6: Post-Cut](#phase-6-post-cut)
9. [Advanced: Multiple Part Workflow](#advanced-multiple-part-workflow)
10. [Optimization Tips](#optimization-tips)

---

## 1. Workflow Overview

### The Complete Process

```
┌──────────────┐
│   CUSTOMER   │ → Drawing/sketch/template
│   REQUEST    │
└──────┬───────┘
       │
       ↓
┌──────────────┐
│ PHASE 1: CAD │ → DXF or SVG file
│   DESIGN     │   (AutoCAD, Fusion360, Inkscape, etc.)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│ PHASE 2: CAM │ → G-code file (.gcode or .nc)
│  (SheetCAM)  │   + Toolpath preview
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  PHASE 3:    │ → Validated G-code
│ G-CODE REVIEW│   (no errors, safe toolpaths)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  PHASE 4:    │ → Material secured
│MACHINE SETUP │   Work coordinate set (G54)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  PHASE 5:    │ → Finished cut
│CUT EXECUTION │   (monitor continuously!)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│ PHASE 6:     │ → Quality checked
│  POST-CUT    │   Documented for future
└──────────────┘
```

### Time Estimates

| Phase | Simple Part (rectangle) | Complex Part (curves) |
|-------|-------------------------|-----------------------|
| CAD design | 5-10 minutes | 30-60 minutes |
| SheetCAM setup | 5 minutes | 15-30 minutes |
| G-code review | 2 minutes | 5-10 minutes |
| Machine setup | 10 minutes | 15 minutes |
| Cut execution | 5-30 minutes | 30-180 minutes |
| Post-cut inspection | 5 minutes | 10-15 minutes |
| **Total** | **30-60 minutes** | **2-5 hours** |

---

## 2. Prerequisites

### Software Requirements

**SheetCAM TNG:**
- Version: 7.0 or newer recommended
- License: Standard or Pro (Dev version OK for testing)
- Download: https://www.sheetcam.com

**Universal GCode Sender (UGS Platform):**
- Version: 2.0 or newer
- Download: https://winder.github.io/ugs_website/download/
- See: `UGS_SETUP_GUIDE.md` for installation

**Text Editor (for G-code review):**
- Notepad++ (Windows)
- gedit (Linux)
- TextEdit (Mac)
- VSCode with G-code extension (advanced)

**Optional but helpful:**
- CAD software (AutoCAD, Fusion360, LibreCAD, Inkscape)
- CAM Simulator (for visualizing toolpaths)

### Hardware Requirements

**Computer:**
- Windows 7/10/11, Linux, or macOS
- 4GB RAM minimum
- USB 2.0 port
- Modern processor (any from last 10 years OK)

**Controller:**
- BISSO E350 Controller (ESP32-S3 based)
- USB cable (Type-A to Micro/Type-C depending on board)
- Firmware v3.5.25 or newer (Grbl 1.1h compatible)

**Machine:**
- Bridge saw properly calibrated (see calibration docs)
- All axes homed and functional
- Encoder system working (verify with `encoder status`)
- Emergency stop tested and functional

### Knowledge Requirements

**You should understand:**
- [ ] Basic CNC concepts (X/Y/Z axes, feed rate, rapids)
- [ ] Work coordinate systems (what G54 means)
- [ ] Sequential motion limitation (one axis at a time)
- [ ] Safety procedures for stone cutting
- [ ] How to operate emergency stop

**If you're new to CNC:**
1. Read this entire document first
2. Practice with dry runs (Z raised, no cutting)
3. Start with simple rectangular cuts
4. Gradually increase complexity
5. Never leave machine unattended

---

## 3. Phase 1: Design (CAD)

### Drawing Requirements

**What you need to know before starting:**

```
Material:
  Type: _____________ (granite, marble, etc.)
  Thickness: ________ mm
  Dimensions: _______ x _______ mm

Part specifications:
  Dimensions: Drawing or sketch
  Tolerances: ±_______ mm (typical: ±0.5mm)
  Edge finish: As-cut / Polished / Chamfered
  Quantity: _______ pieces
```

### Creating the Drawing

**Option 1: Import Existing Drawing**
- Customer provides DXF/DWG from architect/designer
- Open in CAD software
- Verify dimensions match actual requirements
- Export as clean DXF (R12/R14 format most compatible)

**Option 2: Create New Drawing**

1. **Choose CAD software:**
   - **AutoCAD/AutoCAD LT:** Industry standard, expensive
   - **Fusion360:** Free for hobbyists, powerful
   - **LibreCAD:** Free, open source, 2D only
   - **Inkscape:** Free, great for artistic curves

2. **Set units to millimeters:**
   ```
   This is critical! All measurements in mm.
   SheetCAM expects mm input.
   ```

3. **Draw part outline:**
   - Use actual finished dimensions
   - Include all cutouts (sinks, cooktop, etc.)
   - Add reference dimensions for verification

4. **Important considerations:**
   - **Blade kerf:** Don't compensate in CAD (do it in SheetCAM)
   - **Corner radii:** Inside corners ≥ blade radius (typically 150mm for 300mm blade)
   - **Line work:** Use polylines/splines, not text or dimensions
   - **Layers:** Organize by operation (profile, pocket, drill)

5. **Export as DXF:**
   ```
   File → Export → DXF
   Version: R12 ASCII (most compatible)
   Units: mm
   Save as: project_name.dxf
   ```

### Drawing Best Practices

**✅ Do:**
- Use consistent units (mm throughout)
- Draw to actual size (1:1 scale)
- Close all polylines (no gaps)
- Use separate layers for different operations
- Include reference points for alignment
- Save original file + exported DXF

**❌ Avoid:**
- Blocks/groups (may not export correctly)
- Text annotations (will be treated as geometry)
- Dimensions (visual only, not cutting paths)
- 3D objects (SheetCAM needs 2D)
- Tiny gaps in lines (<0.1mm may cause issues)

---

## 4. Phase 2: CAM (SheetCAM)

### 2A: Initial Setup (One-Time)

**Configure SheetCAM for BISSO E350:**

1. **Launch SheetCAM**

2. **Tools → Options:**
   ```
   General:
     Units: Millimeters
     Decimal places: 3
     Grid: 10mm

   Arc fitting:
     ☑ Convert arcs to lines
     Line segment length: 2.0mm
     Min segment: 0.5mm
     Max segment: 5.0mm
   ```

3. **Job → Machine:**
   ```
   Machine type: Router/Mill
   X: 0 to 3500mm (adjust to your machine)
   Y: 0 to 2000mm
   Z: -300 to 0mm
   A: -360 to 360mm (if you use rotary axis)

   Safe Z: 50mm
   Rapid feed: 2400 mm/min
   ```

4. **Install post-processor:**
   - Copy `BISSO_E350.scpost` to SheetCAM/posts/ folder
   - Or use built-in "Grbl.scpost"
   - **Job → Machine → Post processor:** Select "BISSO E350" or "Grbl"

5. **Define tool (diamond blade):**
   ```
   Tools → Tool → New Tool → Line tool

   Name: Diamond Blade 350mm
   Type: Line tool
   Diameter: 350mm (your blade diameter)
   Kerf width: 2.8mm (your blade thickness)
   Feed rate: 15 mm/min (baseline for granite)
   Plunge rate: 5 mm/min
   ```

6. **Save as default:** File → Save defaults

### 2B: Create New Job

**For each new project:**

1. **File → New**

2. **Job → Machine → Material:**
   ```
   Name: Black Granite 30mm
   Thickness: 30mm
   Rapid clearance: 10mm (above material)
   ```

3. **File → Import Drawing:**
   ```
   Select: project_name.dxf
   Click: Import
   ```

4. **Verify import:**
   - Drawing appears in SheetCAM workspace
   - Check scale (measure known dimension)
   - Check orientation (X/Y correct)
   - All lines imported

### 2C: Create Toolpaths

#### Profile Cut (Outside Edge)

1. **Options → Profile:**
   ```
   Tool: Diamond Blade 350mm
   Operation: No offset / Outside offset / Inside offset
     - Outside offset: Cut outside line (for external profile)
     - Inside offset: Cut inside line (for pocket)
     - No offset: Cut on line

   Cut depth: 30mm (material thickness)
   Direction: Climb / Conventional
     - Climb: Blade rotates with feed direction
     - Conventional: Blade rotates against feed
   ```

2. **Lead in/out:**
   ```
   Lead in:
     Type: Line
     Length: 10mm
     Angle: Perpendicular

   Lead out: Same as lead in
   ```

3. **Advanced → Corner slowdown:**
   ```
   ☑ Enable corner slowdown
   Slowdown distance: 10mm
   Slowdown factor: 0.3 (30% of cutting speed)
   ```

4. **Select entities:**
   - Click on lines that form the profile
   - Lines turn red when selected
   - Right-click → Accept

5. **Preview:**
   - Toolpath appears in green
   - Verify direction (arrows)
   - Check lead-in/out positions

#### Pocket Cut (Internal Cutout)

1. **Options → Pocket:**
   ```
   Tool: Diamond Blade 350mm
   Offset: Inside
   Cut depth: 30mm

   Stepover: 280mm (80% of blade diameter)
   Direction: Conventional recommended

   Finish allowance: 0mm (or 0.5mm for finish pass)
   ```

2. **Pattern:**
   ```
   Pattern: Offset (most efficient for sequential motion)
   Corner style: Rounded (natural for blade)
   ```

3. **Select boundary:**
   - Click on lines forming pocket boundary
   - Right-click → Accept

4. **Preview:**
   - Toolpath spirals inward
   - Verify complete material removal

#### Drill/Plunge Holes

1. **Options → Drill:**
   ```
   Tool: Diamond Blade 350mm
   Depth: 30mm
   Peck depth: 10mm (for thick material)
   Retract: 5mm
   ```

2. **Select points:**
   - Click on circle centers or points
   - Right-click → Accept

### 2D: Preview and Verify

1. **View → Show toolpaths:**
   ```
   ☑ Show toolpaths
   ☑ Show rapids
   ☑ Show leads
   ```

2. **Visual checks:**
   - [ ] Toolpath covers all intended cuts
   - [ ] Rapids don't collide with clamps (add clamp positions)
   - [ ] Lead-ins don't crash into material
   - [ ] Cut depth is correct
   - [ ] Toolpath order makes sense

3. **Simulate:**
   ```
   View → Run → Play
   Watch toolpath execute in 3D view
   Verify no issues
   ```

### 2E: Post-Process (Generate G-code)

1. **File → Post process:**
   ```
   Post processor: BISSO E350 (or Grbl)
   Output file: project_name.gcode
   Options:
     ☑ Line numbers (optional, helpful for debugging)
     ☐ Tool changes (not needed, single blade)
   ```

2. **Click OK**

3. **G-code file created!**
   ```
   Location: Same folder as .job file (or specify)
   Extension: .gcode or .nc
   ```

4. **Save SheetCAM job:**
   ```
   File → Save as: project_name.job
   (Allows editing later if needed)
   ```

---

## 5. Phase 3: G-code Review

**Never skip this step!** Reviewing G-code catches errors before they damage material or machine.

### 3A: Open G-code in Text Editor

```
Right-click project_name.gcode → Open with Notepad++
```

### 3B: Visual Inspection

**Look for:**

1. **Header information:**
   ```gcode
   (BISSO E350 Bridge Saw Controller)
   (Date: 2025-12-08 ...)
   (Material: Black Granite 30mm)
   ```

2. **Initialization:**
   ```gcode
   G21 (Units: mm)
   G90 (Absolute positioning)
   G54 (Work coordinate system)
   G0 Z50 (Safe height)
   ```

3. **Operator notes:**
   ```gcode
   (1. MANUALLY START BLADE before running)
   (2. MANUALLY START WATER FLOW before cutting)
   ```

### 3C: Error Check

**Search for unsupported commands:**

**In Notepad++ or text editor:**
```
Ctrl+F (Find)

Search for: G2|G3|G4|G28|G30|M6|M7|M8|M9

If found → ERROR! Fix in SheetCAM and regenerate
```

**Supported commands checklist:**
- ✅ G0, G1 (motion)
- ✅ F (feed rate)
- ✅ X, Y, Z, A (coordinates)
- ✅ G10 (WCS setting)
- ✅ G54-G59 (WCS selection)
- ✅ G90, G91 (absolute/relative)
- ✅ M0, M2 (stop/end)
- ✅ M3, M5 (spindle on/off - no-op, OK to include)
- ✅ Comments in parentheses: (like this)

**Common issues:**

| Problem | How to identify | Fix |
|---------|-----------------|-----|
| Arc commands | G2 or G3 in file | SheetCAM: Enable "Convert arcs to lines" |
| Dwell commands | G4 P__ | Remove or convert to M0 (pause) |
| Tool change | M6 T__ | Remove (single tool only) |
| Coolant | M7, M8, M9 | Remove or ignore (manual control) |

### 3D: Feed Rate Check

**Scan through file, verify F values make sense:**

```gcode
F100         (rapid - OK, maps to FAST profile)
G0 X100 Y50

F5           (plunge - OK, maps to SLOW profile)
G1 Z-30

F15          (cutting - OK, maps to MEDIUM profile)
G1 X500

F5           (exit - OK, maps to SLOW)
G1 X550

F100         (rapid out - OK)
G0 Z50
```

**Red flags:**
- Feed rate changing every line (inefficient, causes speed fluctuation)
- Very slow feeds for rapids (F1 for G0 - wastes time)
- Very fast feeds for plunges (F100 for Z down - dangerous!)

### 3E: Bounds Check

**Note the min/max coordinates:**

```gcode
Look through file for largest X, Y, Z values:

Max X: 1250.0
Max Y: 750.0
Min Z: -30.0

Does this match your expectations?
Will this fit in material?
Will this exceed machine limits?
```

### 3F: Final Safety Check

**Before proceeding, confirm:**

- [ ] No unsupported G/M codes
- [ ] Feed rates appropriate (F5-F100 range)
- [ ] Coordinates within machine limits
- [ ] Safe Z height adequate (≥50mm)
- [ ] Toolpath makes logical sense
- [ ] File size reasonable (<1MB for most jobs)

**If all checks pass → proceed to machine setup**

---

## 6. Phase 4: Machine Setup

### 4A: Material Preparation

1. **Inspect material:**
   - [ ] No cracks or damage
   - [ ] Measure actual thickness (may differ from nominal)
   - [ ] Clean surface (no dust/debris)
   - [ ] Mark reference edge for alignment

2. **Position on table:**
   ```
   - Place flat against reference edge (if equipped)
   - Orient grain direction (if applicable)
   - Leave clearance for clamps
   - Consider where offcut will fall
   ```

3. **Secure with clamps:**
   - [ ] Use adequate clamps (don't under-clamp!)
   - [ ] Clamps don't interfere with toolpath
   - [ ] Material doesn't rock or flex
   - [ ] Check clamping didn't distort thin material

4. **Mark clamp positions on drawing:**
   ```
   Critical! Prevents accidental collision.
   Visual reference during cutting.
   ```

### 4B: Controller Setup

1. **Power on controller**

2. **Connect UGS:**
   ```
   UGS Platform:
     Port: [Select COM port]
     Baud: 115200
     Click: Connect

   Should see:
     Grbl 1.1h [' for help]
     ok
   ```

3. **Home all axes:**
   ```
   Console: $H

   Wait for all axes to home (may take 60-90 seconds)
   Status should show "Idle" when complete
   ```

4. **Verify encoder positions:**
   ```
   Console: ?

   Should see:
   <Idle|MPos:0.000,0.000,0.000,0.000|...>

   All zeros = good!
   ```

### 4C: Work Coordinate Setup

**This is the most critical step!** Setting work coordinate (G54) defines where part zero is.

**Typical setup:**
```
Part zero = Front-left-top corner of material
  X = 0: Left edge
  Y = 0: Front edge
  Z = 0: Top surface
```

**Procedure:**

1. **Jog to origin position:**
   ```
   UGS: Use jog controls (keyboard arrows or buttons)

   Step 1: Jog X/Y to front-left corner
     - Use FAST speed to get close (arrow keys)
     - Switch to SLOW speed for precision (Shift+arrows)
     - Position blade center at origin point

   Step 2: Jog Z to top surface
     - Lower Z slowly (SLOW speed!)
     - Use paper slip method:
       - Place paper on material
       - Lower blade until paper just drags
       - Raise Z by paper thickness (~0.1mm)
     - Or touch-off precisely
   ```

2. **Set work coordinate:**
   ```
   UGS Console:
   G10 L20 P1 X0 Y0 Z0

   This sets G54 so current machine position = work position (0,0,0)
   ```

3. **Verify:**
   ```
   Console: ?

   Should see:
   <Idle|MPos:1234.567,2345.678,345.678|WPos:0.000,0.000,0.000|...>

   Check: WPos = 0,0,0 ✓
   ```

4. **Test jog:**
   ```
   Move away: G0 X10 Y10 Z10
   Return: G0 X0 Y0 Z0

   Should return to exact origin ✓
   ```

### 4D: Load and Visualize G-code

1. **Load file:**
   ```
   UGS: File → Open
   Select: project_name.gcode
   ```

2. **Visualizer tab:**
   ```
   View 3D preview of toolpath

   Check:
   - [ ] Toolpath entirely over material (not off the edge)
   - [ ] No collisions with clamps
   - [ ] Reasonable looking path
   - [ ] Bounds match expectations
   ```

3. **View in table coordinates:**
   ```
   Visualizer should show toolpath starting at (0,0,0)
   Ending positions should make sense
   Z should be negative (below material surface)
   ```

4. **Bounds check:**
   ```
   UGS shows:
   X: 0.0 to 1250.0
   Y: 0.0 to 750.0
   Z: 0.0 to -30.0

   Verify: These dimensions fit material ✓
   ```

---

## 7. Phase 5: Cut Execution

### 5A: Pre-Cut Safety Check

**Complete this checklist before clicking "Send":**

- [ ] **Material:** Securely clamped, no movement
- [ ] **Clamps:** Clear of toolpath
- [ ] **Work coordinate:** G54 set and verified
- [ ] **G-code:** Loaded and visualized
- [ ] **Emergency stop:** Tested and functional
- [ ] **Operator:** Wearing PPE (safety glasses, hearing protection)
- [ ] **Area:** Clear of other people
- [ ] **Lighting:** Adequate visibility
- [ ] **Blade:** Inspected (no damage, segments >3mm)
- [ ] **Water:** System ready to start

### 5B: Start Blade and Water (Manual)

**⚠️ BISSO E350 has manual spindle and water control!**

1. **Start blade:**
   ```
   - Turn on blade motor (outside controller)
   - Wait for blade to reach full speed
   - Listen for smooth operation (no wobble/vibration)
   - Verify correct RPM for material
   ```

2. **Start water flow:**
   ```
   - Turn on water valve/pump
   - Adjust flow rate for cut depth
   - Aim nozzle at cut point
   - Verify good flow, no leaks
   ```

3. **Final verification:**
   ```
   - Blade running smoothly ✓
   - Water flowing adequately ✓
   - Operator ready ✓
   - Emergency stop within reach ✓
   ```

### 5C: Start the Cut

1. **UGS: Review status:**
   ```
   Status: Idle
   WPos: 0.000, 0.000, 0.000
   File loaded: project_name.gcode
   ```

2. **Click "Send" button in UGS**

3. **Program starts:**
   ```
   Controller begins executing G-code
   Status changes to "Run"
   Position values update in real-time
   ```

4. **Initial movements (typical):**
   ```
   G0 Z50        (raise to safe height)
   G0 X10 Y10    (rapid to start position)
   F5            (slow feed)
   G1 Z-30       (plunge to cut depth)
   F15           (cutting feed)
   G1 X100       (start cutting!)
   ```

### 5D: Monitor the Cut

**Operator must attend machine continuously!**

**Watch for:**

1. **Visual:**
   - [ ] Blade cutting smoothly through material
   - [ ] No cracks developing in stone
   - [ ] Water flow adequate (good flushing)
   - [ ] Clamps still secure
   - [ ] Toolpath matches expected

2. **Auditory:**
   - [ ] Steady cutting sound (not grinding or squealing)
   - [ ] No unusual vibrations
   - [ ] Motor not straining

3. **UGS Display:**
   - [ ] Status: "Run" (not "Alarm" or "Hold")
   - [ ] Position updating smoothly
   - [ ] No error messages
   - [ ] Progress bar advancing

**Normal sounds:**
- Steady cutting: shhhhhhhh (good)
- Water splashing: splish-splash (good)
- Blade whistling slightly: wheeeee (good)

**Abnormal sounds:**
- Grinding: GRRRRRRR (blade dull or feed too slow)
- Squealing: EEEEEEEE (blade binding or feed too fast)
- Chattering: BRRRRRRR (vibration, bad mounting)
- Motor straining: RRRRRR (feed too fast or blade dull)

**If anything wrong:** Hit emergency stop (!) immediately

### 5E: Real-Time Adjustments

**Feed override:**

```
UGS: Feed Override slider (10%-200%)

If blade cutting easily → increase to 125%
If blade struggling → decrease to 75%
If blade binding → decrease to 50% or emergency stop

Typical adjustments:
- Start at 100%
- Watch first 30 seconds
- Adjust as needed
- Document successful percentage for next time
```

**Manual pause (if needed):**

```
UGS: Click "Pause" or press ! (feed hold)

Machine stops immediately (blade keeps spinning!)

Use for:
- Check material
- Adjust clamps
- Clear stone slurry
- Investigate unusual sound

Resume: Click "Resume" or press ~ (cycle start)
```

### 5F: Program Completion

**When cut finishes:**

```
UGS shows:
- Status: Idle
- Message: ok (program complete)
- Progress: 100%
```

**Typical end-of-program:**
```gcode
G0 Z50           (retract to safe height)
G0 X0 Y0         (return to origin)
M0               (program stop)
```

**Now stop blade and water:**

1. **Stop water flow** (manual valve)
2. **Stop blade motor** (wait for full stop before approaching!)
3. **Wait 30-60 seconds** for blade to spin down completely

---

## 8. Phase 6: Post-Cut

### 6A: Initial Inspection

**Before removing material:**

1. **Visual check (while still clamped):**
   - [ ] Cut complete (blade went all the way through?)
   - [ ] No obvious cracks
   - [ ] Edges look clean
   - [ ] No pieces ready to fall

2. **Remove clamps carefully:**
   ```
   - Support offcut pieces (don't let them fall and crack)
   - Loosen clamps gradually
   - Watch for sharp edges
   ```

3. **Remove part from table:**
   ```
   - Lift carefully (stone is heavy!)
   - Use two people if >25kg
   - Watch for sharp edges
   - Set on padded surface
   ```

### 6B: Quality Inspection

**Dimensional check:**

```
Measure critical dimensions with calibrated tools:

Length: _______ mm (Spec: _______±_____ mm) ✓/✗
Width:  _______ mm (Spec: _______±_____ mm) ✓/✗
Depth:  _______ mm (Spec: _______±_____ mm) ✓/✗

Holes/cutouts:
  Position: _______ mm (Spec: _______±_____ mm) ✓/✗
  Size:     _______ mm (Spec: _______±_____ mm) ✓/✗
```

**Edge quality:**

- [ ] **Excellent:** Smooth, no chips, clean arris
- [ ] **Good:** Minor roughness, tiny chips <1mm
- [ ] **Acceptable:** Rough but within spec, can be polished
- [ ] **Unacceptable:** Large chips, cracks, out of spec

**Document issues:**
```
Issue: Small chip at exit point, SE corner
Size: 2mm x 1mm
Cause: Exit feed too fast
Acceptable: Yes (will be hidden)
Corrective action: Reduce exit feed to F3 next time
```

### 6C: Secondary Operations (If Needed)

**Polishing:**
- Use progression of polishing pads (50→100→200→400→800→1500→3000 grit)
- Wet polish with water
- Check finish between grits

**Chamfering/easing edges:**
- Removes sharp arris (edge corner)
- Safer to handle
- Use chamfer bit or hand grinder
- Consistent bevel (typically 1-2mm)

**Cleaning:**
- Remove stone dust/slurry
- Clean with water (no harsh chemicals)
- Dry completely before sealing/installing

### 6D: Documentation

**Create cut record (for future reference):**

```
PROJECT: Kitchen Countertop
DATE: 2025-12-08
OPERATOR: [Your Name]

MATERIAL:
  Type: Black Galaxy Granite
  Thickness: 30mm
  Supplier: ABC Stone Supply

G-CODE FILE: kitchen_countertop_profile.gcode

MACHINE PARAMETERS:
  Blade: 350mm diamond segmented, 1440 RPM
  Water: Medium flow (~5 L/min)
  Feed rates:
    Entry: F5 (SLOW)
    Cutting: F15 (MEDIUM)
    Exit: F5 (SLOW)
  Feed override: Started 100%, adjusted to 110% during cut

CUT TIME: 12 minutes
BLADE CONDITION: Good (used for ~50 linear meters so far)

RESULTS:
  Dimensions: All within ±0.5mm ✓
  Edge quality: Excellent, minimal chipping
  Issues: Minor chip (2mm) at exit point, SE corner
  Acceptable: Yes per specification

NOTES:
  - Material cut very smoothly
  - Could potentially increase feed to F18 next time
  - Exit point chip - reduce exit feed to F3

NEXT STEPS:
  - Polish edges to 800 grit
  - Chamfer top edge (1mm)
  - Ready for installation
```

**File documentation:**
```
Save to project folder:
  /Projects/Kitchen_Countertop_2025/
    CAD/countertop.dxf
    CAM/countertop.job (SheetCAM file)
    GCode/countertop.gcode
    Documentation/cut_record_2025-12-08.txt
    Photos/finished_part.jpg
```

### 6E: Machine Cleanup

**After each cut:**

- [ ] Remove stone slurry from table (squeegee + sponge)
- [ ] Clean blade (remove buildup)
- [ ] Check blade condition (segments, cracks)
- [ ] Drain water system (if not recirculating)
- [ ] Wipe down machine surfaces
- [ ] Inspect for any damage or issues

**End of day:**

- [ ] Deep clean machine
- [ ] Lubricate as needed (per maintenance schedule)
- [ ] Check all axes for smooth movement
- [ ] Inspect cables and connections
- [ ] Empty slurry collection (if equipped)
- [ ] Cover machine (protect from dust)

---

## 9. Advanced: Multiple Part Workflow

### Using Multiple Work Coordinates (G54-G59)

**Scenario:** 3 identical sink cutouts on single large slab

**Setup:**

1. **Position all three sinks on slab**

2. **Jog to each position and set WCS:**
   ```
   Sink 1 (left):
     Jog to origin
     G10 L20 P1 X0 Y0 Z0  (sets G54)

   Sink 2 (center):
     Jog to origin
     G10 L20 P2 X0 Y0 Z0  (sets G55)

   Sink 3 (right):
     Jog to origin
     G10 L20 P3 X0 Y0 Z0  (sets G56)
   ```

3. **Create three G-code files in SheetCAM:**
   ```
   sink_cutout.gcode → Modify header to use G54
   (copy file)
   sink_cutout_2.gcode → Change G54 to G55
   sink_cutout_3.gcode → Change G54 to G56
   ```

4. **Run in sequence:**
   ```
   UGS: Load sink_cutout.gcode → Send → Complete
        Load sink_cutout_2.gcode → Send → Complete
        Load sink_cutout_3.gcode → Send → Complete
   ```

**Advantage:** All offsets saved in NVS, persist even after power cycle!

### Batch Production

**For multiple identical parts:**

1. **Create master G-code file (first part)**

2. **For each subsequent part:**
   ```
   - Clamp new material
   - Jog to origin
   - Set G54: G10 L20 P1 X0 Y0 Z0
   - Load same G-code file
   - Run
   ```

3. **Track blade wear:**
   ```
   After every 10 parts, inspect blade
   Document total linear meters cut
   Replace blade at recommended interval
   ```

---

## 10. Optimization Tips

### Speed Up CAM Process

**Create SheetCAM templates:**
```
File → Save as Template
  - Machine settings
  - Material thickness options
  - Tool definitions
  - Common operations (profile, pocket)

New job: File → New from template
```

**Define multiple tools:**
```
Tool 1: Rough cut (F20, faster)
Tool 2: Finish cut (F12, slower, better edge)

Use rough tool for most of cut
Use finish tool for final pass
```

### Reduce G-code File Size

**Large files (curves with many segments) can be slow to send:**

1. **Optimize segment length:**
   ```
   Tight curves (<50mm radius): 1mm segments
   Normal curves: 2mm segments
   Gentle curves (>500mm radius): 5mm segments
   ```

2. **Remove unnecessary comments:**
   ```
   Post-processor options: Minimal comments
   (But keep critical operator notes!)
   ```

3. **Use decimal places wisely:**
   ```
   3 decimal places: X123.456 (adequate precision)
   Don't use 6 decimal places: X123.456789 (overkill, larger file)
   ```

### Improve Cut Quality

**Corner quality:**
- Reduce feed 50% for 10mm before/after corner
- Consider corner radius (even 1-2mm helps)

**Edge finish:**
- Use sharp blade
- Slower feed for final pass
- Adequate water flow
- Check blade RPM correct for material

**Accuracy:**
- Calibrate machine regularly (verify $100-$103)
- Check blade runout (<0.5mm)
- Ensure material flat (no warp/bow)

### Save Time

**Efficient toolpath order:**
```
1. Drill holes first (if any)
2. Inside cuts (pockets, cutouts)
3. Outside profile last (part remains supported)
```

**Minimize air moves:**
- SheetCAM: Tools → Options → Lead ins → minimize distance
- Organize operations to reduce long rapids

**Use appropriate speeds:**
- Don't use F5 for everything (too slow!)
- Use F50-F100 for rapids (FAST profile)
- Use F15-F25 for cutting (MEDIUM profile)
- Use F5 only for critical operations (entry/exit)

---

## Summary Checklists

### One-Page Pre-Cut Checklist

**Material:**
- [ ] Type/thickness verified
- [ ] Securely clamped
- [ ] Clamps clear of toolpath

**Software:**
- [ ] G-code reviewed (no errors)
- [ ] Loaded in UGS
- [ ] Visualized (looks correct)
- [ ] Work coordinate set (G54)

**Machine:**
- [ ] Controller connected
- [ ] Axes homed ($H)
- [ ] Encoders reading correctly
- [ ] Emergency stop tested

**Safety:**
- [ ] PPE worn (glasses, hearing protection)
- [ ] Area clear of people
- [ ] Adequate lighting
- [ ] Blade inspected (no damage)

**Operation:**
- [ ] Blade started (manual)
- [ ] Water flowing (manual)
- [ ] Operator ready to monitor
- [ ] Emergency stop within reach

**→ Ready to click Send!**

---

### Quick Troubleshooting

| Problem | Quick Fix |
|---------|-----------|
| UGS won't connect | Check USB cable, try different port, check baud (115200) |
| Controller says "error:20" | G-code has unsupported command (G2/G3), fix in SheetCAM |
| Blade binding | Emergency stop, reduce feed 30%, check blade sharpness |
| Chipping | Slow down feed, check blade type, slow entry/exit |
| Inaccurate dimensions | Check calibration ($100-$103), verify work coordinate |
| File won't load in UGS | Check file size (<10MB), verify .gcode extension |

---

## Additional Resources

**Documentation:**
- `SHEETCAM_SETUP_GUIDE.md` - Detailed SheetCAM configuration
- `STONE_CUTTING_BEST_PRACTICES.md` - Feed rates, materials, techniques
- `UGS_SETUP_GUIDE.md` - UGS installation and configuration
- `test_files/BISSO_E350_Test.gcode` - Sample test program

**Online Resources:**
- SheetCAM Manual: https://www.sheetcam.com/UserGuide/
- SheetCAM Forum: https://www.sheetcam.com/forum/
- UGS Documentation: https://winder.github.io/ugs_website/
- Grbl Wiki: https://github.com/gnea/grbl/wiki

---

**Now you're ready to go from CAD drawing to finished stone part!**

**Remember:**
- Start simple (rectangles first)
- Always dry-run new programs (Z raised)
- Never leave machine unattended during cutting
- Document successful parameters for future reference

**Happy cutting!** 🪨✂️

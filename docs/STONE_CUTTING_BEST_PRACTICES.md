# Stone Cutting Best Practices for BISSO E350 Controller

**Version:** 1.0
**Date:** 2025-12-08
**Application:** CNC Stone Bridge Saw with Sequential Motion

---

## Table of Contents

1. [Material Properties](#material-properties)
2. [Feed Rate Selection](#feed-rate-selection)
3. [Blade Selection and Maintenance](#blade-selection-and-maintenance)
4. [Water Management](#water-management)
5. [Toolpath Strategies](#toolpath-strategies)
6. [Entry and Exit Techniques](#entry-and-exit-techniques)
7. [Corner Strategies](#corner-strategies)
8. [Safety Considerations](#safety-considerations)
9. [Quality Control](#quality-control)
10. [Troubleshooting Common Issues](#troubleshooting-common-issues)

---

## 1. Material Properties

### Stone Hardness Classification

Understanding stone hardness helps determine appropriate cutting parameters:

| Material | Mohs Hardness | Relative Cutting Speed | Feed Rate Multiplier |
|----------|---------------|------------------------|----------------------|
| **Soft Stones** | | | |
| Soapstone | 1-2 | Very Fast | 2.0x |
| Alabaster | 2-3 | Very Fast | 1.8x |
| Limestone | 3-4 | Fast | 1.5x |
| Marble | 3-4 | Fast | 1.3x |
| Travertine | 3-4 | Fast | 1.4x |
| **Medium Stones** | | | |
| Onyx | 4-5 | Medium | 1.0x (baseline) |
| Slate | 4-5 | Medium | 1.0x |
| Sandstone | 4-6 | Medium | 1.1x |
| **Hard Stones** | | | |
| Granite | 6-7 | Slow | 0.7x |
| Quartzite | 7 | Slow | 0.6x |
| Engineered Quartz | 7 | Very Slow | 0.5x |
| **Very Hard** | | | |
| Porcelain | 7-8 | Very Slow | 0.4x |

### Thickness Considerations

Material thickness significantly affects cutting parameters:

| Thickness | Feed Rate Adjustment | Water Flow | Notes |
|-----------|----------------------|------------|-------|
| < 10mm | 1.5x | Low | Risk of cracking, go faster to avoid heat |
| 10-20mm | 1.2x | Medium | Standard tile thickness |
| 20-40mm | 1.0x | Medium | Standard slab thickness (baseline) |
| 40-60mm | 0.8x | High | Increased blade engagement |
| 60-100mm | 0.6x | High | Very deep cuts, risk of blade binding |
| > 100mm | 0.5x | Very High | Multiple passes recommended |

### Material Characteristics Impact

**Grain structure:**
- Fine grain → Faster feed, cleaner finish
- Coarse grain → Slower feed, more chipping risk
- Layered (slate) → Feed perpendicular to layers when possible

**Porosity:**
- Dense (granite) → Slower feed, more water
- Porous (limestone) → Faster feed, absorbs water
- Resin-filled → Moderate feed, watch for resin buildup

**Brittleness:**
- Brittle (marble) → Gentle entry/exit, corner slowdown
- Tough (quartzite) → Can handle higher speeds
- Cracked material → Extremely slow, risk of breakage

---

## 2. Feed Rate Selection

### Controller Speed Profile Mapping

The BISSO E350 maps commanded feed rates to three discrete hardware profiles:

```
F1 - F9    → SLOW    (~300 mm/min)
F10 - F29  → MEDIUM  (~1200 mm/min)
F30+       → FAST    (~2400 mm/min)
```

**Important:** The actual speeds depend on your controller calibration. Check `$100-$132` settings in UGS.

### Recommended Feed Rates by Operation

#### General Cutting (20-30mm granite baseline)

| Operation | F Value | Profile | Typical Speed | Use Case |
|-----------|---------|---------|---------------|----------|
| **Rapid positioning** | F100 | FAST | 2400 mm/min | Moving to start position |
| **Approach to material** | F50 | FAST | 2400 mm/min | Moving close to material |
| **Entry/plunge** | F5 | SLOW | 300 mm/min | First contact with stone |
| **Rough cutting** | F20 | MEDIUM | 1200 mm/min | Bulk material removal |
| **Finish cutting** | F12 | MEDIUM | 1200 mm/min | Final edge quality |
| **Detail work** | F8 | SLOW | 300 mm/min | Tight corners, intricate shapes |
| **Exit move** | F5 | SLOW | 300 mm/min | Last 10mm before leaving cut |

#### Material-Specific Examples

**Soft Limestone (30mm thick):**
```
Entry:      F8  (SLOW)
Cutting:    F25 (MEDIUM)
Corners:    F15 (MEDIUM)
Exit:       F8  (SLOW)
```

**Medium Granite (30mm thick):**
```
Entry:      F5  (SLOW)
Cutting:    F15 (MEDIUM)
Corners:    F8  (SLOW)
Exit:       F5  (SLOW)
```

**Hard Quartzite (30mm thick):**
```
Entry:      F3  (SLOW)
Cutting:    F10 (MEDIUM)
Corners:    F5  (SLOW)
Exit:       F3  (SLOW)
```

**Thin Marble Tile (12mm):**
```
Entry:      F8  (SLOW)
Cutting:    F20 (MEDIUM)
Corners:    F12 (MEDIUM)
Exit:       F8  (SLOW)
```

### Feed Rate Optimization Process

**Step 1: Start Conservative**
- Use baseline feed rates from tables above
- Monitor blade performance during initial cuts
- Listen for unusual sounds (grinding, squealing)

**Step 2: Evaluate Results**
- ✅ **Good cut:** Smooth edge, minimal chipping, blade cuts freely
- ⚠️ **Too fast:** Rough edge, excessive chipping, blade binding, blade deflection
- ⚠️ **Too slow:** Glazed edge, burn marks, excessive wear, blade heating

**Step 3: Adjust Feed Rate**
```
If too fast:  Reduce F value by 20-30% (F20 → F15)
If too slow:  Increase F value by 20-30% (F15 → F20)
```

**Step 4: Document**
Keep a log of successful parameters for each material type:
```
Material: Black Granite, 30mm
Blade: 350mm diamond segmented, 1440 RPM
Water: Medium flow
Feed: F5 entry, F15 cutting, F5 exit
Result: Excellent edge quality, 3.2 minutes for 1000mm cut
```

### Real-Time Feed Override

During cutting, use UGS feed override to adjust speed:

- **50%** - Emergency slowdown if blade binding
- **75%** - Blade struggling, slow down
- **100%** - Normal operation (programmed speed)
- **125%** - Blade cutting easily, can speed up
- **150%** - Material is softer than expected
- **200%** - Maximum override (use cautiously!)

**How to use:**
1. Start cut at 100%
2. Monitor blade performance for first 30 seconds
3. Adjust override slider in UGS if needed
4. Document successful override percentage
5. Update G-code F values for next cut

---

## 3. Blade Selection and Maintenance

### Diamond Blade Types

| Blade Type | Bond | Best For | Cut Quality | Speed | Cost |
|------------|------|----------|-------------|-------|------|
| **Segmented** | Metal | Granite, hard stone | Good | Fast | Low |
| **Turbo** | Metal | General purpose | Very Good | Medium | Medium |
| **Continuous Rim** | Metal/Resin | Marble, tile | Excellent | Slow | Medium |
| **Bridge Saw Specific** | Metal | All stone | Excellent | Medium | High |

### Blade Specifications

**Critical parameters:**

```
Blade diameter:    _______ mm (typical: 250-450mm for bridge saws)
Bore diameter:     _______ mm (must match spindle arbor)
Blade thickness:   _______ mm (affects kerf width, typically 2.2-3.0mm)
Maximum RPM:       _______ (never exceed blade rating!)
Segment height:    _______ mm (when <3mm, replace blade)
Number of segments: ______ (more = smoother but slower)
```

### Blade Speed (RPM) Selection

**Surface speed formula:**
```
Surface Speed (m/min) = (π × Diameter(mm) × RPM) / 1000

Example: 350mm blade at 1440 RPM
= (3.14159 × 350 × 1440) / 1000
= 1,583 m/min
```

**Recommended surface speeds:**

| Material | Surface Speed (m/min) | Example RPM (350mm blade) |
|----------|----------------------|---------------------------|
| Marble | 1800-2200 | 1640-2000 |
| Granite | 1400-1800 | 1270-1640 |
| Quartzite | 1200-1600 | 1090-1450 |
| Engineered Stone | 1000-1400 | 910-1270 |
| Porcelain | 800-1200 | 730-1090 |

**⚠️ IMPORTANT:** BISSO E350 has **manual spindle control**. Operator must set correct blade RPM before starting program!

### Blade Maintenance

**Daily checks:**
- [ ] Inspect segments for wear (measure height)
- [ ] Check for cracks or damage
- [ ] Verify blade is clean (remove stone slurry buildup)
- [ ] Check blade mounting (tight arbor nut, no wobble)
- [ ] Verify blade runs true (< 0.5mm runout)

**Weekly maintenance:**
- [ ] Deep clean blade (remove resin/pitch buildup)
- [ ] Dress blade if glazed (use dressing stick)
- [ ] Check arbor for wear
- [ ] Inspect blade guard and safety features
- [ ] Lubricate spindle bearings if applicable

**Blade life indicators:**

| Symptom | Likely Cause | Action |
|---------|--------------|--------|
| Segments < 3mm high | Normal wear | Replace blade |
| Blade cuts slowly | Glazed segments | Dress blade or replace |
| Excessive vibration | Cracked or damaged | Replace immediately |
| Blade wanders | Worn arbor or blade | Check mounting, replace if needed |
| Burns/scorch marks | Wrong RPM or too slow feed | Check parameters |
| Excessive chipping | Blade too aggressive or dull | Try different blade type |

---

## 4. Water Management

### Functions of Water/Coolant

1. **Cooling:** Prevents blade overheating and extends life
2. **Lubrication:** Reduces friction between blade and stone
3. **Dust suppression:** Keeps silica dust from becoming airborne (critical for health!)
4. **Flushing:** Removes stone slurry from cut kerf
5. **Finish quality:** Improves edge finish

### Water Flow Requirements

**⚠️ BISSO E350 has manual water control.** Operator must adjust flow rate appropriately.

| Cut Depth | Material | Minimum Flow | Recommended Flow | Notes |
|-----------|----------|--------------|------------------|-------|
| < 20mm | Any | 1 L/min | 2-3 L/min | Shallow cuts |
| 20-40mm | Soft/Medium | 2 L/min | 3-5 L/min | Standard slab |
| 20-40mm | Hard/Dense | 3 L/min | 5-7 L/min | More heat generation |
| 40-80mm | Any | 4 L/min | 7-10 L/min | Deep cuts |
| > 80mm | Any | 6 L/min | 10-15 L/min | Very deep cuts |

**Signs of insufficient water:**
- Blade overheating (discoloration)
- Excessive dust generation
- Glazed blade segments
- Burn marks on stone edge
- Blade binding or squealing

**Signs of excessive water:**
- Stone slurry flooding work area
- Difficulty seeing cut line
- Water splashing excessively
- Waste of water resources

### Water Delivery Methods

**Flood cooling (recommended for bridge saws):**
- Continuous stream directed at cut point
- 3-10 L/min typical
- Adjust aim for best flushing

**Mist cooling (alternative):**
- Fine mist reduces water usage
- 0.5-2 L/min
- May not provide adequate flushing for deep cuts

**Blade cooling holes (if equipped):**
- Some blades have holes for internal cooling
- Requires special spindle with water feed
- Very effective for thick cuts

### Water Quality

**Use clean water:**
- Tap water is usually adequate
- Avoid hard water (mineral buildup on blade)
- Change water in recirculation system regularly
- Filter slurry to prevent pump damage

**Additives (optional):**
- Rust inhibitors (if machine prone to rust)
- Surfactants (improve wetting, reduce surface tension)
- Anti-foam agents (for recirculation systems)

**⚠️ Do NOT use:**
- Oil-based coolants (creates messy slurry)
- Soap (foams excessively)
- Chemicals that may stain stone

---

## 5. Toolpath Strategies

### Sequential Motion Considerations

The BISSO E350 controller moves **one axis at a time**. Understanding this is critical for efficient toolpath planning.

**Controller behavior:**
```gcode
G1 X100 Y50 F20

Controller executes as:
1. Move X to 100mm (Y stays at current position)
2. Wait for X to reach target
3. Move Y to 50mm (X now stationary)
4. Wait for Y to reach target
5. Command complete
```

**This means:**
- Diagonal moves are actually "staircase" motion
- No true simultaneous interpolation
- Toolpath appears segmented, not smooth diagonal

**Implications for CAM:**
- Circle becomes octagon/polygon (more segments = smoother)
- 45° line becomes series of X/Y steps
- Arc conversion to line segments is REQUIRED

### Optimal Toolpath Types for Sequential Motion

#### ✅ Rectangle/Box Cuts (Excellent)

Sequential motion is naturally suited for rectangular shapes:

```gcode
G0 X0 Y0       (rapid to start)
F5             (slow entry)
G1 Z-25        (plunge - Z axis only)
F15            (cutting speed)
G1 X500        (X axis only - fast)
G1 Y300        (Y axis only - fast)
G1 X0          (X axis only - fast)
G1 Y0          (Y axis only - fast)
```

**Advantages:**
- Each side is pure single-axis move
- Controller executes efficiently
- Predictable cutting speed
- Easy to visualize and debug

#### ✅ Grid/Pocket Patterns (Excellent)

Cutting grid of squares or pockets:

```gcode
(Row 1)
G1 X500        (X only)
G1 Y50         (Y only - step over)
G1 X0          (X only - return)
G1 Y100        (Y only - next row)
(Row 2)
G1 X500        (X only)
...
```

**Advantages:**
- Minimal wasted motion
- Natural fit for sequential architecture
- Good for multiple sink cutouts, etc.

#### ⚠️ Diagonal Lines (Acceptable)

Diagonal moves work but become stepped:

```gcode
G1 X100 Y100   (intended: 45° diagonal)

Actual execution:
X moves to 100, then Y moves to 100
Result: "L" shape, not diagonal line
```

**For cutting:** The blade still cuts the stone in a generally diagonal direction because it's engaged throughout both axis moves. The finished edge will be straight.

**Mitigation:**
- Break diagonal into many small segments
- Shorter segments = smoother apparent motion
- Example: Instead of one long diagonal, use 10 small stair-steps

#### ❌ Circles/Arcs (Requires Conversion)

True arcs (G2/G3) not supported. Must convert to line segments:

**Poor (few segments):**
```gcode
(20 segments for 100mm diameter circle)
G1 X15.6 Y4.9
G1 X13.5 Y8.7
G1 X10.6 Y12.1
...
Result: Visible facets, looks like polygon
```

**Good (many segments):**
```gcode
(100 segments for 100mm diameter circle)
G1 X15.6 Y4.9
G1 X15.4 Y5.3
G1 X15.2 Y5.7
...
Result: Appears smooth, acceptable finish
```

**Segment calculation:**
```
For radius R, segment length S:
Number of segments = (2 × π × R) / S

Example: 100mm radius, 2mm segments
= (2 × 3.14159 × 100) / 2
= 314 segments

SheetCAM setting: "Line segment length: 2.0mm"
```

### Lead-In and Lead-Out

**Purpose:** Avoid starting/ending cut directly on final edge (prevents divots, chips)

**Types:**

1. **Line Lead-In (simple):**
   ```
   Approach from outside, cut 5-10mm past edge, then start actual profile
   Leaves small tab to break off
   ```

2. **Perpendicular Lead-In (better for sequential):**
   ```
   Approach perpendicular to edge
   Plunge to depth
   Turn 90° and start cutting
   Natural fit for sequential motion
   ```

3. **Arc Lead-In (requires conversion):**
   ```
   Curved entry (must be converted to line segments)
   More complex, offers smooth entry
   May not provide significant benefit given sequential motion
   ```

**Recommended lead-in length:**
- Soft stone: 5mm
- Medium stone: 7-10mm
- Hard stone: 10-15mm
- Very thick (>50mm): 15-20mm

### Multiple Pass Strategies

For thick material (>60mm), consider multiple passes:

**Option 1: Full-depth single pass**
```
Pros: Faster (one pass)
Cons: High blade load, more wear, risk of binding
Use for: <60mm thickness
```

**Option 2: Multiple depth passes**
```
Pass 1: Cut to 30mm depth
Pass 2: Cut to 60mm depth (full depth)

Pros: Easier on blade, safer, better finish
Cons: Takes longer, requires careful Z positioning
Use for: >60mm thickness, hard materials
```

**Option 3: Climb vs conventional (material dependent)**
```
Climb cutting: Blade rotation same as feed direction
  - Sharper edge on exit side
  - More chipping on entry side
  - Better for brittle materials

Conventional: Blade rotation opposite feed direction
  - Sharper edge on entry side
  - More pressure on blade
  - Better for tough materials
```

---

## 6. Entry and Exit Techniques

### Entry Methods

#### Method 1: Straight Plunge (Simplest)

**Use for:** Pockets, internal cutouts, non-critical edges

```gcode
G0 X100 Y100 Z50    (position above entry point)
F5                  (slow feed)
G1 Z-25             (plunge to full depth)
F15                 (switch to cutting feed)
G1 X200             (start horizontal cut)
```

**Pros:**
- Simple G-code
- Fast entry
- Predictable

**Cons:**
- Can cause chipping at entry point
- High impact on blade
- May crack thin/brittle material

**Tips:**
- Use slowest feed (F5) for plunge
- Enter at location that will be cut away later
- Avoid thin sections

#### Method 2: Ramped Entry (Gentler)

**Use for:** External profiles, critical edges, thin material

```gcode
G0 X50 Y100 Z50     (start before edge)
F5                  (slow feed)
G1 X60 Y100 Z-2.5   (ramp down gradually)
G1 X70 Y100 Z-5
G1 X80 Y100 Z-7.5
G1 X90 Y100 Z-10
G1 X100 Y100 Z-12.5
G1 X110 Y100 Z-15   (reach full depth)
F15                 (speed up)
G1 X200 Y100        (continue cutting)
```

**Pros:**
- Gentler on blade and material
- Reduces entry chipping
- Safer for thin material

**Cons:**
- More complex G-code
- Takes longer
- Requires clear space before cut start

**Calculation:**
```
Ramp angle = depth / horizontal distance

Example: 25mm depth over 100mm horizontal
= 25/100 = 0.25 = 14° ramp angle

Recommended: 10-20° for stone (not too steep!)
```

#### Method 3: Spiral Entry (Arc Conversion Required)

**Use for:** When you need smooth circular entry

```gcode
(Spiral from Z=0 to Z=-25 over 360°)
(Converted to line segments by SheetCAM)
G0 X100 Y100 Z0
F5
G1 X102 Y100 Z-0.5
G1 X103.9 Y101 Z-1.0
G1 X105.5 Y102.5 Z-1.5
... (many segments)
G1 X100 Y100 Z-25  (reached full depth at center)
F15
(Continue with cut)
```

**Pros:**
- Smoothest entry
- Minimal chipping
- Professional appearance

**Cons:**
- Complex G-code (many segments)
- Slower entry
- Requires SheetCAM arc-to-line conversion

### Exit Methods

#### Method 1: Slow Exit (Recommended)

```gcode
G1 X180 Y100 F15    (approaching exit at normal speed)
F5                  (slow down 20mm before exit)
G1 X200 Y100        (exit at slow speed)
G0 Z50              (rapid retract to safe height)
```

**Why slow down?**
- Reduces exit chipping
- Blade leaves cut cleanly
- Better finish quality
- Safer for thin material

#### Method 2: Overrun Exit

```gcode
G1 X200 Y100 F15    (cut past intended edge)
G1 X210 Y100        (overrun by 10mm)
F5                  (slow down)
G0 Z50              (retract)
```

**Use for:** When you can afford to cut slightly past the edge (excess material)

#### Method 3: Lead-Out Radius (Arc Conversion)

Similar to spiral entry, but exiting cut tangentially.

### Entry/Exit Locations

**Strategic placement:**

✅ **Good entry/exit locations:**
- Areas that will be cut away by later operations
- External material (waste side)
- Non-visible edges
- Corners (if using corner radius)

❌ **Avoid entry/exit at:**
- Critical dimensions
- Visible finished edges
- Thin sections (risk of crack propagation)
- Near corners on perimeter

---

## 7. Corner Strategies

### The Corner Problem

Corners present challenges:
1. **Direction change:** Blade must change direction sharply
2. **Chipping risk:** High stress at corner apex
3. **Overcutting:** Inside corners naturally have radius from blade width

### Inside Corners (Pocket/Cutout)

**Reality:** Cannot have sharp inside corner (blade has diameter!)

**Minimum radius = Blade radius**

Example: 300mm diameter blade → 150mm radius
Therefore, inside corner radius ≥ 150mm

**Options:**

1. **Accept the radius (easiest):**
   ```
   Design part with corner radius = blade radius
   Cut naturally follows this radius
   No special programming needed
   ```

2. **Corner cleanup (manual):**
   ```
   Cut with natural blade radius
   Stop machine
   Use angle grinder or hand tools to square corner
   Labor intensive, inconsistent
   ```

3. **Plunge cut in corner (complex):**
   ```gcode
   (Cut first side)
   G1 X100 Y0 F15
   F5
   (Plunge at corner)
   G1 X100 Y0 Z-25
   (Cut second side)
   G1 X100 Y100 F15
   ```
   Creates closer to square corner, but:
   - Leaves witness mark
   - Risk of chipping
   - Adds time

### Outside Corners (Profile)

**Challenge:** Sharp direction change can chip corner apex

**Solution: Corner Slowdown**

```gcode
(Approaching corner)
G1 X100 Y0 F15      (cutting at normal speed)

(Slow down before corner)
F5
G1 X100 Y10         (slow through corner zone)

(Navigate corner)
G1 X90 Y10          (still slow)

(Speed back up)
F15
G1 X0 Y10           (resume normal speed)
```

**Slowdown distance:**
- Soft stone: 5mm before corner
- Medium stone: 10mm before corner
- Hard/brittle stone: 15mm before corner

**In SheetCAM:**
- Tool definition → Advanced → "Corner slowdown"
- Set percentage: 30-50% of cutting feed
- Set distance: 5-15mm (material dependent)

### Corner Types

**90° Corner:**
```
Highest risk of chipping
Slow to F5
Consider chamfer (cut off apex)
```

**Acute Corner (<90°):**
```
Very high chipping risk
Slow to F3
May need multiple passes
Consider rounding the corner slightly
```

**Obtuse Corner (>90°):**
```
Lower risk
Slow to F8
Less critical
```

**Rounded Corner (specified radius):**
```
Convert arc to line segments (2mm segments)
Less chipping risk
Can maintain moderate speed (F10-12)
```

---

## 8. Safety Considerations

### Personal Protective Equipment (PPE)

**Required:**
- [ ] Safety glasses (ANSI Z87.1 rated)
- [ ] Hearing protection (stone cutting is loud!)
- [ ] Steel-toe boots
- [ ] Close-fitting clothing (no loose sleeves)
- [ ] Hair tied back/covered

**Recommended:**
- [ ] Face shield (in addition to glasses)
- [ ] Dust mask (even with water, some dust escapes)
- [ ] Gloves (for handling stone, NOT while machine running)
- [ ] Apron (waterproof)

### Machine Safety

**Before starting:**
- [ ] Emergency stop button tested and functional
- [ ] Blade guard in place and secure
- [ ] All guards and covers installed
- [ ] Work area clear of obstructions
- [ ] No one else in immediate area
- [ ] Adequate lighting

**During operation:**
- [ ] Operator always attends machine (never walk away!)
- [ ] Hands clear of cutting area
- [ ] Watch for clamp collisions
- [ ] Monitor blade performance
- [ ] Listen for unusual sounds
- [ ] Be ready to hit emergency stop

**After operation:**
- [ ] Stop blade completely before approaching
- [ ] Wait for blade to stop spinning (30-60 seconds)
- [ ] Lower water flow
- [ ] Inspect work area for hazards
- [ ] Clean up stone slurry (slip hazard!)

### Material Handling Safety

**Stone slabs are heavy and awkward:**

| Thickness | Typical Weight (granite, 1m²) |
|-----------|-------------------------------|
| 12mm | ~32 kg (70 lbs) |
| 20mm | ~54 kg (120 lbs) |
| 30mm | ~81 kg (180 lbs) |

**Safe lifting:**
- Use mechanical aids (crane, suction cups, forklift)
- Two-person lift minimum for >25kg
- Proper lifting technique (legs, not back)
- Wear steel-toe boots

**Securing material:**
- Clamp securely to table
- Verify clamps won't interfere with toolpath
- Check clamps after any machine vibration
- Use anti-slip pads if needed

### Silica Dust Hazard

**⚠️ CRITICAL:** Stone dust contains crystalline silica, which causes silicosis (incurable lung disease)

**Protection:**
1. **Wet cutting:** Water suppresses most dust (BISSO E350 does this)
2. **Ventilation:** Good air circulation in shop
3. **Dust mask:** Even with water, wear N95 or better
4. **Clean up:** Wet mop, don't dry sweep (resuspends dust)
5. **Monitoring:** If cutting dry, use proper dust extraction and OSHA compliance

**Long-term health:**
- Regular medical check-ups
- Avoid dry cutting when possible
- Keep work area wet
- Change clothes before leaving shop

### Emergency Procedures

**Blade binding/jamming:**
1. Hit emergency stop (!) immediately
2. Do NOT try to free blade while spinning
3. Wait for complete stop
4. Turn off water
5. Assess situation
6. Remove material carefully
7. Inspect blade for damage

**Stone cracking during cut:**
1. Pause or stop immediately (!)
2. Assess crack propagation
3. If crack stops → may continue carefully at slow speed
4. If crack expanding → abort cut, material may shatter

**Blade breakage (rare but dangerous):**
1. Emergency stop immediately
2. Take cover (blade fragments can fly)
3. Once stopped, evacuate area
4. Inspect for blade fragments
5. Replace blade, inspect arbor for damage
6. Investigate root cause before resuming

**Electrical issues:**
1. Emergency stop
2. Disconnect power at breaker
3. Do not touch machine until electrician inspects

---

## 9. Quality Control

### Dimensional Accuracy

**Expected tolerances:**

| Feature | Typical Tolerance |
|---------|-------------------|
| Linear dimensions (X/Y) | ±0.5mm |
| Thickness/depth (Z) | ±0.3mm |
| Squareness (90° corners) | ±0.5mm over 1000mm |
| Parallelism | ±0.5mm |
| Straightness | ±0.3mm over 1000mm |

**Factors affecting accuracy:**
- Machine calibration ($100-$132 settings)
- Blade runout (should be <0.5mm)
- Material flatness (warp, bow)
- Clamping (distortion from clamp pressure)
- Thermal expansion (machine and material)

**Measurement:**
- Use calibrated measuring tools
- Measure at room temperature (thermal stabilization)
- Multiple measurements per dimension
- Document results for process control

### Edge Quality

**Visual inspection:**

**Excellent:**
- Smooth edge, minimal roughness
- No chipping or spalling
- Consistent finish along entire edge
- Sharp arris (edge corner)

**Acceptable:**
- Minor roughness (can be polished)
- Tiny chips <1mm
- Slight inconsistency (blade wear, hardness variation)

**Unacceptable:**
- Large chips (>2mm)
- Cracks radiating from edge
- Glazed/melted appearance (too slow or blade glazed)
- Severe spalling

**Causes of poor edge quality:**

| Problem | Likely Cause | Solution |
|---------|--------------|----------|
| Chipping | Feed too fast, wrong blade | Slow down, change blade type |
| Rough edge | Blade dull/glazed | Dress or replace blade |
| Burn marks | Feed too slow, blade speed wrong | Speed up feed, check RPM |
| Wandering cut | Blade wobble, worn bearings | Check blade mounting, spindle |
| Inconsistent depth | Z-axis calibration off | Recalibrate $102 (Z PPM) |

### Cut Surface Finish

**For exposed edges that will be seen:**

1. **As-cut finish:** Blade cutting only
   - Faster, lower cost
   - Acceptable for many applications
   - Visible blade marks

2. **Polished finish:** Cut + polishing
   - Cut with sharp blade, slow feed
   - Follow with polishing pads (50-3000 grit progression)
   - Professional appearance
   - Labor intensive

3. **Chamfered/eased edge:** Cut + light grinding
   - Removes sharp arris
   - Safer to handle
   - More professional appearance

### In-Process Monitoring

**During cutting, check:**

- [ ] Visual: Does toolpath match drawing?
- [ ] Auditory: Any unusual sounds? (grinding, squealing)
- [ ] Blade: Cutting freely or binding?
- [ ] Water: Adequate flow, good flushing?
- [ ] Material: Any cracks developing?
- [ ] Clamps: Still secure?
- [ ] Position: Machine position matches expected?

**Use UGS status display:**
```
<Idle|MPos:125.34,678.90,23.45|WPos:25.34,78.90,-6.55|...>

Check:
- MPos: Machine position (should increase smoothly during cut)
- WPos: Work position (relative to G54 zero)
- Status: Should be "Run" during cut, "Idle" when stopped
```

### Post-Cut Inspection

**Checklist:**

- [ ] Measure critical dimensions (compare to drawing)
- [ ] Inspect all edges for chipping
- [ ] Check corners for completeness
- [ ] Verify depth of cut (if through-cut, should be clear)
- [ ] Look for cracks (especially at corners, near clamps)
- [ ] Compare to specification/drawing

**Document results:**
```
Part: Kitchen countertop section A
Material: Black galaxy granite, 30mm
Date: 2025-12-08
Dimensions: Length 1000mm (spec: 1000±1) ✓ OK
            Width 600mm (spec: 600±1)   ✓ OK
Edge quality: Excellent, minimal chipping
Notes: Slight chip at exit point (2mm), acceptable per spec
```

---

## 10. Troubleshooting Common Issues

### Issue: Excessive Chipping

**Symptoms:**
- Chips >2mm on edges
- Chunks missing from corners
- Spalling (flaking) on back side of cut

**Possible causes and solutions:**

| Cause | How to identify | Solution |
|-------|-----------------|----------|
| Feed rate too fast | Chipping along entire edge | Reduce F value 30% |
| Wrong blade type | Chipping worse on hard materials | Use finer grit, continuous rim |
| Dull/glazed blade | Progressively worse chipping | Dress or replace blade |
| Entry/exit too fast | Chipping only at start/end | Use F5 for entry/exit |
| Corner too fast | Chipping only at corners | Enable corner slowdown |
| Material brittleness | Chipping even at slow speeds | Consider multiple lighter passes |
| Insufficient water | Dry cutting = more chipping | Increase water flow |

**Test procedure:**
1. Make test cut at current parameters → document chipping
2. Reduce feed 30% (F15 → F10) → test again
3. If improved → use slower feed
4. If no improvement → check blade condition
5. If blade OK → try different blade type

### Issue: Rough/Uneven Edge Finish

**Symptoms:**
- Edge feels rough to touch
- Visible grooves or scoring
- Inconsistent texture along edge

**Possible causes:**

| Cause | Solution |
|-------|----------|
| Blade dull/glazed | Dress blade with dressing stick or replace |
| Blade segments uneven | Replace blade (segments worn unevenly) |
| Vibration | Check blade mounting, balance, spindle bearings |
| Feed too fast | Reduce feed rate |
| Feed inconsistent | Check for binding axes, verify motion smooth |
| Material hardness variation | Natural (granite has hard/soft spots), slow down |

### Issue: Blade Binding/Stopping

**Symptoms:**
- Blade slows or stops during cut
- Motor straining (sound changes)
- Emergency stop triggered
- Blade overheating

**Immediate action:**
1. Emergency stop (!)
2. Retract blade from cut
3. Wait for blade to stop completely
4. Assess situation

**Possible causes:**

| Cause | Solution |
|-------|----------|
| Feed too slow | Increase feed rate (blade needs to cut, not rub) |
| Cut depth too great | Multiple shallower passes or increase water |
| Blade pinched in kerf | Material moving/shifting, check clamps |
| Blade dull | Won't cut efficiently, replace |
| Insufficient water | Blade overheating, increase water flow |
| Wrong blade RPM | Check blade speed, may be too slow |

### Issue: Inaccurate Dimensions

**Symptoms:**
- Cut dimensions don't match G-code
- Consistent error (always off by same amount)
- Random errors (unpredictable)

**Diagnosis:**

**Consistent error (calibration issue):**
```
Example: G-code says move 100mm, but machine moves 98mm
→ Calibration error in $100 (X steps/mm)

Test: Command machine to move 1000mm
Measure actual movement
Calculate error percentage
Adjust $100-$103 settings
```

**Random error (mechanical issue):**
```
Example: Sometimes moves 100mm, sometimes 98mm, sometimes 102mm
→ Mechanical problem (loose belt, worn encoder, electrical noise)

Check:
- Encoder cables secure
- I2C communication stable
- No mechanical binding
- No electrical interference
```

**Kerf compensation:**
```
Issue: Hole diameter too large or too small

Blade kerf = blade thickness (typically 2.5-3mm)

For inside cut (pocket): Actual size = Programmed size + kerf
For outside cut (profile): Actual size = Programmed size - kerf

Solution: Apply kerf compensation in SheetCAM
```

### Issue: Controller Errors

**Common G-code errors:**

| Error | Meaning | Solution |
|-------|---------|----------|
| error:2 | Bad number format | Check G-code for malformed numbers |
| error:8 | Not idle | Wait for previous move to complete |
| error:9 | G-code locked | Reset emergency stop, send Ctrl-X |
| error:20 | Unsupported command | Remove G2/G3/G4 from G-code |
| error:21 | Soft limit | Toolpath exceeds machine limits |
| error:22 | Homing required | Home machine before starting |

**If controller stops responding:**
1. Check USB connection
2. Reset controller (Ctrl-X in UGS)
3. Reconnect UGS
4. Re-home if necessary
5. Reload G-code file

### Issue: Poor Water Flow/Flooding

**Insufficient water:**
- Blade overheating
- Excessive dust
- Poor finish quality

**Solutions:**
- Increase water flow (manual valve)
- Check water pump operation
- Clear any blockages in water line
- Verify nozzle aimed at cut point

**Too much water:**
- Slurry flooding work area
- Difficulty seeing cut
- Water splashing excessively

**Solutions:**
- Reduce water flow
- Adjust nozzle angle
- Ensure drain system working
- Use vacuum or squeegee to manage slurry

---

## Summary: Best Practices Checklist

### Before Each Cut:
- [ ] Material secured and clamps clear of toolpath
- [ ] Blade inspected (segments >3mm, no damage)
- [ ] Water system tested and flowing
- [ ] Work coordinate (G54) set and verified
- [ ] G-code previewed in UGS (visual check)
- [ ] Emergency stop button tested
- [ ] PPE worn (glasses, hearing protection)
- [ ] Blade manually started at correct RPM

### During Cut:
- [ ] Operator attending machine continuously
- [ ] Monitoring blade sound (should be steady)
- [ ] Watching for material cracking
- [ ] Feed override adjusted if needed
- [ ] Water flow adequate (no dry cutting)
- [ ] Position tracking in UGS matches expected

### After Cut:
- [ ] Blade stopped and spinning stopped
- [ ] Water flow reduced
- [ ] Part inspected for quality
- [ ] Dimensions measured and documented
- [ ] Machine cleaned (stone slurry removed)
- [ ] Any issues noted for future reference

### Periodic Maintenance:
- [ ] Daily: Inspect blade, clean machine
- [ ] Weekly: Dress blade if needed, deep clean
- [ ] Monthly: Check calibration accuracy
- [ ] Quarterly: Review and update parameters based on experience

---

**For questions or issues not covered here, refer to:**
- SheetCAM Setup Guide (SHEETCAM_SETUP_GUIDE.md)
- UGS Setup Guide (UGS_SETUP_GUIDE.md)
- Controller documentation (docs/ folder)

**Stay safe and cut smart!**

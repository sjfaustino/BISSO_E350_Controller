# BISSO S5 PLC — FUNCTION BLOCK REFERENCE
## Every FB, PB, OB with Actual NAME: Fields from Raw STL Code

**Last Updated**: 2026-02-20
**Source of Truth**: Raw STL files in `01 - STL/`

> **NOTE**: Every block description here is based on reading the actual raw
> STL instructions. The `NAME:` field comes directly from the Siemens S5
> program listing. Where no comment exists in the raw code, behavior was
> determined by tracing instructions line-by-line.

---

## MASTER CALL HIERARCHY — ASCII DIAGRAM

```
  OB1 (Main Loop — 660 lines, 10 sections)
  │
  │  Section [1]: Program Mode Selection
  │  ├─ FastCmd triple-press → S F 6.3
  │  ├─ IF F 6.3 = 1:
  │  │   ├─ JU PB 9 .................. Speed button mapping
  │  │   └─ JU FB 15 (HELP.) ........ Fast mode axis control
  │  └─ Program mode flags F 0.0-F 0.4
  │
  │  Section [2]: Disk Motor & VFD Sequencing
  │  └─ Star-Delta / VFD startup logic, T 0/T 2/T 20/T 21
  │
  │  Section [3]: Programmed Motion Timer (T 1)
  │
  │  Section [4]: Speed Reference Signal (F 0.7)
  │
  │  Section [5]: Limit Switch Safety & Q 73.2
  │
  │  Section [6]: Main Power (Q 7.4), Table Selection, Motion Pulse
  │  └─ JU FB 18 (INCLBANC) ........ Table hydraulic control
  │
  │  Section [7]: Inverter Sequence Control
  │  ├─ Motion detection: F 5.0 = OR(Q6.0..Q6.5)
  │  ├─ Inverter 1 & 2 sequences (F 1.3, F 1.4)
  │  └─ Emergency stop: F 1.6 → R Q6.0..Q6.5
  │
  │  Section [8]: Table Lock & Program Sequencing
  │  ├─ Q 8.7 table lock solenoid logic
  │  ├─ IF F 5.1 (program started):
  │  │   └─ JU PB 10 ................ Elbo axis routing dispatcher
  │  ├─ IF F 5.2 (manual override in auto):
  │  │   └─ JU PB 11 ................ Manual axis routing dispatcher
  │  └─ F 5.3, F 5.4 sequence control
  │
  │  Section [9]: Inverter Output Generation
  │  ├─ Q 8.0-Q 8.5 (inverter commands)
  │  └─ Phase timing (T 9, T 10)
  │
  │  Section [10]: Speed Control & Potentiometer
  │  ├─ F 2.0/2.1/2.2 speed mode selection
  │  ├─ F 3.5/3.6 speed+/- flag generation
  │  └─ JU FB 14 (RICH.POT) ........ Speed reference dispatcher
  │      ├─ (M001) IF F 2.0: JU FB 17 → FB 12/13, then FB 16
  │      ├─ (M002) IF F 2.1: JU FB 17 → FB 12/13, then FB 16
  │      └─ (M003) IF F 2.2: JU FB 17 → FB 12/13
  │
  │  ┌──────────────── PB10 CALL TREE ─────────────────────────┐
  │  │  PB10 (Elbo Axis Routing)                               │
  │  │  ├─ IF I72.0 OR I72.2 (AND NOT F0.4):                  │
  │  │  │   └─ JC PB 20 ............. Fwd/Rev motor control    │
  │  │  ├─ IF I72.2 (Up/Down axis active):                     │
  │  │  │   └─ JC PB 30 ............. Up/Down speed control    │
  │  │  ├─ IF I72.1 (Translation):                             │
  │  │  │   └─ JC PB 40 ............. Left/Right speed ctrl    │
  │  │  ├─ IF I72.3 OR (I72.0 AND F0.4):                      │
  │  │  │   └─ JC PB 50 ............. Rot Table control        │
  │  │  └─ IF I72.4 (Disk):                                    │
  │  │       └─ JC PB 52 ............ Rot Disk control         │
  │  └─────────────────────────────────────────────────────────┘
  │
  │  ┌──────────────── PB11 CALL TREE ─────────────────────────┐
  │  │  PB11 (Manual Axis Routing)                             │
  │  │  ├─ IF I2.6/I2.7 (Fwd/Rev buttons):                    │
  │  │  │   └─ JC PB 21 ............. Manual Fwd/Rev control   │
  │  │  ├─ IF I3.0/I3.1 (Up/Down buttons):                    │
  │  │  │   └─ JC PB 31 ............. Manual Up/Down control   │
  │  │  ├─ IF I2.4/I2.5 (Left/Right buttons):                 │
  │  │  │   └─ JC PB 41 ............. Manual Left/Right ctrl   │
  │  │  ├─ IF I3.2/I72.3/I3.3 (Rot Table):                    │
  │  │  │   └─ JC PB 51 ............. Manual Rot Table ctrl    │
  │  │  └─ IF I3.4/I3.5 (Rot Disk buttons):                   │
  │  │       └─ JC PB 53 ............ Manual Rot Disk control  │
  │  └─────────────────────────────────────────────────────────┘
  │
  │  ┌──────────────── FB15 INTERNAL ──────────────────────────┐
  │  │  FB15 (HELP.) — Fast Mode Processor (411 lines)         │
  │  │  ├─ Network 1: Entry & FastCmd counter logic            │
  │  │  ├─ Network 2: Fwd/Rev axis → F7.0, DW 0               │
  │  │  │   └─ JU FB 17 (RIC+/-) → FB 12 or FB 13             │
  │  │  ├─ Network 3: Left/Right axis → F7.1, DW 3/4/11       │
  │  │  │   ├─ JU FB 17 (RIC+/-) → FB 12 or FB 13             │
  │  │  │   └─ JU FB 16 (SCALAR) → FB 243 (DIV:16)            │
  │  │  ├─ Network 4: Up/Down axis → F7.2, DW 5/6/7/8         │
  │  │  │   ├─ JU FB 17 (RIC+/-) → FB 12 or FB 13             │
  │  │  │   └─ JU FB 16 (SCALAR) → FB 243 (DIV:16)            │
  │  │  ├─ Network 5: Rot Table axis → F7.4, DW 12/13/14      │
  │  │  │   ├─ JU FB 17 (RIC+/-) → FB 12 or FB 13             │
  │  │  │   └─ JU FB 16 (SCALAR) → FB 243 (DIV:16)            │
  │  │  ├─ Network 6: Rot Disk axis → F7.5, DW 15/16/17       │
  │  │  │   ├─ JU FB 17 (RIC+/-) → FB 12 or FB 13             │
  │  │  │   └─ JU FB 16 (SCALAR) → FB 243 (DIV:16)            │
  │  │  └─ Network 7: QW 66 output assignment                 │
  │  └─────────────────────────────────────────────────────────┘
```

---

## FUNCTION BLOCKS — DETAILED

### FB10 — CALC1 (Increment with Step Doubling)

```
  NAME:    CALC1
  Lines:   72
  Called:  From FB12 and FB13 (potentiometer blocks)
```

**Purpose**: Implements an intelligent step-doubling increment algorithm.
When the operator holds the Speed+/- button, the step size starts small and
doubles after a counter threshold, giving fine control at first and coarse
adjustment later.

**Algorithm (traced from raw STL)**:

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                  FB10 CALC1 — Step Doubling                     │
  │                                                                 │
  │  Input:   FW 34 (current value)                                │
  │  Params:  DW 28 (max limit = 0x4000 = 16384)                  │
  │           DW 31 (initial step = 0x000A = 10)                   │
  │           DW 25 (step increment = 0x0008 = 8)                  │
  │           DW 26 (step divisor = 0x0004 = 4)                    │
  │  Output:  FW 34 (updated value)                                │
  │                                                                 │
  │  IF FW 34 < DW 28 (current < max):                            │
  │    set F 2.5 = 1 (below max)                                   │
  │    Load step from FW 32 (current step size)                    │
  │    FW 34 = FW 34 + step                                        │
  │    IF FW 34 > DW 28: FW 34 = DW 28   (clamp to max)           │
  │  ELSE:                                                          │
  │    F 2.5 = 0 (at max already)                                  │
  │                                                                 │
  │  Step doubling via C 3:                                         │
  │    Edge of F 1.2 (toggle output) → CD C 3                     │
  │    When C 3 = 0: step = step × 2 (reload C 3 = 2)             │
  └─────────────────────────────────────────────────────────────────┘
```

**Key**: The `+F`, `-F`, `<=F` instructions in this block are **16-bit
fixed-point integer** operations, NOT floating point. The S5 CPU 100
does not have a floating-point unit.

---

### FB11 — CALC2 (Spianatura Calculation)

```
  NAME:    CALC2
  Lines:   83
  Called:  From OB1 (exact calling context needs more analysis)
```

**Purpose**: Performs calculations specific to the Spianatura
(automatic surface leveling/flattening) mode. Uses parameters from
DW 32-37 with comparisons against maximum limits.

**Key Operations**:
- Loads DW 32 (max = 0x7FD0 = 32720) as upper comparison limit
- Uses DW 33 (999), DW 34 (12800 — "Spianatura reference")
- Uses the `<=F` (less-or-equal fixed-point integer) comparison
- Uses `>=F` (greater-or-equal fixed-point integer) comparison
- Produces result in FW 34 for downstream use

---

### FB12 — POT.+ (Potentiometer Increment)

```
  NAME:    POT.+
  Lines:   59
  Called:  From FB17 (RIC+/-) when F 3.5 = 1 (Speed+ pressed)
```

**Purpose**: Implements the "increase potentiometer value" operation.
Loads the current speed value from FW 34, increments by the step
value from FW 32, clamps to max (DW 28 = 16384), and stores back.

**Algorithm**:

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                  FB12 POT.+ — Increment                         │
  │                                                                 │
  │  1. Load FW 34 (current value)                                 │
  │  2. IF FW 34 < DW 28 (16384):                                 │
  │       F 4.6 = 0 (not at max)                                   │
  │       FW 34 = FW 34 + FW 32 (add step)                        │
  │     ELSE:                                                       │
  │       F 4.6 = 1 (at max)                                       │
  │  3. Step doubling: same logic as FB10 via C 3                  │
  │  4. IF F 5.7 = 1: double the step  (long-press acceleration)  │
  └─────────────────────────────────────────────────────────────────┘
```

---

### FB13 — POT.- (Potentiometer Decrement)

```
  NAME:    POT.-
  Lines:   39
  Called:  From FB17 (RIC+/-) when F 3.6 = 1 (Speed- pressed)
```

**Purpose**: Implements the "decrease potentiometer value" operation.
Mirror of FB12 but decrements. Loads FW 34, subtracts step from FW 32,
clamps to minimum (DW 27 = 1 or 0), stores back.

---

### FB14 — RICH.POT (Speed Reference Dispatcher)

```
  NAME:    RICH.POT
  Lines:   49
  Called:  From OB1 Section [10] — at end of main loop
```

**Purpose**: Master dispatcher that routes speed adjustment operations
to the correct data words depending on which speed mode is active.
This is the central hub for all speed value management.

**Logic Flow**:

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                  FB14 RICH.POT — Speed Dispatcher               │
  │                                                                 │
  │  ┌─ IF F 2.0 = 0 (NOT in passage speed mode):                 │
  │  │    Load DW 2 → FW 34                                        │
  │  │    Call FB 17 (RIC+/-)     ; adjust FW 34 up/down           │
  │  │    Store FW 34 → DW 2                                       │
  │  │    Store FW 34 → QW 66    ; output to VFD                   │
  │  │    IF F 2.0 = 1: BEC      ; exit if changed to mode 0      │
  │  │                                                              │
  │  ├─ M001: IF F 2.1 = 0 (NOT in tilt speed mode):              │
  │  │    Load DW 41 → FW 38    ; scale factor for Spianatura      │
  │  │    Load DW 18 → FW 34    ; Spianatura speed value           │
  │  │    Call FB 17 (RIC+/-)   ; adjust                            │
  │  │    Store FW 34 → DW 18                                       │
  │  │    Call FB 16 (SCALAR)   ; scale → QW 66 output             │
  │  │    IF F 2.1 = 1: BEC                                        │
  │  │                                                              │
  │  └─ M002: IF F 2.2 = 0 (NOT in general speed mode):           │
  │       Load DW 1 → FW 34                                        │
  │       Call FB 17 (RIC+/-)                                       │
  │       Store FW 34 → DW 1                                        │
  │       Store FW 34 → QW 66   ; output to VFD                    │
  │  M003: BE (end)                                                 │
  └─────────────────────────────────────────────────────────────────┘

  Speed Mode Routing:
  ┌──────────┬──────────┬───────────────────────────────────┐
  │ Flag     │ Mode     │ Data Word Modified                │
  ├──────────┼──────────┼───────────────────────────────────┤
  │ F 2.0=0  │ Passages │ DW 2 → FW 34 → DW 2 → QW 66    │
  │ F 2.1=0  │ Tilt     │ DW 18 → FW 34 → DW 18 → SCALAR │
  │ F 2.2=0  │ General  │ DW 1 → FW 34 → DW 1 → QW 66    │
  └──────────┴──────────┴───────────────────────────────────┘
```

---

### FB15 — HELP. (Fast Mode Axis Processor)

```
  NAME:    HELP.
  Lines:   411 (LARGEST BLOCK)
  Called:  From OB1 Section [1] when F 6.3 = 1
```

**Purpose**: The "help" or "fast command" processor. When the operator
triple-presses I 3.7, this block takes over and allows editing axis
speed parameters via the manual buttons (I 2.4-3.5).

Each axis has its own network with up to 3 speed levels, selected by
the F 8.0-8.2 flags. The operator selects an axis by pressing the
corresponding jog button, then uses Speed+/- to adjust values.

**Architecture**:

```
  ┌──────────────────────────────────────────────────────────────────┐
  │                  FB15 HELP. — Architecture                       │
  │                                                                  │
  │  Network 1: ENTRY LOGIC                                         │
  │  ├─ Timeout via T 17 (2 second window)                          │
  │  ├─ Triple-press counter C 1 → S F 6.3                         │
  │  ├─ Axis mutex: reset all F 7.0-7.5 when new axis chosen       │
  │  └─ Exit when I 3.7 pressed again (AN F 7.6 toggle)            │
  │                                                                  │
  │  Network 2: FWD/REV AXIS (F 7.0)                               │
  │  ├─ Trigger: I 2.6 OR I 2.7 (front/back buttons)               │
  │  ├─ Speed levels: DW 0 only (1 speed level)                    │
  │  ├─ Call: FB 17 → FB 12/13 (adjust)                             │
  │  └─ No scaling needed (direct to QW 66)                         │
  │                                                                  │
  │  Network 3: LEFT/RIGHT AXIS (F 7.1)                             │
  │  ├─ Trigger: I 2.4 OR I 2.5 (left/right buttons)               │
  │  ├─ Speed levels:                                                │
  │  │   F 8.0 → DW 3 (level 1 = 6654)                             │
  │  │   F 8.1 → DW 4 (level 2 = 3216)                             │
  │  │   F 8.2 → DW 11 (level 3 = 160)                             │
  │  ├─ Scale: DW 39 (= 3) via FB 16 SCALAR                        │
  │  └─ Output: QW 66 (via FB 16)                                   │
  │                                                                  │
  │  Network 4: UP/DOWN AXIS (F 7.2)                                │
  │  ├─ Trigger: I 3.0 OR I 3.1 (down/up buttons)                  │
  │  ├─ Guard: AN I 4.3 (NOT in tilt mode)                          │
  │  ├─ Speed levels:                                                │
  │  │   F 34.0 → DW 6 (level 1 = 14383)                           │
  │  │   F 34.1 → DW 7 (level 2 = 5632)                            │
  │  │   F 34.2 → DW 8 (level 3 = 1214)                            │
  │  │   Default: DW 5 (= 855)                                      │
  │  ├─ Scale: DW 40 (= 28) via FB 16 SCALAR                       │
  │  └─ Output: QW 66 (via FB 16)                                   │
  │                                                                  │
  │  Network 5: ROT TABLE AXIS (F 7.4)                              │
  │  ├─ Trigger: I 3.2 OR I 3.3 (table rotation buttons)           │
  │  ├─ Speed levels:                                                │
  │  │   F 8.0 → DW 12 (level 1 = 4029)                            │
  │  │   F 8.1 → DW 13 (level 2 = 634)                             │
  │  │   F 8.2 → DW 14 (level 3 = 83)                              │
  │  ├─ Scale: DW 42 (= 1) via FB 16 SCALAR                        │
  │  └─ Output: QW 66 (via FB 16)                                   │
  │                                                                  │
  │  Network 6: ROT DISK AXIS (F 7.5)                               │
  │  ├─ Trigger: I 3.4 OR I 3.5 (disk rotation buttons)            │
  │  ├─ Speed levels:                                                │
  │  │   F 8.0 → DW 15 (level 1 = 16384)                           │
  │  │   F 8.1 → DW 16 (level 2 = 5632)                            │
  │  │   F 8.2 → DW 17 (level 3 = 0)                               │
  │  ├─ Scale: DW 42 (= 1) via FB 16 SCALAR                        │
  │  └─ Output: QW 66 (via FB 16)                                   │
  │                                                                  │
  │  Network 7: FINAL OUTPUT                                        │
  │  └─ QW 66 assignment from SCALAR result                         │
  └──────────────────────────────────────────────────────────────────┘

  Speed Level Selection within each network:
  ┌─────────────────────────────────────────────────┐
  │  F 8.0 = 0 → Speed Level 1 (DW x)             │
  │  F 8.0 = 1, F 8.1 = 0 → Speed Level 2 (DW y) │
  │  F 8.1 = 1, F 8.2 = 0 → Speed Level 3 (DW z) │
  │  F 8.2 = 1 → Jump to FB17 for adjustment      │
  │                                                 │
  │  Level cycling: each edit toggles to next level │
  └─────────────────────────────────────────────────┘
```

---

### FB16 — SCALAR (Speed Value Scaling)

```
  NAME:    SCALAR
  Lines:   16
  Called:  From FB14 (M001) and FB15 (Networks 3-6)
```

**Purpose**: Divides one 16-bit value by another using the standard
Siemens FB243 (DIV:16) library block. Used to scale speed values
by axis-specific scale factors before output to QW 66.

**Logic**:

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                  FB16 SCALAR — Division                         │
  │                                                                 │
  │  Calls FB243 (DIV:16) with these parameters:                   │
  │                                                                 │
  │  Z1  = FW 34   (numerator — current speed value)               │
  │  Z2  = FW 38   (denominator — scale factor from DW 39-42)     │
  │  OV  = F 8.7   (overflow flag — not monitored)                 │
  │  FEH = F 8.7   (error flag — not monitored)                    │
  │  Z3=0= F 8.7   (quotient-zero flag — not monitored)            │
  │  Z4=0= F 8.7   (remainder-zero flag — not monitored)           │
  │  Z3  = QW 66   (quotient → VFD speed output!)                 │
  │  Z4  = FW 36   (remainder — stored but not used)               │
  │                                                                 │
  │  Result: QW 66 = FW 34 ÷ FW 38                                │
  │                                                                 │
  │  Example: If FW 34 = 6654 and FW 38 = 3 (DW 39):             │
  │           QW 66 = 6654 ÷ 3 = 2218                             │
  └─────────────────────────────────────────────────────────────────┘
```

---

### FB17 — RIC(+/-) (Speed Increment/Decrement Dispatcher)

```
  NAME:    RIC(+/-)
  Lines:   12
  Called:  From FB14 and FB15 (multiple times)
```

**Purpose**: Simple dispatcher that checks which speed button flag is
active and calls the corresponding potentiometer block.

**Logic**:

```
  IF F 3.5 = 1 (Speed+ pressed):
      JC FB 12 (POT.+)   ; increment FW 34

  IF F 3.6 = 1 (Speed- pressed):
      JC FB 13 (POT.-)   ; decrement FW 34

  BE (end — no Speed button pressed)
```

**Data Flow Through Speed Adjustment Chain**:

```
  I 4.5 (Speed+ btn)
    │
    ▼
  F 3.5 = 1
    │
    ▼
  FB17 calls FB12 (POT.+)
    │
    ├─ FB12 loads FW 34
    ├─ FB12 adds step from FW 32
    ├─ FB12 clamps to DW 28 max
    └─ FB12 stores back to FW 34
         │
    ┌────┘
    │
    ▼
  Caller (FB14 or FB15) reads FW 34
    │
    ├─► FB14: stores to DW 1/2/18, outputs to QW 66 or calls FB16
    └─► FB15: stores to DW x, calls FB16 → QW 66
```

---

### FB18 — INCLBANC (Table Hydraulic Control)

```
  NAME:    INCLBANC
  Lines:   67
  Called:  From OB1 Section [6] — JU FB 18
  Comment: [1] Move tables Up/Down
```

**Purpose**: Controls the hydraulic table raise/lower system for both
Table 1 and Table 2. Reads button inputs (I 9.0-9.4), determines
which table and direction, starts the oil pump (Q 7.3) with a safety
timer (T 26), and outputs direction signals to the Elbo (Q 72.7-73.7).

**Logic**:

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                  FB18 INCLBANC — Table Control                  │
  │                                                                 │
  │  1. POSITION CHANGE DETECTION:                                 │
  │     Load IW 8 (encoder/position input)                         │
  │     AND with DW 44 (mask = 0x001F = bits 0-4)                 │
  │     Compare with last position (FW 50)                         │
  │     ><F (not-equal): F 10.0 = 1 if changed                    │
  │                                                                 │
  │  2. BUTTON LATCHING:                                            │
  │     F 11.0 = I 9.0 (Table btn)                                │
  │     F 11.1 = I 9.1 (Table 1 Down)                             │
  │     F 11.2 = I 9.2 (Table 2 Up)                               │
  │     F 11.3 = I 9.3 (Table 2 Down)                             │
  │     F 11.4 = I 9.4                                             │
  │                                                                 │
  │  3. DIRECTION LOGIC (mutex with I 9.0 as master gate):         │
  │     F 10.1 = I 9.1 AND I 9.0 AND NOT I 9.2  → Table 1 Up     │
  │     F 10.2 = I 9.2 AND I 9.0 AND NOT I 9.1  → Table 1 Down   │
  │     F 10.3 = I 9.3 AND NOT I 9.0 AND NOT I 9.4 → Table 2 Up  │
  │     F 10.4 = I 9.4 AND NOT I 9.0 AND NOT I 9.3 → Table 2 Down│
  │                                                                 │
  │  4. PUMP CONTROL:                                               │
  │     IF (F10.1 OR F10.2 OR F10.3 OR F10.4) AND F10.0:          │
  │       Start T 26 (KT 120.2 = 120 seconds)                     │
  │       Q 7.3 = T 26 (pump runs while timer active)             │
  │                                                                 │
  │  5. DIRECTION OUTPUTS (to Elbo/external via Q 72.7-73.7):     │
  │     Q 72.7 = F 10.1 AND Q 7.3  (Table 1 Up + pump on)        │
  │     Q 73.5 = F 10.2 AND Q 7.3  (Table 1 Down + pump on)      │
  │     Q 73.6 = F 10.3 AND Q 7.3  (Table 2 Up + pump on)        │
  │     Q 73.7 = F 10.4 AND Q 7.3  (Table 2 Down + pump on)      │
  └─────────────────────────────────────────────────────────────────┘

  Safety: T 26 = 120 seconds max pump run time (prevents overheating)
```

---

### FB240 — COD:B4 (BCD-to-Binary Conversion)

```
  NAME:    COD:B4
  Lines:   9
  Called:  Standard library block (from OB1 Section [10])
  Params:  BCD (input word), SBCD (sign bit), DUAL (output word)
```

**Purpose**: Converts a BCD (Binary-Coded Decimal) value to binary.
This is a Siemens standard library function block.

---

### FB241 — (Standard Library Block)

```
  Lines:   Similar to FB240
  Called:  Standard library block
```

---

### FB242 — (Standard Library Block)

```
  Lines:   Similar to FB240/241
  Called:  Standard library block
```

---

### FB243 — DIV:16 (16-bit Integer Division)

```
  NAME:    DIV:16
  Lines:   14
  Called:  From FB16 (SCALAR)
  Params:  Z1 (numerator IW), Z2 (denominator IW),
           OV (overflow QBI), FEH (error QBI),
           Z3=0 (quotient-zero QBI), Z4=0 (remainder-zero QBI),
           Z3 (quotient QW), Z4 (remainder QW)
```

**Purpose**: Siemens standard library function block for 16-bit integer
division. Used by SCALAR (FB16) to divide speed value by scale factor.

---

### FB250, FB251 — (Standard Library Blocks)

```
  Lines:   ~15 each
  Called:  Standard library blocks (exact usage to be determined)
```

---

## PROGRAM BLOCKS — DETAILED

### PB9 — Speed Button Flags

```
  Lines:   12
  Called:  From OB1 Section [1] when F 6.3 = 1
  Comment: [1] Set Flags based on Speed Buttons
```

**Purpose**: Simple mapping of speed button inputs to flags.

```
  A   I 4.5   →   = F 2.3    (Speed+ latch)
  A   I 4.6   →   = F 2.4    (Speed- latch)
  BE
```

---

### PB10 — Elbo Axis Routing Dispatcher

```
  Lines:   60
  Called:  From OB1 Section [8] when F 5.1 = 1 (Elbo program started)
```

**Purpose**: When the Elbo is in automatic/program mode, this block
determines which axis should be controlled based on Elbo command
signals (I 72.x), and dispatches to the appropriate sub-program.

**Routing Logic**:

```
  ┌──────────────────────────────────────────────────────────────┐
  │ Priority │ Condition                    │ Action             │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 1st      │ (I72.0 OR I72.2) AND NOT    │ F 6.0 → JC PB 20  │
  │          │ F5.0 AND NOT F0.4, OR Q6.0  │ (Fwd/Rev axis)     │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 2nd      │ I72.2 AND NOT F5.0,         │ F 6.0 → JC PB 30  │
  │          │ OR Q6.2 OR Q6.3             │ (Up/Down axis)     │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 3rd      │ I72.1 AND NOT F5.0,         │ F 6.1 → JC PB 40  │
  │          │ OR Q6.1                     │ (Left/Right axis)  │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 4th      │ (I72.3 AND NOT F0.4) OR     │ F 6.2 → JC PB 50  │
  │          │ (I72.0 AND F0.4) OR Q6.4   │ (Rot Table)        │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 5th      │ I72.4 AND NOT F5.0,         │ → JC PB 52         │
  │          │ OR Q6.5                     │ (Rot Disk)         │
  └──────────┴──────────────────────────────┴────────────────────┘

  Note: Each routing uses BEC (Block End Conditional) after its JC call,
  so only the FIRST matching axis is processed. This ensures mutex behavior.
```

---

### PB11 — Manual Axis Routing Dispatcher

```
  Lines:   68
  Called:  From OB1 Section [8] when F 5.2 = 1 (manual override)
```

**Purpose**: When the operator uses manual jog buttons during automatic
mode, this block routes the button presses to the correct axis sub-program.

**Routing Logic**:

```
  ┌──────────────────────────────────────────────────────────────┐
  │ Priority │ Condition                    │ Action             │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 1st      │ Q6.0 OR (I2.6/I2.7 AND     │ F 6.4 → JC PB 21  │
  │          │ NOT F0.4 AND NOT F5.0)      │ (Fwd/Rev)          │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 2nd      │ Q6.2/Q6.3 OR (I3.0/I3.1    │ F 6.5 → JC PB 31  │
  │          │ AND NOT F5.0)               │ (Up/Down)          │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 3rd      │ Q6.1 OR (I2.4/I2.5         │ F 6.6 → JC PB 41  │
  │          │ AND NOT F5.0)               │ (Left/Right)       │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 4th      │ Q6.4 OR (I3.2/I72.3/I3.3   │ F 6.7 → JC PB 51  │
  │          │ AND NOT F5.0)               │ (Rot Table)        │
  ├──────────┼──────────────────────────────┼────────────────────┤
  │ 5th      │ Q6.5 OR (I3.4/I3.5         │ → JC PB 53         │
  │          │ AND NOT F5.0)               │ (Rot Disk)         │
  └──────────┴──────────────────────────────┴────────────────────┘
```

---

### PB20 — Forward/Reverse Motor Control (Elbo Axis)

```
  Lines:   113
  Called:  From PB10 when Fwd/Rev axis selected (F 6.0)
```

**Purpose**: Controls the forward/reverse (avanço/recuo) motor contactor
Q 6.0 based on Elbo speed signals. Implements complex OR-before logic
for direction selection (F 4.0 forward, F 4.1 reverse).

**Key Signals Used**:
- I 72.5 (Elbo Fast), I 72.6 (Elbo Slow), I 72.0 (Elbo Medium)
- I 73.0, I 3.7, I 72.2, I 72.7 — various gate/interlock signals
- T 12, T 16 — variable ramp timers (from FW 24, FW 22)
- QW 64 — VFD speed output (NOTE: QW 64, not QW 66!)

**Data Flow in PB20**:
```
  F 4.0 (Forward) ──┐
  F 4.1 (Reverse) ──┤── gated by many conditions ──► Q 6.0 (Fwd/Rev contactor)
                     │
  DW 0 ─────────────► QW 64 (VFD speed reference: default/idle)
  DW 1 ─────────────► QW 64 (VFD speed reference: forward active)
  DW 2 ─────────────► QW 64 (VFD speed reference: reverse active)
```

---

### PB21 — Manual Forward/Reverse Control

```
  Lines:   ~50
  Called:  From PB11 when manual Fwd/Rev (F 6.4)
```

**Purpose**: Manual version of PB20. Controls Q 6.0 contactor based on
I 2.6/I 2.7 button presses rather than Elbo signals.

---

### PB30, PB31 — Up/Down Axis Control

```
  PB30: Elbo-controlled (called from PB10)
  PB31: Manual-controlled (called from PB11)
  Lines:  ~30 each
```

**Purpose**: Control Q 6.2 (partial up/down) and Q 6.3 (full up/down)
based on Elbo or manual inputs.

---

### PB32, PB33 — Up/Down Support Blocks

```
  Lines:  ~25 each
  Called:  Support routines for PB30/PB31
```

---

### PB40, PB41 — Left/Right Axis Control

```
  PB40: Elbo-controlled (called from PB10)
  PB41: Manual-controlled (called from PB11)
  Lines:  ~25 each
```

**Purpose**: Control Q 6.1 contactor for left/right translation.

---

### PB50, PB51 — Table Rotation Control

```
  PB50: Elbo-controlled (called from PB10)
  PB51: Manual-controlled (called from PB11)
  Lines:  ~45 each
```

**Purpose**: Control Q 6.4 contactor for table rotation. Includes
table lock interlock logic with Q 8.7 and I 72.3.

---

### PB52, PB53 — Disk Rotation Control

```
  PB52: Elbo-controlled (called from PB10) — NOT in block dump
  PB53: Manual-controlled (called from PB11) — NOT in block dump
```

**Purpose**: Control Q 6.5 contactor for disk rotation.
⚠ These blocks are called in PB10/PB11 but were not included in the
original S5 program dump. They may be empty or contain minimal code.

---

## BLOCK INVENTORY — COMPLETE

### Blocks Present in Dump (31 files):

```
  ┌──────────┬───────────┬──────────────────────────────────────┐
  │ Block    │ Lines     │ NAME / Purpose                       │
  ├──────────┼───────────┼──────────────────────────────────────┤
  │ OB1      │ 660       │ Main loop (10 sections)             │
  │ DB10     │ 49        │ Data block (45 words)               │
  │ FB10     │ 72        │ CALC1 — Step doubling increment     │
  │ FB11     │ 83        │ CALC2 — Spianatura calculation      │
  │ FB12     │ 59        │ POT.+ — Potentiometer increment     │
  │ FB13     │ 39        │ POT.- — Potentiometer decrement     │
  │ FB14     │ 49        │ RICH.POT — Speed dispatcher         │
  │ FB15     │ 411       │ HELP. — Fast mode axis processor    │
  │ FB16     │ 16        │ SCALAR — Division (calls FB243)     │
  │ FB17     │ 12        │ RIC(+/-) — Speed +/- dispatcher     │
  │ FB18     │ 67        │ INCLBANC — Table hydraulics          │
  │ FB240    │ 9         │ COD:B4 — BCD conversion (stdlib)    │
  │ FB241    │ ~10       │ Standard library block               │
  │ FB242    │ ~10       │ Standard library block               │
  │ FB243    │ 14        │ DIV:16 — 16-bit division (stdlib)   │
  │ FB250    │ ~12       │ Standard library block               │
  │ FB251    │ ~12       │ Standard library block               │
  │ PB9      │ 12        │ Speed button flag mapping            │
  │ PB10     │ 60        │ Elbo axis routing dispatcher        │
  │ PB11     │ 68        │ Manual axis routing dispatcher      │
  │ PB20     │ 113       │ Fwd/Rev motor control (Elbo)        │
  │ PB21     │ ~50       │ Manual Fwd/Rev control              │
  │ PB30     │ ~30       │ Elbo Up/Down control                │
  │ PB31     │ ~25       │ Manual Up/Down control              │
  │ PB32     │ ~25       │ Up/Down support                     │
  │ PB33     │ ~20       │ Up/Down support                     │
  │ PB40     │ ~25       │ Elbo Left/Right control             │
  │ PB41     │ ~20       │ Manual Left/Right control           │
  │ PB50     │ ~45       │ Elbo Rot Table control              │
  │ PB51     │ ~50       │ Manual Rot Table control            │
  └──────────┴───────────┴──────────────────────────────────────┘

  Blocks CALLED but NOT present in dump:
    PB52 (Elbo Rot Disk) — called from PB10 line 57
    PB53 (Manual Rot Disk) — called from PB11 line 66
```

---

*End of Function Block Reference*

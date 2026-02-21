# BISSO S5 PLC — OB1 MAIN LOOP WALKTHROUGH
## Line-by-Line Section Analysis of the 660-Line Main Program

**Last Updated**: 2026-02-20
**Source of Truth**: `01 - STL/OB1.AWL` (raw, 660 lines)

> **NOTE**: OB1 (Organization Block 1) is the main cyclic program.
> The S5 PLC executes OB1 repeatedly in an infinite loop. Each
> complete scan takes approximately 10-50ms depending on logic
> complexity.

---

## OB1 SECTION MAP — ASCII OVERVIEW

```
  OB1.AWL (660 lines)
  │
  │  Line 1-165:    ┌─────────────────────────────────────────┐
  │  Section [1]    │ PROGRAM MODE SELECTION & FAST MODE      │
  │                 │ Sets F 0.0-0.4 based on button inputs   │
  │                 │ FastCmd triple-press → F 6.3 → FB15     │
  │                 │ Outputs: Q 73.0, Q 73.2, Q 72.0-72.2   │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 166-231:  ┌─────────────────────────────────────────┐
  │  Section [2]    │ DISK MOTOR CONTROL & VFD SEQUENCING     │
  │                 │ Star-Delta / VFD startup logic           │
  │                 │ Laser toggle, Q 7.0, Q 6.6/6.7          │
  │                 │ Timers: T 0, T 2, T 20, T 21            │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 233-243:  ┌─────────────────────────────────────────┐
  │  Section [3]    │ PROGRAMMED MOTION TIMEOUT               │
  │                 │ T 1 = 60 second sequence timeout         │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 244-262:  ┌─────────────────────────────────────────┐
  │  Section [4]    │ GENERAL SPEED ENABLE (F 0.7)            │
  │                 │ F 0.7 = VFD ready AND speed valid        │
  │                 │ Toggle flip-flop: F 0.5 → F 0.6 → F1.2 │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 264-318:  ┌─────────────────────────────────────────┐
  │  Section [5]    │ LIMIT SWITCH SAFETY & STATUS OUTPUTS    │
  │                 │ Q 73.2 = composite limit switch signal   │
  │                 │ Pass-throughs: Q 72.5, Q 72.6, Q 72.4   │
  │                 │ Spianatura logic: F 9.4, T 19            │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 319-360:  ┌─────────────────────────────────────────┐
  │  Section [6]    │ MAIN POWER, TABLE, MOTION PULSE         │
  │                 │ Q 7.4 power relay, T 4 timeout           │
  │                 │ JU FB 18 (INCLBANC) table hydraulics     │
  │                 │ I 4.3 tilt/vertical selector             │
  │                 │ Motion pulse T 24 → Q 7.5                │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 361-468:  ┌─────────────────────────────────────────┐
  │  Section [7]    │ INVERTER SEQUENCE CONTROL               │
  │                 │ Motion detection: F 5.0 = OR(Q6.0..Q6.5)│
  │                 │ Inverter 1: F 1.4, T 5                   │
  │                 │ Inverter 2: F 1.3, T 6                   │
  │                 │ Motion stop: F 1.6 → R Q6.0..Q6.5       │
  │                 │ Speed output: QW 64                      │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 470-532:  ┌─────────────────────────────────────────┐
  │  Section [8]    │ TABLE LOCK & PROGRAM SEQUENCING         │
  │                 │ Q 8.7 table lock solenoid control        │
  │                 │ JU PB 10 (Elbo axis routing)             │
  │                 │ JU PB 11 (Manual axis routing)           │
  │                 │ F 5.1-5.4 sequence state machine         │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 533-600:  ┌─────────────────────────────────────────┐
  │  Section [9]    │ INVERTER OUTPUT GENERATION              │
  │                 │ Q 8.0-Q 8.5 inverter forward/reverse     │
  │                 │ Q 8.2/Q 8.3 speed control                │
  │                 │ Phase timing: T 9, T 10, T 22            │
  │                 │ Q 8.6 spindle motor, Q 73.1 status       │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 601-659:  ┌─────────────────────────────────────────┐
  │  Section [10]   │ SPEED CONTROL & POTENTIOMETER           │
  │                 │ F 2.0/2.1/2.2 speed mode selection       │
  │                 │ F 3.5/F 3.6 speed+/- flag generation     │
  │                 │ JU FB 14 (RICH.POT) speed dispatcher     │
  │                 └─────────────────────────────────────────┘
  │
  │  Line 660:      BE (Block End — restart OB1 from top)
```

---

## SECTION [1] — PROGRAM MODE SELECTION (Lines 1-165)

This is the largest section — it handles program mode selection,
the FastCmd triple-press mechanism, and all status outputs to the Elbo.

### Program Mode Selection Logic

```
  ┌────────────────────────────────────────────────────────────────────┐
  │  PROGRAM MODES are mutually exclusive. Pressing one button        │
  │  resets all others. I 4.4 (Power OFF) resets everything.         │
  │                                                                    │
  │  SET CONDITIONS:                                                   │
  │  ┌───────────── I 2.0 ─────────────────────────────────────────┐  │
  │  │ A  I 2.0      (Manual button pressed)                       │  │
  │  │ AN F 6.3      (NOT in fast mode)                            │  │
  │  │ AN I 2.4-3.5  (ALL jog buttons released — 12 checks!)      │  │
  │  │ AN F 5.0      (no motion active)                            │  │
  │  │ S  F 0.0      → Manual mode ON                             │  │
  │  └─────────────────────────────────────────────────────────────┘  │
  │                                                                    │
  │  ┌───────────── I 2.1 ─────────────────────────────────────────┐  │
  │  │ AN F 0.1      (NOT already in Translation)                  │  │
  │  │ A  I 2.1      (Translation button pressed)                  │  │
  │  │ AN I 72.7     (Elbo Emergency OFF)                          │  │
  │  │ AN F 0.4      (NOT in Spianatura)                           │  │
  │  │ S  F 0.1      → Translation mode ON                        │  │
  │  └─────────────────────────────────────────────────────────────┘  │
  │                                                                    │
  │  Similar patterns for I 2.2 → F 0.2, I 2.3 → F 0.3              │
  │                                                                    │
  │  SPIANATURA MODE:                                                  │
  │  ┌───────────── I 73.2 ────────────────────────────────────────┐  │
  │  │ AN F 0.1      (NOT in Translation)                          │  │
  │  │ A  I 73.2     (Spianatura command from Elbo/CNC)            │  │
  │  │ AN I 72.7     (Elbo Emergency OFF)                          │  │
  │  │ S  F 0.4      → Spianatura mode ON                         │  │
  │  └─────────────────────────────────────────────────────────────┘  │
  │                                                                    │
  │  RESET CONDITIONS (for each mode F 0.0-0.4):                      │
  │  ON I 2.x (own button released)   OR                              │
  │  ON I 4.4 (power OFF)             → R F 0.x                      │
  └────────────────────────────────────────────────────────────────────┘
```

### FastCmd Triple-Press Mechanism

```
  ┌────────────────────────────────────────────────────────────────────┐
  │  STEP 1: First I 3.7 press                                       │
  │  ├─ A I 3.7 (FastCmd button pressed)                              │
  │  ├─ Load KC 003 into C 1 (counter = 3)                           │
  │  ├─ Start T 17 (KT 020.1 = 2 second window)                     │
  │  └─ T 17 starts counting                                          │
  │                                                                    │
  │  STEP 2: Subsequent presses within 2s window                      │
  │  ├─ A I 3.7 → CD C 1 (decrement counter)                        │
  │  ├─ 2nd press: C 1 = 2                                           │
  │  └─ 3rd press: C 1 = 1                                           │
  │                                                                    │
  │  STEP 3: Activation check                                         │
  │  ├─ IF C 1 = 0 AND T 17 still running:                           │
  │  │   S F 6.3 = 1 (Fast mode activated!)                          │
  │  │   JU PB 9  (map speed buttons to flags)                       │
  │  │   JU FB 15 (HELP. — fast mode processor)                      │
  │  └─ IF T 17 expires before C 1 = 0: nothing happens              │
  │                                                                    │
  │  STEP 4: Deactivation                                              │
  │  ├─ Press I 3.7 again while F 6.3 = 1                            │
  │  └─ R F 6.3 (fast mode OFF, return to normal)                    │
  │                                                                    │
  │  Timeline:                                                         │
  │  ├─ t=0ms     I 3.7 press #1 → T17 start, C1=3                  │
  │  ├─ t=300ms   I 3.7 press #2 → C1=2                              │
  │  ├─ t=600ms   I 3.7 press #3 → C1=1 → (one more needed?)        │
  │  └─ t=2000ms  T17 expires → reset if not activated               │
  └────────────────────────────────────────────────────────────────────┘
```

### Status Outputs to Elbo

```
  Q 73.0 = F 0.0 OR F 0.1 OR F 0.2 OR F 0.3 OR (F 0.4 AND F 1.2)
           Meaning: "PLC is in a program mode" → tells Elbo

  Q 73.2 = F 6.3 AND F 1.2  (fast mode active + toggle)
           Meaning: "Fast mode indicator" → tells Elbo

  Q 72.0 = F 0.3  (Passage program status)
  Q 72.1 = F 0.1  (Translation program status)
  Q 72.2 = F 0.1  (duplicate Translation status)

  = F 2.7: A Q 6.1 → S F 2.7  (translation interlock SET when Q6.1 active)
           A Q 6.0, (O Q8.0, O Q8.4) → R F 2.7  (RESET when Fwd/Rev with inverter)
```

---

## SECTION [2] — DISK MOTOR CONTROL (Lines 166-231)

Controls the cutting disk motor startup/shutdown and VFD/Star-Delta selection.

### Disk Start Sequence

```
  ┌────────────────────────────────────────────────────────────────────┐
  │                                                                    │
  │  I 4.0 (Disk ON)                                                  │
  │    │                                                               │
  │    ├─ A I 4.0, A Q 7.1, AN T 0                                   │
  │    │  S F 1.1 (startup sequence flag)                              │
  │    │                                                               │
  │    ├─ F 1.1 triggers timer chain:                                 │
  │    │  ├─ T 21 (KT 020.1 = 2s) → motor ramp-up                   │
  │    │  ├─ After T 21: Q 8.6 = 1 (spindle motor ON)                │
  │    │  └─ T 0 (KT 060.2 = 60s) → cooldown lockout                │
  │    │                                                               │
  │    ├─ Star-Delta sequence if NOT Q 7.1 (VFD):                    │
  │    │  ├─ Q 6.6 = 1 (Star contactor)                               │
  │    │  ├─ T 2 (KT 080.1 = 8s) → transition timer                  │
  │    │  ├─ After T 2: Q 6.6 = 0, Q 6.7 = 1 (Delta)                │
  │    │  └─ Motor at full speed                                       │
  │    │                                                               │
  │    └─ F 1.0 = 1 (disk motor running flag)                         │
  │                                                                    │
  │  I 4.1 (Disk OFF) OR I 4.4 released:                             │
  │    └─ R F 1.0, R F 1.1 (immediate shutdown)                      │
  │                                                                    │
  └────────────────────────────────────────────────────────────────────┘
```

### Laser Control

```
  I 4.7 → toggle F 3.7 (laser ON/OFF)
  F 3.7 = 1 → Q 7.2 = 1 (laser relay)
  T 14 (300 × 0.1s = 30s)  → auto-off safety
  T 15 (300 × 1s = 300s)   → 5-minute hard limit
```

---

## SECTION [3] — PROGRAMMED MOTION TIMEOUT (Lines 233-243)

```
  Simple section — just the sequence timeout timer:

  IF F 5.3 (in programmed mode):
    Start T 1 (KT 600.1 = 60 seconds)
    IF T 1 expires:
      Force stop all programmed motion
      Set F 5.4 (fault flag)
```

---

## SECTION [4] — GENERAL SPEED ENABLE (Lines 244-262)

### F 0.7 — General Enable Logic

```
  F 0.7 = (Q 7.1 AND Q 8.6 AND T 3 AND I 73.7)
          OR
          (F 1.0 AND Q 6.7)

  Meaning: Speed reference is valid when:
    EITHER: VFD selected AND spindle on AND ramp done AND velocity signal
    OR:     Disk running AND Delta contactor active
```

### Toggle Flip-Flop (F 0.5 → F 0.6 → F 1.2)

```
  ┌──────────────────────────────────────────────────────────────┐
  │  T 13 (KT 005.1 = 0.5s) → F 0.5 (clock)                   │
  │                                                              │
  │  Rising edge of F 0.5:                                       │
  │  ├─ IF F 0.6 = 0: S F 1.2  (toggle output = 1)             │
  │  └─ IF F 0.6 = 1: R F 1.2  (toggle output = 0)             │
  │                                                              │
  │  F 0.6 tracks F 1.2 state (edge detection latch):           │
  │  ├─ A F 0.5, AN F 0.6 → S F 1.2                            │
  │  ├─ A F 0.5, A F 0.6 → R F 1.2                             │
  │  └─ AN F 0.5: latch F 0.6 = F 1.2                          │
  │                                                              │
  │  Result: F 1.2 toggles ON/OFF every 0.5 seconds             │
  │  Used as: Blink clock for indicators, step doubling counter  │
  └──────────────────────────────────────────────────────────────┘
```

---

## SECTION [5] — LIMIT SWITCH SAFETY (Lines 264-318)

### Q 73.2 — Composite Limit Signal to Elbo

```
  Q 73.2 indicates "a motor is running AND has reached its limit switch."
  This tells the Elbo to stop sending velocity commands for that axis.

  Q 73.2 = (Q 6.0 AND (I 5.2 OR I 5.3))     ← Fwd/Rev at limit
         OR (Q 6.1 AND (I 5.0 OR I 5.1))     ← Left/Right at limit
         OR (Q 6.2 AND (I 5.4 OR I 5.5))     ← Up/Down at limit
         OR (Q 6.3 AND (I 5.6 OR I 5.7))     ← Tilt at limit
```

### Direct Pass-Throughs

```
  Q 72.5 = I 5.3   (backward limit → Elbo)
  Q 72.6 = I 5.1   (left limit → Elbo)
```

### Spianatura Special Logic

```
  F 9.4 = F 0.4 AND I 72.0 AND I 72.5 AND T 19
  Meaning: Spianatura mode AND medium speed AND fast speed AND timer active
  Q 72.4 = complex logic involving F 9.4, I 5.2, limit conditions
```

---

## SECTION [6] — MAIN POWER & TABLE (Lines 319-360)

### Main Power Relay

```
  Q 7.4 is the master power relay — keeps the whole system alive.

  Power ON:   O I 2.0, ON I 2.0  (any edge on I 2.0)
              OR I 4.4 (power button held)
              → S Q 7.4

  Power OFF:  T 4 (KT 030.2 = 30s timeout)
              A T 4 → R Q 7.4

  Emergency:  AN I 4.4 → immediate R Q 7.4
```

### Table Control (FB18)

```
  JU FB 18 (INCLBANC)
  NAME: INCLBANC
  
  See FUNCTION_BLOCK_REFERENCE.md for full FB18 analysis.
```

### Tilt/Vertical Mode Selection

```
  A  I 4.3, AN F 5.0 → S F 3.0 (Tilt mode)
  AN I 4.3, AN F 5.0 → S F 3.1 (Vertical mode)
  
  Note: Can only switch mode when no motion active (F 5.0 = 0)
```

### Motion Pulse

```
  T 24 (KT 020.1 = 2s)
  A T 24 → = Q 7.5 (motion pulse output)
```

---

## SECTION [7] — INVERTER SEQUENCE CONTROL (Lines 361-468)

### Motion Detection

```
  F 5.0 = Q 6.0 OR Q 6.1 OR Q 6.2 OR Q 6.3 OR Q 6.4 OR Q 6.5

  Meaning: F 5.0 = 1 if ANY motor contactor is energized.
  This is the master "motion active" flag used throughout OB1.
```

### Inverter 1 Sequence (F 1.4)

```
  ┌──────────────────────────────────────────────────────────────┐
  │  F 1.4 manages the primary inverter (VFD #1).               │
  │                                                              │
  │  SET conditions:                                              │
  │  A I 4.4 (power ON)                                          │
  │  A I 73.7 (velocity enable from Elbo)                        │
  │  AN I 73.5 (NOT at forward limit)                            │
  │  Various axis and direction conditions...                    │
  │  → S F 1.4                                                   │
  │                                                              │
  │  Timer: T 5 (KT 050.1 = 5s) ← inverter response window     │
  │                                                              │
  │  RESET conditions:                                            │
  │  AN I 4.4 (power OFF) → R F 1.4                             │
  │  Various fault conditions → R F 1.4                          │
  └──────────────────────────────────────────────────────────────┘
```

### Emergency Stop Logic

```
  F 1.6 is the System Ready / Stop Gate flag.

  When F 1.6 = 1 AND stop conditions are met:
    R Q 6.0    (stop Fwd/Rev)
    R Q 6.1    (stop Left/Right)
    R Q 6.2    (stop Up/Down partial)
    R Q 6.3    (stop Up/Down full)
    R Q 6.4    (stop Table Rotation)
    R Q 6.5    (stop Disk Rotation)

  ALL motors stop simultaneously. This is the primary safety mechanism.
```

### Speed Output to QW 64

```
  The Elbo-controlled speed reference goes to QW 64 (NOT QW 66).
  QW 66 is for manual/potentiometer speed (via FB14/FB15/FB16).

  QW 64 = loaded from DW values in PB20 depending on axis mode:
    DW 0 → QW 64 (default/idle speed)
    DW 1 → QW 64 (forward speed)
    DW 2 → QW 64 (reverse speed)
```

---

## SECTION [8] — TABLE LOCK & PROGRAM SEQUENCING (Lines 470-532)

### Table Lock Solenoid (Q 8.7)

```
  ┌──────────────────────────────────────────────────────────────┐
  │  Q 8.7 = Table Lock Solenoid                                │
  │                                                              │
  │  LOCK (energize Q 8.7):                                      │
  │  A I 73.3 → S Q 8.7                                         │
  │                                                              │
  │  UNLOCK (release Q 8.7):                                     │
  │  Complex conditions must ALL be met:                         │
  │  ├─ AN I 72.3 (Elbo lock command OFF)                       │
  │  ├─ AN I 73.3 (CNC lock OFF)                                │
  │  ├─ AN I 3.2 (NOT pressing table CW)                        │
  │  ├─ AN I 3.3 (NOT pressing table CCW)                       │
  │  ├─ AN Q 6.4 (table motor NOT running)                      │
  │  └─ T 8 (KT 020.1 = 2s debounce passed)                    │
  │  → R Q 8.7 (unlock)                                          │
  │                                                              │
  │  Safety: Table stays locked unless ALL conditions met.       │
  │  Fail-safe: Default is LOCKED (Q 8.7 = 1).                  │
  └──────────────────────────────────────────────────────────────┘
```

### Program Dispatch

```
  IF F 5.1 = 1 (Elbo program started):
    JU PB 10  → routes to PB20/30/40/50/52 based on Elbo axis

  IF F 5.2 = 1 (Manual override in auto mode):
    JU PB 11  → routes to PB21/31/41/51/53 based on manual buttons

  Sequence State Machine:
  F 5.3 = F 0.1 OR F 0.2 OR F 0.3    (any automatic program active)
  F 5.1 = A F 5.3 → S F 5.1          (start sequence)
  F 5.4 = complex fault/stop logic    (sequence stopped or fault)
```

---

## SECTION [9] — INVERTER OUTPUT GENERATION (Lines 533-600)

### Inverter Command Generation

```
  ┌──────────────────────────────────────────────────────────────┐
  │  INVERTER 1 OUTPUTS (Q 8.0-Q 8.2):                         │
  │                                                              │
  │  Q 8.0 (Inv1 Forward):                                      │
  │    AN F 5.4 (no fault)                                       │
  │    A  F 1.4 (Inv1 sequence active)                           │
  │    A  F 4.2 (phase 1 complete / forward)                     │
  │    = Q 8.0                                                    │
  │                                                              │
  │  Q 8.1 (Inv1 Reverse):                                      │
  │    AN F 5.4, A F 1.4, A F 4.3                               │
  │    = Q 8.1                                                    │
  │                                                              │
  │  Q 8.2 (Inv1 Speed Enable):                                 │
  │    (Q 8.0 OR Q 8.1) AND F 4.5                               │
  │    = Q 8.2                                                    │
  │                                                              │
  │  Phase Timing:                                                │
  │    F 4.2 = F 4.0 delayed by T 9 (KT 010.0 = 0.1s)          │
  │    F 4.3 = F 4.1 delayed by T 10 (KT 010.0 = 0.1s)         │
  │                                                              │
  │  INVERTER 2 OUTPUTS (Q 8.4-Q 8.5):                         │
  │    Similar logic but uses F 1.3 and T 22 timing             │
  │    Q 8.3 = (Q8.4 OR Q8.5) AND F4.5                         │
  └──────────────────────────────────────────────────────────────┘
```

### Spindle Motor & Status

```
  Q 8.6 = A T 21 (spindle ON after T21 ramp)
  Q 73.1 = O Q 8.6, O F 1.0  (disk motor status → Elbo)
  Q 73.3 = AN F 1.4  (Inv1 NOT active → Elbo)
  Q 73.4 = A F 1.5, AN F 1.3  (Inv2 status → Elbo)
```

---

## SECTION [10] — SPEED CONTROL (Lines 601-659)

### Speed Mode Selection

```
  ┌──────────────────────────────────────────────────────────────┐
  │  Three speed editing modes, determined by which program      │
  │  mode is active and what buttons are pressed:                │
  │                                                              │
  │  F 2.0 (Passages speed):                                     │
  │    = (F 3.5 OR F 3.6) AND (F 0.2 OR F 0.3) AND I 2.7      │
  │    Meaning: Speed+/- pressed in passage mode while backing  │
  │                                                              │
  │  F 2.1 (Spianatura speed):                                  │
  │    = (F 3.5 OR F 3.6) AND F 0.4                             │
  │    Meaning: Speed+/- pressed in Spianatura mode              │
  │                                                              │
  │  F 2.2 (General speed):                                      │
  │    = AN F 2.0 AND AN F 2.1 AND (F 3.5 OR F 3.6)            │
  │    Meaning: Speed+/- pressed but not in passage/spianatura   │
  └──────────────────────────────────────────────────────────────┘
```

### Speed Button Flag Generation

```
  F 3.5 = I 4.5 AND NOT I 4.6   (Speed+ pressed, Speed- released)
  F 3.6 = I 4.6 AND NOT I 4.5   (Speed- pressed, Speed+ released)

  Mutual exclusion: can't press both Speed+ and Speed- simultaneously.
```

### Speed Dispatcher Call

```
  JU FB 14 (RICH.POT) — Speed Reference Dispatcher

  FB14 checks F 2.0 / F 2.1 / F 2.2 and routes to the correct
  DW for the active speed mode:
    F 2.0 = 0: DW 2 (passage speed) → QW 66
    F 2.1 = 0: DW 18 (Spianatura speed) → QW 66 via SCALAR
    F 2.2 = 0: DW 1 (general speed) → QW 66

  See FUNCTION_BLOCK_REFERENCE.md for full FB14 analysis.
```

---

## COMPLETE EXECUTION FLOW — EXAMPLE SCENARIO

### "Operator Does a Manual Forward Cut"

```
  ┌─ STEP 1: SELECT MANUAL MODE ──────────────────────────────────────┐
  │  Operator presses I 2.0 (Manual button)                           │
  │  OB1[1]: A I 2.0, AN F 6.3, all jog buttons OFF                  │
  │  → S F 0.0 (Manual mode ON)                                       │
  │  → Q 73.0 = 1 (tells Elbo: "in program mode")                    │
  └────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
  ┌─ STEP 2: START DISK MOTOR ────────────────────────────────────────┐
  │  Operator presses I 4.0 (Disk ON)                                 │
  │  OB1[2]: S F 1.1 (startup sequence)                               │
  │  → T 21 starts (2s ramp) → T 2 starts (8s Star→Delta)           │
  │  → Q 6.6 = 1 (Star), then Q 6.7 = 1 (Delta after T 2)          │
  │  → Q 8.6 = 1 (spindle ON after T 21)                             │
  │  → F 1.0 = 1 (disk running flag)                                  │
  └────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
  ┌─ STEP 3: SELECT AXIS & DIRECTION ─────────────────────────────────┐
  │  Operator presses I 2.6 (Avanço / Forward button)                 │
  │  OB1[8]: F 5.2 = F 0.0 = 1 → JU PB 11 (manual axis routing)    │
  │  PB11: I 2.6 pressed, NOT F 0.4, NOT F 5.0                      │
  │  → F 6.4 = 1 (Fwd/Rev axis selected)                             │
  │  → JC PB 21 (manual Fwd/Rev motor control)                       │
  │  PB21: Sets Q 6.0 = 1 (Forward/Reverse contactor ON)             │
  └────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
  ┌─ STEP 4: MOTION ACTIVE ──────────────────────────────────────────┐
  │  OB1[7]: F 5.0 = OR(Q 6.0 = 1) → F 5.0 = 1 (motion active)    │
  │  → Inverter 1 sequence: F 1.4 logic activates                    │
  │  OB1[9]: F 4.2 = F 4.0 delayed by T 9 (0.1s)                   │
  │  → Q 8.0 = 1 (Inv1 Forward command)                              │
  │  → Q 8.2 = 1 (Inv1 Speed Enable)                                 │
  │  → Motor accelerates, material moves forward                     │
  └────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
  ┌─ STEP 5: LIMIT REACHED ──────────────────────────────────────────┐
  │  Material reaches forward limit: I 5.2 = 1                       │
  │  OB1[5]: Q 6.0 AND I 5.2 → Q 73.2 = 1 (signal to Elbo)         │
  │  OB1[7]: Limit conditions → F 1.6 triggers stop                 │
  │  → R Q 6.0 (stop Fwd/Rev contactor)                              │
  │  → F 5.0 = 0 (no motion)                                         │
  │  → System ready for next command                                  │
  └────────────────────────────────────────────────────────────────────┘
```

---

## QUICK REFERENCE: AWL INSTRUCTIONS USED IN OB1

```
  ┌───────┬──────────────────────────────────────────────────────────┐
  │ Instr │ Meaning                                                 │
  ├───────┼──────────────────────────────────────────────────────────┤
  │ A     │ AND — check bit = 1                                     │
  │ AN    │ AND NOT — check bit = 0                                 │
  │ O     │ OR — starts new AND chain, OR'd with previous           │
  │ ON    │ OR NOT — OR with inverted bit                           │
  │ A(    │ AND with nested expression (open parentheses)           │
  │ O(    │ OR with nested expression                               │
  │ )     │ Close parentheses                                       │
  │ S     │ SET bit to 1 (latched — stays until R)                  │
  │ R     │ RESET bit to 0 (latched — stays until S)                │
  │ =     │ ASSIGN — direct (non-latched) output                   │
  │ L     │ LOAD value into accumulator (ACCU 1)                   │
  │ T     │ TRANSFER accumulator to destination                     │
  │ SP    │ Start Pulse timer (retriggerable)                       │
  │ SE    │ Start Extended-pulse timer (non-retriggerable)          │
  │ SD    │ Start Delayed timer                                     │
  │ SF    │ Start Timer as flag (monoflop)                          │
  │ JC    │ Jump Conditional (if RLO = 1)                           │
  │ JU    │ Jump Unconditional                                      │
  │ BEC   │ Block End Conditional (return if RLO = 1)               │
  │ BE    │ Block End (unconditional return)                         │
  │ +F    │ ADD 16-bit fixed-point integers (ACCU1 + ACCU2)        │
  │ -F    │ SUBTRACT 16-bit fixed-point integers                    │
  │ <=F   │ COMPARE less-or-equal 16-bit fixed-point integers      │
  │ <F    │ COMPARE less-than 16-bit fixed-point integers           │
  │ >=F   │ COMPARE greater-or-equal 16-bit fixed-point integers   │
  │ ><F   │ COMPARE not-equal 16-bit fixed-point integers           │
  │ AW    │ AND Word (bitwise AND on 16-bit word)                  │
  │ CD    │ Count Down (decrement counter)                          │
  │ KC    │ Constant Counter value                                  │
  │ KT    │ Constant Timer value (format: KT xxx.y)                │
  │ KH    │ Constant Hex value                                      │
  │ KF    │ Constant Fixed-point value                              │
  │ FW    │ Flag Word (16-bit working register)                     │
  │ DW    │ Data Word (from DB10)                                   │
  │ IW    │ Input Word (16-bit input register)                      │
  │ QW    │ Output Word (16-bit output register)                    │
  └───────┴──────────────────────────────────────────────────────────┘

  ⚠ IMPORTANT: +F, -F, <=F, <F, >=F, ><F are NOT floating-point!
  They are 16-bit FIXED-POINT INTEGER operations. The "F" stands
  for "Fixed" (Fest in German), NOT "Float". The S5 CPU 100 does
  not have a floating-point unit.
```

---

*End of OB1 Walkthrough*

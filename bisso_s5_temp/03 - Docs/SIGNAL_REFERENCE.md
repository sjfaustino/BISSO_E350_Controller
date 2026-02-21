# BISSO S5 PLC — COMPLETE SIGNAL REFERENCE
## Cross-Referenced Against Raw STL Code (OB1, FB10-FB18, PB9-PB11, PB20)

**Last Updated**: 2026-02-20
**Source of Truth**: Raw STL files in `01 - STL/`

> **NOTE**: This document was written by cross-referencing every instruction in the
> raw STL code. Every signal listed here has been verified against at least one
> raw code file. Signals marked `??` have inferred names — they are not labeled
> in the original Siemens S5 program.

---

## SYSTEM OVERVIEW — ASCII Block Diagram

```
  ┌─────────────────────────────────────────────────────────────────────────┐
  │                    OPERATOR PANEL (Botoneira/Manipulo)                  │
  │                                                                         │
  │  I 2.0  Program Manual      I 4.0  Disco Ligar (Disk ON)              │
  │  I 2.1  Program Translação  I 4.1  Disco Desligar (Disk OFF)          │
  │  I 2.2  Program Pass+Trans  I 4.2  Estrela-Triangulo/VFD selector     │
  │  I 2.3  Program Passagens   I 4.3  Inclinação/Vertical selector       │
  │  I 2.4  Mnp Trans Direita   I 4.4  Máquina Ligar (Power ON)           │
  │  I 2.5  Mnp Trans Esquerda  I 4.5  + Velocidade (Speed+)              │
  │  I 2.6  Mnp Avanço (Front)  I 4.6  - Velocidade (Speed-)              │
  │  I 2.7  Mnp Recuo (Back)    I 4.7  Laser ON/OFF                       │
  │  I 3.0  Mnp Baixar (Down)                                              │
  │  I 3.1  Mnp Subir (Up)      I 9.0  Btn Mesa 1 Subir                   │
  │  I 3.2  Mnp Rot Mesa Hor    I 9.1  Btn Mesa 1 Descer                  │
  │  I 3.3  Mnp Rot Mesa Anti   I 9.2  Btn Mesa 2 Subir                   │
  │  I 3.4  Mnp Rot Disco Up    I 9.3  Btn Mesa 2 Descer                  │
  │  I 3.5  Mnp Rot Disco Down  I 9.4  (?)                                │
  │  I 3.7  FastCmd (3x→F6.3)                                              │
  └─────────┬───────────────────────────────────────────────────────────────┘
            │
            ▼
  ┌─────────────────────────────────────────────────────────────────────────┐
  │                     SIEMENS S5 PLC (CPU 100)                            │
  │                                                                         │
  │   OB1 Main Loop ─┬─ FB15 (HELP.)     Fast mode / manual axis control   │
  │                   ├─ FB18 (INCLBANC)  Table hydraulics                  │
  │                   ├─ PB10 → PB20..52  Elbo-controlled axis motion       │
  │                   ├─ PB11 → PB21..53  Manual button axis motion         │
  │                   └─ FB14 (RICH.POT)  Speed reference dispatcher        │
  │                                                                         │
  │   DB10: 45 Data Words (speed presets, parameters, scale factors)        │
  └─────────┬───────────────────────────────────────────────────────────────┘
            │
            ▼
  ┌─────────────────────────────────────────────────────────────────────────┐
  │                    OUTPUTS TO FIELD DEVICES                             │
  │                                                                         │
  │  Q 6.0  Contator Avanço/Recuo (C3)    Q 8.0  Inv1 Frente              │
  │  Q 6.1  Contator Esquerda/Direita (C4) Q 8.1  Inv1 Atrás              │
  │  Q 6.2  Contator Subir/Descer (C5)    Q 8.2  Inv1 Velocidade          │
  │  Q 6.3  Contator Vertical Compl (C6)  Q 8.3  Inv2 Velocidade          │
  │  Q 6.4  Contator Rot Mesa (C7)        Q 8.4  Inv2 Frente              │
  │  Q 6.5  Contator Rot Disco (C9)       Q 8.5  Inv2 Atrás              │
  │  Q 6.6  Estrela (Star)                Q 8.6  Motor Spindle            │
  │  Q 6.7  Triângulo (Delta)             Q 8.7  Trava Mesa (Lock)        │
  │                                                                         │
  │  QW 64  VFD Ref Velocidade (Elbo)     QW 66  VFD Ref Velocidade (Man) │
  └─────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────────┐
  │                    ELBO MICROCONTROLLER INTERFACE                       │
  │                                                                         │
  │  COMMANDS FROM ELBO TO PLC (Bus 72/73):  STATUS FROM PLC TO ELBO:      │
  │  I 72.0  Asse Taglio (X Req - PB20)    Q 73.2  Composite Limit/Status  │
  │  I 72.1  Asse Traslaz. (Y Req - PB40)  Q 73.4  F.C. Avanzamento Avanti │
  │  I 72.2  Asse Cala (Z Req - PB30)      Q 73.5  F.C. Avanzamento Indiet.│
  │  I 72.3  Asse Rot. Banco (Table Req)   Q 73.6  F.C. Traslazione Sinist.│
  │  I 72.4  Asse Rot. Testa (Disk Req)    Q 73.7  Table hydraulics status │
  │  I 72.5  Direzione "+"                 Q 72.0  Program Status (F 0.3)  │
  │  I 72.6  Direzione "-"                 Q 72.1  Translation Status (F0.1)│
  │  I 72.7  Segnale V/B (Ready - PB30)    Q 72.5  F.C. Recuo pass-through │
  │                                        Q 72.6  F.C. Esquerda pass-thru │
  │  I 73.7  Velocity Enable (Master)                                      │
  │                                                                         │
  │  NOTE: S5 code uses IB 72 for axis dispatching despite schematic P13    │
  │        mapping these to IB 73. IB 72 is the definitive functional byte. │
  └─────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────────┐
  │                    LIMIT SWITCHES (Fins de Curso)                       │
  │                                                                         │
  │  I 5.0  FC Direita (Right limit)       I 5.4  FC Baixo parcial (Down) │
  │  I 5.1  FC Esquerda (Left limit)       I 5.5  FC Cima parcial (Up)    │
  │  I 5.2  FC Avanço (Forward limit)      I 5.6  FC Baixo completo       │
  │  I 5.3  FC Recuo (Backward limit)      I 5.7  FC Cima completo        │
  └─────────────────────────────────────────────────────────────────────────┘
```

---

## INPUTS (I) — DETAILED MAPPING

### I 2.x — Manual Control Panel (Manipulo/Botoneira)

```
  I 2.0 ──► S F 0.0   Program Manual mode
             Conditions: AN F 6.3, all jog buttons OFF (I 2.4-3.5 = 0), AN F 5.0
             Reset:      ON I 2.0 OR ON I 4.4 → R F 0.0

  I 2.1 ──► S F 0.1   Program Translação
             Guard:      AN I 72.7 (Elbo Emergency OFF), AN F 0.4 (NOT Spianatura)
             Reset:      ON I 2.1 OR ON I 4.4 → R F 0.1

  I 2.2 ──► S F 0.2   Program Passagens + Translação
             Guard:      AN I 72.7
             Reset:      ON I 2.2 OR ON I 4.4 → R F 0.2

  I 2.3 ──► S F 0.3   Program Passagens only
             Guard:      AN I 72.7
             Reset:      ON I 2.3 OR ON I 4.4 → R F 0.3

### I 9.x — Table Tilting (Loading Slabs)
These buttons control the two hydraulic loading tables. Based on FB18.

```
  I 9.1 ──► Table 1 Horizontal (Cutting position)
  I 9.2 ──► Table 1 Vertical (Loading position)
  I 9.3 ──► Table 2 Horizontal (Cutting position)
  I 9.4 ──► Table 2 Vertical (Loading position)
  I 9.0 ──► Table Selector (Selects which table the hydraulics move)
```

### Modes: C, T, C+T (The Automatic Modes)
The physical switch positions correspond to these inputs and behaviors:

| Label | Input | PLC Flag | Feedback to PosiPro | Behavior |
| :--- | :--- | :--- | :--- | :--- |
| **"T"**  | I 2.1 | F 0.1 | Q 72.1, Q 72.2 (Transl.) | Shifting mode. No speed override. |
| **"C+T"**| I 2.2 | F 0.2 | (Internal Hybrid) | **Most Permissive.** Combined Auto mode. Allows Manual Speed Adj. |
| **"C"**  | I 2.3 | F 0.3 | Q 72.0 (Prog Status) | Passages only. Blocks table lock reset for safety. |

**Note on Speed Override**: The PLC allows the operator to manually override the PosiPro cutting speed using the +/- buttons **ONLY** in `C+T` and `C` modes (`F 0.2` and `F 0.3`).

**Note on Hydraulics (Q 7.4)**: In all three modes, the PLC keeps the pump alive (T1 timer) to ensure the tables can be tilted at any moment during slab loading/handling.

  I 3.3 ──► Manipulo Rotação Mesa Anti-Horário (54)
             Same as I 3.2 but opposite direction

  I 3.4 ──► Manipulo Rotação Disco para cima (Disk tilt up)
             Used in:   FB15 Network 6 (Rot Disk axis) — sets F 7.5
             Used in:   PB11 (manual axis routing) — calls PB53
             ⚠ NOT unused/reserved — this is an active input

  I 3.5 ──► Manipulo Rotação Disco para baixo (Disk tilt down)
             Same as I 3.4 but opposite direction
             ⚠ NOT unused/reserved — this is an active input

  I 3.7 ──► FastCmd — "Botão Rápido" (56)
             Triple-press mechanism:
               1. Press I3.7 → starts T17 (2 second window)
               2. T17 running → loads C1 = 3
               3. Each press of I3.7 → CD C1 (decrement counter)
               4. When C1 = 0 AND T17 still running → S F 6.3
             Result: F 6.3 enables FB15 fast mode and PB9
             Also used in FB15 as AN I 3.7 (axis disabling condition)
             Also checked in PB20 Networks 1,2 (OR I 3.7 for speed selection)
```

### I 4.x — Special Function Buttons

```
  I 4.0 ──► Disco Ligar (Disk ON)
             OB1[2]: A I 4.0, A Q 7.1, AN T 0 → S F 1.1
             Starts disk motor startup sequence

  I 4.1 ──► Disco Desligar (Disk OFF)
             OB1[2]: ON I 4.1 (in OR chain) → R F 1.1, R F 1.0

  I 4.2 ──► Selector Estrela-Triângulo / VFD
             OB1[2]: A I 4.2, AN F 1.0 → S Q 7.1 (VFD selector)
             Selects between Star-Delta soft start and VFD drive

  I 4.3 ──► Selector Inclinação / Vertical
             OB1[6]: A I 4.3, AN F 5.0 → S F 3.0 (Tilt mode)
             OB1[6]: AN I 4.3, AN F 5.0 → S F 3.1 (Vertical mode)
             Also: FB15 Network 4 uses AN I 4.3 as a safety interlock
                   for the Up/Down axis (prevents vertical motion when
                   I 4.3 is active — i.e., tilt mode selected)

  I 4.4 ──► Máquina Ligar / Power ON (Main power master)
             OB1[1]: Used in every R F 0.x chain (ON I 4.4 → R F 0.x)
             OB1[6]: A I 4.4, A T 5/T 6 → enables inverter commands
             OB1[7]: AN I 4.4 → R F 1.4, R F 1.5 (kill inverter sequences)
             ⚠ MOST-REFERENCED INPUT — appears in nearly every OB1 section

  I 4.5 ──► Aumentar Velocidade / Speed+ (62)
             OB1[10]: A I 4.5, AN I 4.6 → = F 3.5 (Speed+ flag)
             PB9:     A I 4.5 → = F 2.3
             FB15:    A I 4.5, AN I 4.6 → = F 3.5

  I 4.6 ──► Diminuir Velocidade / Speed- (63)
             OB1[10]: A I 4.6, AN I 4.5 → = F 3.6 (Speed- flag)
             PB9:     A I 4.6 → = F 2.4

  I 4.7 ──► Laser ON/OFF
             OB1[2]: A I 4.7, conditions → = Q 7.2 (laser relay)
             Timers: T 14 (auto-off), T 15 (hard limit)
```

### I 5.x — Limit Switches (Fins de Curso) ⚠ SAFETY CRITICAL

```
  Limit Switch Convention in this machine:
  ┌──────────────────────────────────────────────────┐
  │  Signal = 1 → switch CLOSED → at that position  │
  │  Signal = 0 → switch OPEN → NOT at that limit   │
  └──────────────────────────────────────────────────┘

  I 5.0 ──► FC Direita (Right end limit)
             Monitored in OB1[5]: part of Q 73.2 limit switch alert

  I 5.1 ──► FC Esquerda (Left end limit)
             Also: = Q 72.6 (direct pass-through to Elbo in OB1[5])

  I 5.2 ──► FC Avanço (Forward end limit) — ⭐ MOST USED
             PB20: A I 5.2 in forward motion logic (ramp timer T 12)
             PB20: AN I 5.2 in OR-before chain (feed condition)
             OB1[5]: passed through as part of limit logic

  I 5.3 ──► FC Recuo (Backward end limit)
             PB20: A I 5.3 in reverse motion logic (ramp timer T 16)
             Also: = Q 72.5 (direct pass-through to Elbo in OB1[5])

  I 5.4 ──► FC Baixo parcial (Partial down limit)
             OB1[5]: part of Q 6.2 safety logic

  I 5.5 ──► FC Cima parcial (Partial up limit)
             OB1[5]: part of Q 6.2 safety logic

  I 5.6 ──► FC Baixo completo (Full down limit — tilt/vertical)
             OB1[5]: part of Q 6.3 safety logic

  I 5.7 ──► FC Cima completo (Full up limit — tilt/vertical)
             OB1[5]: part of Q 6.3 safety logic

  Limit Switch Safety Logic (OB1 Section [5]):
  ┌──────────────────────────────────────────────────────────────────────┐
  │ Q 73.2 = (Q6.0 AND (I5.2 OR I5.3))                                │
  │       OR (Q6.1 AND (I5.0 OR I5.1))                                │
  │       OR (Q6.2 AND (I5.4 OR I5.5))                                │
  │       OR (Q6.3 AND (I5.6 OR I5.7))                                │
  │                                                                     │
  │ This sends an "at limit" signal to the Elbo whenever a motor       │
  │ contactor is ON and the corresponding limit switch is reached.     │
  └──────────────────────────────────────────────────────────────────────┘
```

### I 9.x — Table Controls

```
  I 9.0 ──► Btn Mesa 1 Subir (Table 1 Up button)
             FB18: = F 11.0 (direct latch)
             FB18: A I 9.0, A I 9.1, AN I 9.2 → = F 10.1 (Table 1 Up action)
             FB18: Used as gate for Table 2 (AN I 9.0 required for Table 2)

  I 9.1 ──► Btn Mesa 1 Descer (Table 1 Down button)
             FB18: = F 11.1 (direct latch)
             FB18: A I 9.0, A I 9.2, AN I 9.1 → = F 10.2 (Table 1 Down action)

  I 9.2 ──► Btn Mesa 2 Subir (Table 2 Up button)
             FB18: = F 11.2 (direct latch)

  I 9.3 ──► Btn Mesa 2 Descer (Table 2 Down button)
             FB18: = F 11.3 (direct latch)
             FB18: A I 9.3, AN I 9.0, AN I 9.4 → = F 10.3 (Table 2 Up action)

  I 9.4 ──► (Additional table input — exact function unclear)
             FB18: = F 11.4 (direct latch)
             FB18: A I 9.4, AN I 9.0, AN I 9.3 → = F 10.4 (Table 2 Down action)
```

### I 72.x — Elbo Microcontroller Inputs ⚠ CRITICAL

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │ ⚠ THESE SIGNALS COME FROM THE ELBO ELECTRONIC INCLINOMETER.       │
  │ They are NOT VFD status signals or pressure sensors.               │
  │ Previous documentation was COMPLETELY WRONG for I 72.4-72.7.      │
  └──────────────────────────────────────────────────────────────────────┘

  I 72.0 ──► Elbo Medium (2°-5° inclination range)
             PB20: A I 72.0 in Networks 1,2 (part of motion enable conditions)
             PB20: A I 72.0 in Network 4 (safety logic with T 7)
             PB10: O I 72.0 (Elbo axis selection — Fwd/Rev OR Rot Table)
             Used 14+ times across the codebase
             ⚠ Previous docs SWAPPED this with I 72.1

  I 72.1 ──► Cmd Translação (Translation command from Elbo)
             OB1[1]: A F 0.1 → = Q 72.1 (pass F0.1 status back to Elbo)
             PB10: A I 72.1 — triggers Left/Right axis routing (F 6.1 → PB40)
             NOT a speed mode signal — it's an axis COMMAND

  I 72.2 ──► Cmd Rotação / Inclinação (Rotation command from Elbo)
             PB20: AN I 72.2 → S Q 6.0 (Fwd/Rev contactor, gate condition)
             PB20: A I 72.2, A I 72.5 → S F 3.2 (fast mode flag)
             PB10: A I 72.2 (triggers Q6.2/Q6.3 axis routing)
             OB1[1]: A F 0.1 → = Q 72.2 (pass status to Elbo)

  I 72.3 ──► Cmd Trava Mesa / Lock Table (Table lock command from Elbo)
             OB1[8]: A I 73.3 → S Q 8.7 (table lock solenoid)
                     Complex unlock logic with AN I 72.3 gates
             PB10: A I 72.3 — triggers table rotation routing (F 6.2 → PB50)
             PB11: O I 72.3 — part of manual table rotation routing
             Also: Used in OB1[8] table lock/unlock logic (23 references across code)

  I 72.4 ──► Elbo Disco (Disk cutting signal from Elbo)
             PB10: A I 72.4 — triggers disk rotation routing → PB52
             Only 1 direct reference in motion logic
             ⚠ Previous docs called this "Pressostato" — WRONG

  I 72.5 ──► Elbo Fast (>5° inclination — high speed range)
             PB20: A I 72.5 in Network 1 (part of forward motion chain)
             PB20: A I 72.2, A I 72.5 → S F 3.2 (fast mode flag)
             Used 11+ times across the codebase
             ⚠ Previous docs called this "VFD1 Running" — COMPLETELY WRONG

  I 72.6 ──► Elbo Slow (<2° inclination — low speed range)
             PB20: A I 72.6 in Network 2 (part of reverse motion chain)
             Used 6+ times across the codebase
             ⚠ Previous docs called this "VFD2 Running" — COMPLETELY WRONG

  I 72.7 ──► Elbo Emergency (Emergency/fault signal from Elbo)
             OB1[1]: AN I 72.7 — guard on F 0.1, F 0.2, F 0.3 (SET conditions)
             PB20: A I 72.7 — part of Network 4 safety logic (F 4.5)
             Used 9+ times, mostly as AN (guard/interlock)
             ⚠ Previous docs called this "Disk Running" — COMPLETELY WRONG
```

### I 73.x — CNC/Elbo Feedback Inputs

```
  I 73.0 ──► ??CNC Consenso (CNC consent/interlock signal)
             PB20: AN I 73.0 in Networks 1,2 (part of motion enable)
             OB1[5]: part of complex limit/safety logic

  I 73.2 ──► Cmd Spianatura (Spianatura/leveling command)
             OB1[1]: AN F 0.1, A I 73.2, AN I 72.7 → S F 0.4
             Sets the Spianatura (automatic leveling) mode flag

  I 73.3 ──► Cmd Lock Table (direct input for table lock)
             OB1[8]: A I 73.3 → S Q 8.7 (energize table lock solenoid)

  I 73.5 ──► FC Avanço signal (forward limit signal — passed to OB1[7])
             OB1[7]: AN I 73.5 used in inverter sequence F 1.4 logic
             NOT a joystick direction signal in this machine

  I 73.6 ──► FC Esquerda signal (left limit signal — passed to OB1[7])
             OB1[7]: AN I 73.6 used in inverter sequence F 1.3 logic

  I 73.7 ──► Velocidade Enable (Speed/velocity master gate)
             OB1[4]: A I 73.7 — part of F 0.7 (General Enable) logic
             OB1[7]: A I 73.7 — gates all inverter commands (Q 8.x)
             ⚠ CRITICAL: When I 73.7 = 0, the PLC resets ALL contactors
```

---

## OUTPUTS (Q) — DETAILED MAPPING

### Q 6.x — Motor Contactors (Main Motion)

```
  Q 6.0 ──► Contator Avanço/Recuo (C3) — Forward/Reverse axis
             SET by:    PB20 Network 1 (AN I 72.2, A F 1.6 → S Q 6.0)
             RESET by:  OB1[7] when F 1.6 = 1 (A F 1.6, R Q 6.0)
             Monitored: OB1[5] (Q 73.2 limit logic), OB1[9] T 7 ramp

  Q 6.1 ──► Contator Esquerda/Direita (C4) — Left/Right translation
             Part of limit logic: with I 5.0, I 5.1
             Also: A Q 6.1 → S F 2.7 in OB1[1]

  Q 6.2 ──► Contator Subir/Descer parcial (C5) — Up/Down partial
             Part of limit logic: with I 5.4, I 5.5
             Used by PB10/PB30 axis routing

  Q 6.3 ──► Contator Vertical completo (C6) — Up/Down full range
             Part of limit logic: with I 5.6, I 5.7

  Q 6.4 ──► Contator Rotação Mesa (C7) — Table rotation
             Used by PB10/PB50 axis routing
             Gated by table lock logic (Q 8.7)

  Q 6.5 ──► Contator Rotação Disco (C9) — Disk rotation
             Used by PB10 → PB52 axis routing

  Q 6.6 ──► Estrela (Star contactor)
             OB1[2]: A F 1.0 → timing with T 2 (80ms transition)

  Q 6.7 ──► Triângulo (Delta contactor)
             OB1[2]: AN T 2, A F 1.0 OR A Q 7.1 → = Q 6.7
```

### Q 7.x — Special Relays

```
  Q 7.0 ──► Disco Linha Contator (Disk line contactor RL2)
             OB1[2]: A F 1.0 → = Q 7.0

  Q 7.1 ──► Selector VFD / Estrela-Triângulo
             OB1[2]: A I 4.2, AN F 1.0 → S Q 7.1

  Q 7.2 ──► Laser Contator
             OB1[2]: AN I 4.7 → R F 3.7; complex toggle logic

  Q 7.3 ──► Bomba Óleo (Oil Pump for table hydraulics)
             FB18: A T 26 → = Q 7.3 (only runs while T 26 is active)
             T 26 = KT 120.2 = 12 seconds (time base 2 = 1s)
             ⚠ Pump runs for max 120 seconds per activation

  Q 7.4 ──► Relé Auxiliar Principal (Main power relay)
             OB1[6]: O I 2.0, ON I 2.0 → S Q 7.4 (always ON initially)
             OB1[6]: A T 4 → R Q 7.4 (timeout kills power)

  Q 7.5 ──► Motion Pulse Signal
             OB1[6]: A T 24 → = Q 7.5

  Q 7.6 ──► = F 1.4 (Inverter 1 sequence feedback)
  Q 7.7 ──► = F 1.3 (Inverter 2 sequence feedback)
```

### Q 8.x — Inverter Commands & Table Lock

```
  Q 8.0 ──► Inverter 1 Forward:  AN F 5.4, A F 1.4, A F 4.2 → = Q 8.0
  Q 8.1 ──► Inverter 1 Reverse:  AN F 5.4, A F 1.4, A F 4.3 → = Q 8.1
  Q 8.2 ──► Inverter 1 Speed:    (Q8.0 OR Q8.1) AND F 4.5 → = Q 8.2
  Q 8.3 ──► Inverter 2 Speed:    (Q8.4 OR Q8.5) AND F 4.5 → = Q 8.3
  Q 8.4 ──► Inverter 2 Forward:  AN F 5.4, A F 1.3, A F 4.2, A T 22
  Q 8.5 ──► Inverter 2 Reverse:  AN F 5.4, A F 1.3, A F 4.3, A T 22
  Q 8.6 ──► Motor Spindle:       A T 21 → = Q 8.6 (after T 21 ramp-up)
  Q 8.7 ──► Trava Mesa (Table Lock Solenoid) ⚠ SAFETY CRITICAL
             OB1[8]: A I 73.3 → S Q 8.7
             Complex release logic involving AN I 72.3, AN I 73.3,
             AN I 3.2, AN I 3.3, AN Q 6.4
```

### Q 72.x / Q 73.x — Status Outputs & Elbo Feedback

```
  Q 72.0 ──► = F 0.3 (Program Passagens status output)
  Q 72.1 ──► = F 0.1 (Program Translação status output)
  Q 72.2 ──► = F 0.1 (duplicate — also Translação)
  Q 72.4 ──► Complex Elbo disco signal (AN F 4.0, A I 5.2, ... OR A F 9.4)
  Q 72.5 ──► = I 5.3 (FC Recuo pass-through to external)
  Q 72.6 ──► = I 5.1 (FC Esquerda pass-through to external)
  Q 72.7 ──► = F 10.1 AND Q 7.3 (Table 1 Up status + pump on)

  Q 73.0 ──► OB1[1]: Complex OR chain (F 0.0 OR F 0.1 OR F 0.2 OR F 0.3
                      OR (F 0.4 AND F 1.2)) → = Q 73.0
  Q 73.1 ──► O Q 8.6, O F 1.0 → = Q 73.1 (Disk motor status)
  Q 73.2 ──► OB1[5]: Limit switch composite signal (see above)
             OB1[1]: Also A F 6.3, A F 1.2 → = Q 73.2 (F6.3 mode output)
  Q 73.3 ──► AN F 1.4 → = Q 73.3 (Inverter 1 NOT active → Elbo)
  Q 73.4 ──► A F 1.5, AN F 1.3 → = Q 73.4 (Inverter status)
  Q 73.5 ──► = F 10.2 AND Q 7.3 (Table 1 Down status + pump on)
  Q 73.6 ──► = F 10.3 AND Q 7.3 (Table 2 Up status + pump on)
  Q 73.7 ──► = F 10.4 AND Q 7.3 (Table 2 Down status + pump on)
```

---

## FLAGS (F) — DETAILED MAPPING

### F 0.x — Program Mode Flags (Mutually Exclusive)

```
  F 0.0  Program Manual mode          SET by I 2.0 + guards
  F 0.1  Program Translação           SET by I 2.1 + guards
  F 0.2  Program Pass + Translação    SET by I 2.2 + guards
  F 0.3  Program Passagens            SET by I 2.3 + guards
  F 0.4  Spianatura (leveling) mode   SET by I 73.2 + guards
  F 0.5  Toggle Clock — T 13 output   = T 13 (0.5s pulse timer)
  F 0.6  Toggle State Latch           SET when AN F 0.5 AND A F 1.2
                                       RESET when AN F 0.5 AND AN F 1.2
  F 0.7  General Enable               = complex OR logic in OB1[4]:
                                       (Q7.1 AND Q8.6 AND T3 AND I73.7)
                                       OR (F1.0 AND Q6.7)
```

### F 1.x — System State Flags

```
  F 1.0  Disk motor running flag       SET/RESET in OB1[2] disk sequence
  F 1.1  Disk startup sequence         SET by I 4.0 conditions
  F 1.2  Toggle Output (Flip-Flop)     SET by A F 0.5, AN F 0.6
                                        RESET by A F 0.5, A F 0.6
  F 1.3  Inverter 2 sequence           Complex logic in OB1[7]
  F 1.4  Inverter 1 sequence           Complex logic in OB1[7]
  F 1.5  Inverter 2 active             = AN F 1.3, AN F 1.4 → S F 1.5
  F 1.6  System Ready / Stop Gate      = complex logic in OB1[7]
                                        When F 1.6 = 1 → R Q6.0 thru Q6.5
  F 1.7  Motion cutoff condition        OB1[7]: complex OR chain → = F 1.7
```

### F 2.x — Speed Control Flags

```
  F 2.0  Speed adjust active (passages)  OB1[10]: (F3.5 OR F3.6) AND
                                                    (F0.2 OR F0.3) AND I 2.7
  F 2.1  Speed adjust active (tilt)      OB1[10]: (F3.5 OR F3.6) AND F 0.4
  F 2.2  Speed adjust general            OB1[10]: AN F 2.0, AN F 2.1,
                                                    AND (F3.5 OR F3.6)
  F 2.3  Speed+ button latch             PB9: = I 4.5
  F 2.4  Speed- button latch             PB9: = I 4.6
  F 2.5  Below max flag (FB10)           FB10: <F comparison result
  F 2.7  Translation interlock           OB1[1]: A Q 6.1 → S F 2.7
                                                  A Q 6.0, (Q8.0 OR Q8.4) → R F 2.7
```

### F 3.x — Mode & Speed Flags

```
  F 3.0  Tilt mode flag                  OB1[6]: A I 4.3, AN F 5.0 → S
  F 3.1  Vertical mode flag              OB1[6]: AN I 4.3, AN F 5.0 → S
  F 3.2  Fast mode active (Elbo)         PB20: A I 72.2, A I 72.5 → S
  F 3.3  Medium mode fallback            PB20: A F 4.0, A Q 6.0 → S
  F 3.4  Medium mode override            PB20: AN F 3.2, A F 3.3 → S
  F 3.5  Speed+ flag (= I 4.5, AN I 4.6) OB1[10], FB15
  F 3.6  Speed- flag (= I 4.6, AN I 4.5) OB1[10], FB15
  F 3.7  Laser ON flag                   OB1[2]: toggle logic with I 4.7
```

### F 4.x — Direction & Phase Flags

```
  F 4.0  Forward direction active         PB20: OR-before logic → = F 4.0
  F 4.1  Reverse direction active         PB20: OR-before logic → = F 4.1
  F 4.2  Phase 1 complete (delayed F4.0)  OB1[9]: A F 4.0, A T 9 → = F 4.2
  F 4.3  Phase 2 complete (delayed F4.1)  OB1[9]: A F 4.1, A T 10 → = F 4.3
  F 4.5  Movement enable flag              PB20: OR-before self-latch → = F 4.5
  F 4.6  At max flag (FB12)               FB12: <=F comparison result
```

### F 5.x — Motion Status Flags

```
  F 5.0  ANY contactor active             OB1[7]: O Q6.0 thru Q6.5 → = F 5.0
  F 5.1  Program sequence started         OB1[8]: A F 5.3 → S F 5.1
  F 5.2  Manual override (Auto mode)      OB1[8]: A F 0.0 → S F 5.2
  F 5.3  In programmed mode               OB1[8]: O F 0.1, O F 0.2, O F 0.3
  F 5.4  General fault / sequence ctrl     OB1[8]: complex OR chain → = F 5.4
  F 5.7  Doubling active flag (FB12/13)   Counter-based step doubling logic
```

### F 6.x — Axis Selection & Mode Flags

```
  F 6.0  Axis Fwd/Rev selected (PB10)    PB10: = (I72.2 AND NOT F5.0)
                                                   OR Q6.2 OR Q6.3
  F 6.1  Axis Left/Right selected (PB10) PB10: = (NOT F5.0 AND I72.1) OR Q6.1
  F 6.2  Axis Rot Table selected (PB10)  PB10: = complex logic with I72.3, I72.0
  F 6.3  Speed Edit Mode (Fast Mode)     ⚠ SET by triple-press of I 3.7 within 2s
                                          Enables FB15 and PB9 execution
  F 6.4  Axis Fwd/Rev active (PB11)      PB11: = Q6.0 OR (I2.6 OR I2.7) manual
  F 6.5  Axis Up/Down active (PB11)      PB11: = Q6.2/Q6.3 OR (I3.0 OR I3.1)
  F 6.6  Axis Left/Right active (PB11)   PB11: = Q6.1 OR (I2.4 OR I2.5) manual
  F 6.7  Axis Rot Table active (PB11)    PB11: = Q6.4 OR (I3.2 OR I72.3 OR I3.3)
```

### F 7.x — FB15 Fast Mode Axis Selection (Mutex Flags)

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │  F 7.0-7.5 are MUTUALLY EXCLUSIVE axis selection flags within FB15.│
  │  They determine which axis is being speed-edited in fast mode.     │
  │  Only ONE can be active at a time.                                 │
  └──────────────────────────────────────────────────────────────────────┘

  F 7.0  Fwd/Rev axis (I 2.6 OR I 2.7)     → FB15 Network 2 → DW 0
  F 7.1  Left/Right axis (I 2.4 OR I 2.5)  → FB15 Network 3 → DW 3,4,11
  F 7.2  Up/Down axis (I 3.0 OR I 3.1)     → FB15 Network 4 → DW 5,6,7,8
  F 7.3  (Not used in current code — reserved)
  F 7.4  Rot Table axis (I 3.2 OR I 3.3)   → FB15 Network 5 → DW 12,13,14
  F 7.5  Rot Disk axis (I 3.4 OR I 3.5)    → FB15 Network 6 → DW 15,16,17

  F 7.6  Fast mode toggle (edge detection)
  F 7.7  Fast mode active indicator
```

### F 8.x — FB15 Speed Level Selection

```
  F 8.0  Speed level 1 active (within FB15 axis networks)
  F 8.1  Speed level 2 active
  F 8.2  Speed level 3 active
  F 8.7  Overflow/error flag (from FB243 DIV:16)
```

### F 9.x, F 10.x, F 11.x — Position & Table Flags

```
  F 9.4   Spianatura speed condition     OB1[5]: A F 0.4, A I 72.0,
                                                   A I 72.5, A T 19
  F 10.0  Position changed flag           FB18: IW8 masked ><F FW10 masked
  F 10.1  Table 1 Up action               FB18: A I 9.1, A I 9.0, AN I 9.2
  F 10.2  Table 1 Down action             FB18: A I 9.2, A I 9.0, AN I 9.1
  F 10.3  Table 2 Up action               FB18: A I 9.3, AN I 9.0, AN I 9.4
  F 10.4  Table 2 Down action             FB18: A I 9.4, AN I 9.0, AN I 9.3
  F 11.0  = I 9.0 (direct latch)
  F 11.1  = I 9.1 (direct latch)
  F 11.2  = I 9.2 (direct latch)
  F 11.3  = I 9.3 (direct latch)
  F 11.4  = I 9.4 (direct latch)
```

---

## TIMERS (T) — COMPLETE LISTING

```
  ┌────────┬────────────┬──────────────────────────────────────────────────┐
  │ Timer  │ Value      │ Purpose                                         │
  ├────────┼────────────┼──────────────────────────────────────────────────┤
  │ T 0    │ KT 060.2   │ Disk startup cooldown (60 × 1s = 60s)          │
  │ T 1    │ KT 600.1   │ Programmed sequence timeout (600 × 0.1s = 60s) │
  │ T 2    │ KT 080.1   │ Star-Delta transition (80 × 0.1s = 8s)         │
  │ T 3    │ KT 400.1   │ VFD ramp timer (400 × 0.1s = 40s)             │
  │ T 4    │ KT 030.2   │ Power timeout (30 × 1s = 30s)                  │
  │ T 5    │ KT 050.1   │ Inverter 1 pulse (50 × 0.1s = 5s)             │
  │ T 6    │ KT 050.1   │ Inverter 2 pulse (50 × 0.1s = 5s)             │
  │ T 7    │ KT 050.0   │ Motion ramp (50 × 0.01s = 0.5s)               │
  │ T 8    │ KT 020.1   │ Table lock debounce (20 × 0.1s = 2s)          │
  │ T 9    │ KT 010.0   │ Phase 1 delay (10 × 0.01s = 0.1s)             │
  │ T 10   │ KT 010.0   │ Phase 2 delay (10 × 0.01s = 0.1s)             │
  │ T 11   │ KT 015.1   │ Stop motion delay (15 × 0.1s = 1.5s)          │
  │ T 12   │ FW 24      │ Forward ramp (variable, loaded from FW 24)     │
  │ T 13   │ KT 005.1   │ Toggle clock (5 × 0.1s = 0.5s)                │
  │ T 14   │ KT 300.1   │ Laser auto-off (300 × 0.1s = 30s)             │
  │ T 15   │ KT 300.2   │ Laser hard limit (300 × 1s = 300s = 5 min)    │
  │ T 16   │ FW 22      │ Reverse ramp (variable, loaded from FW 22)     │
  │ T 17   │ KT 020.1   │ FastCmd window (20 × 0.1s = 2s)               │
  │ T 18   │ KT 030.1   │ Speed adjust hold (30 × 0.1s = 3s)            │
  │ T 19   │ FW 26      │ Spianatura timer (variable, from FW 26)        │
  │ T 20   │ KT 020.1   │ VFD stabilization (20 × 0.1s = 2s)            │
  │ T 21   │ KT 020.1   │ Disk startup ramp (20 × 0.1s = 2s)            │
  │ T 22   │ KT 050.1   │ Inverter 2 phase timing (50 × 0.1s = 5s)      │
  │ T 24   │ KT 020.1   │ Motion pulse (20 × 0.1s = 2s)                 │
  │ T 25   │ KT 180.3   │ Hydraulic limit (180 × 10s = 1800s = 30 min)  │
  │ T 26   │ KT 120.2   │ Oil pump safety (120 × 1s = 120s = 2 min)     │
  └────────┴────────────┴──────────────────────────────────────────────────┘

  S5 Timer Time Base Reference:
  ┌──────────┬─────────────┐
  │ Base (y) │ Resolution  │
  ├──────────┼─────────────┤
  │ 0        │ 0.01 s      │
  │ 1        │ 0.1  s      │
  │ 2        │ 1.0  s      │
  │ 3        │ 10.0 s      │
  └──────────┴─────────────┘
  Format: KT xxx.y where xxx = count, y = time base
  Timer value = xxx × resolution
```

---

## DATA BLOCK 10 (DB10) — SPEED PRESETS & PARAMETERS

### Complete DW Listing with Hex Values

```
  ┌──────┬────────────┬────────────┬─────────────────────────────────────────┐
  │ DW   │ Hex Value  │ Decimal    │ Usage / Description                    │
  ├──────┼────────────┼────────────┼─────────────────────────────────────────┤
  │  0   │ 0x25BF     │ 9663       │ Speed: Fwd/Rev base (FB15 Net 2)      │
  │  1   │ 0x047E     │ 1150       │ Speed: PB20 level 1 / QW64 output     │
  │  2   │ 0x0B38     │ 2872       │ Speed: PB20 level 2 / FB14 M002       │
  │  3   │ 0x19FE     │ 6654       │ Speed: Left/Right level 1 (FB15 N3)   │
  │  4   │ 0x0C90     │ 3216       │ Speed: Left/Right level 2 (FB15 N3)   │
  │  5   │ 0x0357     │ 855        │ Speed: Up/Down level 1 (FB15 N4/M003) │
  │  6   │ 0x382F     │ 14383      │ Speed: Up/Down level 1 (FB15 N4)      │
  │  7   │ 0x1600     │ 5632       │ Speed: Up/Down level 2 (FB15 N4)      │
  │  8   │ 0x04BE     │ 1214       │ Speed: Up/Down level 3 (FB15 N4)      │
  │  9   │ 0x2B58     │ 11096      │ (parameter)                           │
  │ 10   │ 0x0125     │ 293        │ (parameter)                           │
  │ 11   │ 0x00A0     │ 160        │ Speed: Left/Right level 3 (FB15 N3)   │
  │ 12   │ 0x0FBD     │ 4029       │ Speed: Rot Table level 1 (FB15 N5)    │
  │ 13   │ 0x027A     │ 634        │ Speed: Rot Table level 2 (FB15 N5)    │
  │ 14   │ 0x0053     │ 83         │ Speed: Rot Table level 3 (FB15 N5)    │
  │ 15   │ 0x4000     │ 16384      │ Speed: Rot Disk level 1 (FB15 N6)     │
  │ 16   │ 0x1600     │ 5632       │ Speed: Rot Disk level 2 (FB15 N6)     │
  │ 17   │ 0x0000     │ 0          │ Speed: Rot Disk level 3 (FB15 N6)     │
  │ 18   │ 0x4000     │ 16384      │ Speed: Spianatura (FB14 M001)         │
  │ 19   │ 0x0000     │ 0          │ (unused)                              │
  │ 20   │ 0x101E     │ 4126       │ (parameter)                           │
  │ 21   │ 0x0005     │ 5          │ (parameter)                           │
  │ 22   │ 0x0000     │ 0          │ (parameter)                           │
  │ 23   │ 0x1520     │ 5408       │ (parameter)                           │
  │ 24   │ 0x1000     │ 4096       │ (parameter)                           │
  │ 25   │ 0x0008     │ 8          │ FB10: Step increment value            │
  │ 26   │ 0x0004     │ 4          │ FB10: Min step divider                │
  │ 27   │ 0x0001     │ 1          │ FB12/FB13: Min step size (POT.+/-)    │
  │ 28   │ 0x4000     │ 16384      │ FB10/FB12: Max limit value            │
  │ 29   │ 0x0000     │ 0          │ (parameter)                           │
  │ 30   │ 0x0000     │ 0          │ (parameter)                           │
  │ 31   │ 0x000A     │ 10         │ FB10: Initial step size               │
  │ 32   │ 0x7FD0     │ 32720      │ FB11: Max comparison limit            │
  │ 33   │ 0x03E7     │ 999        │ FB11: Parameter                       │
  │ 34   │ 0x3200     │ 12800      │ FB11: Spianatura reference            │
  │ 35   │ 0x2000     │ 8192       │ (parameter)                           │
  │ 36   │ 0x3000     │ 12288      │ (parameter)                           │
  │ 37   │ 0x07D0     │ 2000       │ (parameter)                           │
  │ 38   │ 0x0000     │ 0          │ (parameter)                           │
  │ 39   │ 0x0003     │ 3          │ Scale: Left/Right axis (FB15 N3)      │
  │ 40   │ 0x001C     │ 28         │ Scale: Up/Down axis (FB15 N4)         │
  │ 41   │ 0x0010     │ 16         │ Scale: Spianatura (FB14 M001)         │
  │ 42   │ 0x0001     │ 1          │ Scale: Rot Disk axis (FB15 N6)        │
  │ 43   │ 0x0000     │ 0          │ (parameter)                           │
  │ 44   │ 0x001F     │ 31         │ FB18: Position mask (AW mask for IW8) │
  └──────┴────────────┴────────────┴─────────────────────────────────────────┘
```

### FLAG WORDS (FW) — Working Registers

```
  FW 10  Last position value (FB18 comparison)
  FW 22  Reverse ramp time (loaded into T 16)
  FW 24  Forward ramp time (loaded into T 12)
  FW 26  Spianatura timer value (loaded into T 19)
  FW 32  Current step size (FB10/FB12/FB13)
  FW 34  Current potentiometer/speed value (working register)
  FW 36  Remainder from FB16 SCALAR division (Z4 output)
  FW 38  Scale factor for FB16 SCALAR (Z2 input)
  FW 50  Current position (masked) from FB18
```

### INPUT/OUTPUT WORDS

```
  IW 8   CNC/encoder position feedback (read by FB18)
  QW 64  VFD speed reference — Elbo-controlled axes (PB20)
  QW 66  VFD speed reference — Manual/potentiometer axes (FB14/FB15/FB16)
```

---

## COUNTERS (C) — COMPLETE LISTING

```
  C 1  FastCmd triple-press counter    OB1[1]: Loaded with KC 003,
                                       decremented by I 3.7 presses,
                                       when = 0 within T 17 → S F 6.3
  C 3  Potentiometer step doubling     FB12/FB13: Loaded with KC 002,
                                       decremented by F 1.2, controls
                                       when step size doubles
```

---

*End of Signal Reference*

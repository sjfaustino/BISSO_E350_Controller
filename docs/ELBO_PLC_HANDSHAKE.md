# Elbo ⇄ PLC Handshake (Bisso E350 / Siemens S5)

**Machine:** Bisso E350 bridge saw  
**PLC:** Siemens S5 (STEP5)  
**External motion controller:** Modular Elbo positioning controller (multi-board)  
**Date:** 2025-11-03  
**Doc version:** 1.0

---

## 1. System Role Split

- **Elbo** = motion sequencer. It decides *what move to perform next* (which axis, which direction, which speed, how far).
- **PLC** = power + safety. It decides *whether that move is allowed right now* and actually energizes the contactor and direction relays for the motors, plus coolant / hydraulics.

Encoders from the machine axes go to the Elbo, not the PLC.  
The Elbo watches encoder distance and ends the move when the target is reached.

The PLC does not do position control.  
It just arbitrates and supervises.

---

## 2. Signal Map

### 2.1 Elbo → PLC (PLC reads these on inputs I72.x / I73.x)

| PLC Addr | Typical Symbol           | Meaning / Purpose                                                     |
|:---------|:-------------------------|:----------------------------------------------------------------------|
| I 72.0   | Elbo_FAST                | Request fast feed preset                                              |
| I 72.1   | Elbo_MED                 | Request medium feed preset                                            |
| I 73.7   | Elbo_SLOW                | Request slow / fine feed                                              |
| I 72.2   | Elbo_RUN                 | Motion block active / program running                                 |
| I 72.3   | Elbo_LOCK                | Request table unlock / clamp release sequence                         |
| I 72.4   | Elbo_RESET               | Request PLC reset / handshake resync                                  |
| I 73.0   | Elbo_Yreq                | Move feed axis (Y / avanço)                                           |
| I 73.1   | Elbo_Xreq                | Move translation axis (X / translação)                                |
| I 73.2   | Elbo_Zreq                | Move vertical axis (Z / subida-descida do disco)                      |
| I 73.3   | Elbo_Treq                | Rotate/position the table                                             |
| I 73.5   | Elbo_DIR_PLUS            | Direction "+" (forward / up / CW depending on which axis is active)   |
| I 73.6   | Elbo_DIR_MINUS           | Direction "−" (back / down / CCW)                                     |

The Elbo asserts exactly ONE axis request bit at a time (e.g. I73.0 or I73.1 or I73.2...).  
Direction is encoded separately using I73.5 / I73.6.  
Speed class (fast / med / slow) is also encoded separately using I72.0 / I72.1 / I73.7.

---

### 2.2 PLC → Elbo (PLC writes these on Q8.x etc.)

| PLC Addr | Typical Symbol  | Meaning / Purpose                                                             |
|:---------|:----------------|:------------------------------------------------------------------------------|
| Q 8.0    | PLC_READY       | "System enabled / you may command moves"                                     |
| Q 8.1    | PLC_ACK         | Short acknowledge pulse when PLC accepts a requested move                    |
| Q 8.2    | PLC_INHIBIT     | Safety inhibit / stop. When ON, PLC refuses motion or forces stop            |

These signals tell the Elbo if it's allowed to proceed and confirm that the PLC actually acted on a given motion request.

---

### 2.3 PLC physical drive outputs (to the machine, not back to Elbo)

| PLC Addr | Typical Function                            | Purpose                                                                 |
|:---------|:--------------------------------------------|:------------------------------------------------------------------------|
| Q 6.0    | Contactor: Feed axis motor (Y / avanço)      | Routes the single shared VFD to that axis motor                         |
| Q 6.1    | Contactor: Translation axis motor (X)        | "                                                                     " |
| Q 6.2    | Contactor: Vertical axis motor (Z)           | "                                                                     " |
| Q 6.3    | Contactor: Tilt / Head rotation              | "                                                                     " |
| Q 6.4    | Contactor: Table rotation                    | "                                                                     " |
| Q 6.5    | Contactor: Disk/head rotation axis           | "                                                                     " |
| Q 8.3+   | Direction forward / CW command to drive      | Sets drive direction / RUN FWD input                                   |
| Q 8.3−   | Direction reverse / CCW command to drive     | Sets drive reverse                                                      |
| QW 64    | Analog feed reference for the shared VFD     | PLC-selected feedrate, chosen from DB10 presets + POT scaling          |
| Q 8.2    | Hydraulics / coolant enable (in many builds) | Coolant and pump management linked to motion or forced during surfacing|

> OB1 enforces that **only one** of Q6.0..Q6.5 can be active at a time, so only one motor is ever driven by the VFD in a given scan.

---

## 3. High-Level Handshake Cycle

Below is the normal cycle for one motion block sent from Elbo to PLC and executed on the saw.

### Step 1. Idle / Armed
1. PLC holds `Q 8.0 = 1` (PLC_READY high).  
2. All Elbo request bits are low (`I 73.x = 0`, `I 72.2 = 0`).  
3. No axis contactors are energized (`Q 6.x = 0`).

Elbo interprets this as: "System ready. I can ask for the next move."

---

### Step 2. Elbo Requests Motion
Elbo:
1. Raises one axis bit, e.g. `I 73.0 = 1` (Y axis feed).  
2. Sets the direction bit (`I 73.5` for + or `I 73.6` for −).  
3. Sets speed class bits (e.g. `I 72.0 = 1` for fast).  
4. Raises `I 72.2 = 1` (RUN = active block).  

To the PLC this means:  
> "I want you to move THIS axis, in THIS direction, at THIS speed, NOW."

---

### Step 3. PLC Accepts and Starts Motion
PLC (OB1 → PB10 → PB20/PB30/...):
1. Checks safety: global inhibit bit, limit switches, table/clamp state, Z-up interlock if implemented.  
2. Clears all Q6.x contactors (this happens every scan).  
3. Energizes the correct contactor for that axis (e.g. `Q 6.0 = 1` for Y feed).  
4. Sets the motor direction relay outputs (`Q 8.x` forward/reverse).  
5. Loads the proper feedrate preset from DB10 and writes it to `QW 64` (analog speed reference to the VFD).  
6. Briefly pulses `Q 8.1 = 1` (PLC_ACK).  

That `PLC_ACK` pulse is how the Elbo knows "the PLC actually accepted this block and powered the axis."

During this phase coolant / hydraulics may also be energized, and table clamp may be released (PB50/PB51 logic) if the requested axis is table rotation.

---

### Step 4. Motion In Progress
- Elbo keeps `I 73.0` (axis request) high.  
- PLC keeps the matching `Q 6.x` contactor energized and maintains direction.  
- Elbo uses encoder feedback on that axis to measure travel.  

The PLC does **not** measure distance. It just keeps supplying power.

---

### Step 5. Motion Complete
When the Elbo reaches the programmed travel distance:  
1. Elbo drops the axis request bit (`I 73.0 = 0`) and drops RUN (`I 72.2 = 0`).  
2. PLC sees that request bit is now 0.  
3. PLC drops `Q 6.x` for that axis (cut power to that motor).  
4. PLC leaves `Q 8.0 = 1` (still READY) so the Elbo can immediately issue the next block.  

This is how the Elbo chains together blocks like:  
- Move Y forward 400 mm  
- Lift Z up 30 mm  
- Translate X right 600 mm  
…to execute an entire cutting cycle.

---

### Step 6. Fault / Reset Path
If Elbo can't continue (encoder fail, travel exceeded, axis jam, etc.), it will assert a reset/handshake line (often `I 72.4`).  
In that case the PLC should:  
- Drop `Q 8.0` (READY) for a short time, then raise it again after things are idle.  
- Optionally set its internal inhibit flag (the F5.x "no motion" bit) so PB10 refuses any new motion until the operator clears the fault.

---

## 4. Timing Diagram (conceptual)

```text
Elbo_I73.0 (AxisReq Y)  ──────┐█████████████████████████████████┐────────────
                             │<-- Axis running / encoder move ->│
Elbo_I72.2 (RUN)       ──────┐█████████████████████████████████┐────────────
                             │                                 │
PLC_Q8.1 (ACK pulse)   ───┐███┐
                          │ACK│
PLC_Q6.0 (Y Contactor) ───┐████████████████████████████████████┐────────────
                          │ axis motor powered                 │
PLC_Q8.0 (READY)     ─────████████████████████████████████████████████──────
```

Legend:  
- `Elbo_I73.0` = Elbo asks PLC to move Y axis.  
- `Elbo_I72.2` = Elbo says "program RUN / motion active."  
- `PLC_Q8.1`  = PLC acknowledge pulse right after it energizes the axis.  
- `PLC_Q6.0`  = PLC energizes the Y contactor so the shared VFD is routed to that axis.  
- `PLC_Q8.0`  = PLC READY stays high the whole time unless there's a fault.

**How It Works:**
```
COMPLETE MOTION SEQUENCE STATE MACHINE:
┌────────────────────────────────────────────────────────────────────────────┐
│  STATE 0: IDLE                                                              │
│   └─► ESP32 waits for PLC_READY (Q8.0 = HIGH)                              │
│                                                                             │
│  STATE 1: REQUEST                                                           │
│   └─► ESP32 asserts: Axis + Direction + Speed + RUN                        │
│                                                                             │
│  STATE 2: ACK WAIT                                                          │
│   └─► Wait for PLC_ACK pulse (Q8.1) - confirms contactor energized         │
│   └─► Timeout after 500ms → FAULT                                          │
│                                                                             │
│  STATE 3: MOVING                                                            │
│   └─► Monitor encoder position via WJ66                                    │
│   └─► Check PLC_INHIBIT (Q8.2) for safety stops                            │
│                                                                             │
│  STATE 4: COMPLETE                                                          │
│   └─► Target reached → Drop Axis + RUN bits                                │
│   └─► PLC drops contactor → Return to STATE 0                              │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Safety Notes

1. The PLC is trusting the Elbo to sequence moves correctly (lift Z, then move X).  
   In your machine, stock code does **not** enforce "Z up before X/Y" — you should add that in PB20/PB40/PB21/PB41.

2. If two axis request bits go high at once (e.g. I73.0 and I73.1), PB10 must choose exactly one.  
   Never allow two `Q 6.x` to energize at the same time — a shared VFD cannot drive two motors simultaneously.

3. Coolant and hydraulics should follow RUN (I72.2) or an internal "movement active" flag, not just blade ON.

4. Before table rotation (I73.3), PB50/PB51 must release the table clamp, wait using T8, then energize `Q 6.4`. After motion, it must re-clamp.

---

## 6. Emulating / Replacing the Elbo

You can replace the Elbo with an ESP32 or other controller **without changing the PLC program**, by doing this:

1. Drive the same I72.x / I73.x request lines into the PLC:  
   - Pick axis (I73.0 / I73.1 / I73.2 / I73.3)  
   - Pick direction (I73.5 / I73.6)  
   - Pick feed class (I72.0 / I72.1 / I73.7)  
   - Assert RUN (I72.2)

2. Watch PLC outputs Q8.0 (READY) and Q8.1 (ACK) as digital inputs on your side.  
   - When you see ACK and the right Q6.x contactor energize, you know the PLC accepted.

3. Once your encoder feedback says "distance reached", drop the axis bit (e.g. clear I73.0) and clear RUN (I72.2).  
   - The PLC will then drop Q6.x and wait for your next move request.

This lets you phase out the aging multi-board Elbo hardware while leaving the Siemens S5 logic and all its safety interlocks intact.

---

**Generated:** 2025-11-03  
**Prepared for:** Bisso E350 PLC retrofit / diagnostic documentation  

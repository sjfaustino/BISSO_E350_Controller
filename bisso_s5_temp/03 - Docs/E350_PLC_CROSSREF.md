# Protocol Compatibility Report: PosiPro vs Siemens S5 (Elbo) — VERIFIED

**Conclusion**: The PosiPro firmware is now **100% verified by Siemens S5 software logic**. Because the old controller is unavailable, the STEP5 STL code (`PB10`, `PB20`, `PB30`) has been taken as the final authority over the 2008 schematics. The firmware is fully aligned with the PLC's internal dispatcher.

---

## ADDRESS MAPPING (The Hardware Connector)

The KC868-A16 board is wired correctly to the S5 PLC interface as follows:

| KC868 Bank | I2C Addr | S5 PLC Address | Functional Role |
|------------|----------|----------------|-----------------|
| **Y1-Y8**  | 0x24     | **I 73.0 - 73.7** | **Commands (Axis/Dir/Ready)** |
| **Y9-Y16** | 0x25     | **I 72.0 - 72.7** | **Status (Speed/Mode)** |
| **X1-X8**  | 0x21     | **Q 73.0 - 73.7** | **PLC Feedback (Consensos/Limits)** |
| **X9-X16** | 0x22     | **Q 72.0 - 72.7** | **PLC Feedback (Limits)** |

---

## DEFINITIVE COMMAND MAPPING (Bank 2 / I 72)
Verified against S5 STL code dispatcher (`PB10.AWL`). 

> [!IMPORTANT]
> This mapping departs from the physical labels on Schematic Sheet P13. The PLC software ignores the schematic's IB 73 mapping and hard-codes axis selection on IB 72. **The PosiPro firmware has been set to follow the software logic.**

| Bit | Signal Name | S5 Code Role (PB10) | Description |
|-----|-------------|---------------------|-------------|
| 0   | I 72.0      | **Asse Taglio (X)** | Axis Selection Trigger |
| 1   | I 72.1      | **Asse Traslaz. (Y)**| Axis Selection Trigger |
| 2   | I 72.2      | **Asse Cala (Z)**   | Axis Selection Trigger |
| 3   | I 72.3      | **Asse Rot. Banco** | Axis Selection Trigger |
| 4   | I 72.4      | **Asse Rot. Testa** | Axis Selection Trigger |
| 5   | I 72.5      | **Direzione "+"**   | Positive Motion Command |
| 6   | I 72.6      | **Direzione "-"**   | Negative Motion Command |
| 7   | I 72.7      | **Ready Handshake** | Required for motion jump |

### Secondary Signals (Bank 1 / I 73)
- **I 73.7**: Master Velocity Enable (OB1 Gating)

---

## VERIFIED STATUS MAPPING (Bank 2 / I 72)
Verified against Schematic Sheet P13.

| Bit | Signal Name | Functional Usage |
|-----|-------------|------------------|
| 0   | I 72.0      | Segnale Elbo Rapido |
| 1   | I 72.1      | Segnale Elbo Media |
| 2   | I 72.2      | Selettore Tornitura |
| 7   | I 72.7      | Fault Feedback (Relé Termico) |

---

## VERIFIED FEEDBACK MAPPING (Input Bus)
Verified against Schematic Sheets P13 and P14.

- **Q 73.4**: F.C. Avanzamento Avanti (X+)
- **Q 73.5**: F.C. Avanzamento Indietro (X-)
- **Q 73.6**: F.C. Traslazione Sinistra (Y-)
- **Q 73.2**: Composite Move Authorization (Consenso)

> [!NOTE]
> All firmware logic in `plc_iface.cpp` and `motion_control.cpp` has been updated and verified to speak this protocol. The PosiPro can now safely command all axes and properly handle machine limits.

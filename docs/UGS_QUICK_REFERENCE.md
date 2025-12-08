# UGS Quick Reference Card - BISSO E350 Controller

**Print this page for easy reference at your machine**

---

## Connection Settings
- **Port:** (Your COM port) _______
- **Baud:** 115200
- **Firmware:** GRBL

---

## Essential Commands

### Status & Information
```gcode
?           # Query status
$$          # View all settings
$G          # View parser state
$#          # View work coordinates
```

### Set Work Zero
```gcode
G10 L20 P1 X0 Y0 Z0    # Set G54 zero at current position
G54                    # Select G54 coordinate system
```

### Motion Commands
```gcode
G90         # Absolute mode
G91         # Relative mode
G0 X10 Y20  # Rapid move to position
G1 X50 F500 # Linear move at 500mm/min
```

### Jogging (While Idle)
```gcode
$J=G91 X5 F100      # Jog X +5mm
$J=G91 X-5 F100     # Jog X -5mm
$J=G91 Y5 F100      # Jog Y +5mm
$J=G91 Z5 F100      # Jog Z +5mm
```

### Real-Time Commands (Interrupt Anytime)
```
?           # Status query (send any time)
!           # Feed hold (pause immediately)
~           # Cycle start (resume from hold)
Ctrl+X      # Soft reset
```

### Emergency
```gcode
!           # Feed hold
Ctrl+X      # Soft reset
$X          # Unlock after alarm
```

---

## UGS Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Ctrl+C` | Connect |
| `Ctrl+D` | Disconnect |
| `Alt+S` | Send file |
| `Space` | Pause/Resume |
| `Ctrl+X` | Soft reset |

---

## Work Coordinates

### Multiple Setups
```gcode
G54    # Work coordinate 1
G55    # Work coordinate 2
G56    # Work coordinate 3
G57    # Work coordinate 4
G58    # Work coordinate 5
G59    # Work coordinate 6
```

### Set Each One
```gcode
G10 L20 P1 X0 Y0 Z0    # Set G54
G10 L20 P2 X0 Y0 Z0    # Set G55
# ... P3-P6 for G56-G59
```

---

## Error Codes

| Code | Meaning | Action |
|------|---------|--------|
| error:1 | Unknown command | Check syntax |
| error:3 | Invalid statement | Check parameters |
| error:8 | Not idle | Wait for motion to stop |
| error:20 | Unsupported G-code | Command not implemented |

---

## Safety Reminders

### Before Starting:
- [ ] Water flowing (MANUAL)
- [ ] Blade guard closed
- [ ] Material secured
- [ ] Emergency stop tested

### Manual Controls:
- ⚠️ **Spindle:** MANUAL control
- ⚠️ **Water:** MANUAL control
- ✅ **Motion:** UGS controls

---

## Troubleshooting

**No Connection:**
- Check COM port
- Verify 115200 baud
- Try different USB cable

**ALARM State:**
- Send: `$X` to unlock
- Check emergency stop
- Verify soft limits

**Position Wrong:**
- Check encoder cables
- Verify work zero set
- Run: `G10 L20 P1 X0 Y0 Z0`

---

## Support

**CLI Access:** Connect serial terminal at 115200
**Diagnostics:** Type `debug all` in CLI
**Fault Log:** Type `faults show` in CLI

---

**For detailed guide:** See `UGS_SETUP_GUIDE.md`
**Test file:** `BISSO_E350_Test.gcode`
**Validation:** `VALIDATION_CHECKLIST.md`

# CLI Reference
**Baud:** 115200 (USB) • **WJ66:** 9600 (Serial2)

- `help` — list commands
- `ver` — firmware + schema
- `cfg show|export|import`
- `set cal <ch> <gain> <offset>` — gain 0.1..10.0, offset -5..5
- `jtail [n]` — tail journal
- `flush` — force flush
- `i2cscan` — scan I²C
- `cal` — LCD calibration mode
- `selftest` — relay walking with timeout

**G-code:** single-axis `G0`/`G1` with optional `F`. Requires AUTO high.

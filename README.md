# BISSO E350 Controller — Modular Dev Build (R3)

- I²C lock guards LCD/IO to avoid collisions.
- Motion asserts Y_VS during active move; clears on complete/error.
- WJ66 parser updated for `01,+000123,+000456,+000789,+001234` ASCII protocol @ 9600 baud.
- CLI now at **115200** baud for faster streaming.
- Stall detection: no encoder delta for 300 ms -> `ALARM STALL`.
- `onSystemError()` callback keeps modules decoupled from global state.
- Soft-limits enforced at enqueue and during motion.

See `/docs/` for developer details.

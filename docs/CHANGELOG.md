# CHANGELOG (R3)
- Fix: Batched outputs with I²C lock to prevent collisions with LCD.
- Fix: Start signal `Y_VS` driven during motion.
- Fix: Direction interlock warning + safe retry; interlock verified periodically.
- Update: WJ66 parser for 5-field comma protocol.
- Perf: CLI at 115200; WJ66 remains 9600 on Serial2.
- Add: Stall detection (300 ms encoder no-change).
- Design: motion.cpp no longer mutates `state` directly; uses `onSystemError()`.

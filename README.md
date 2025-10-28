# BISSO E350 Controller Firmware (PlatformIO, Modular)

**Version:** v0.4.9-Stable (R2)  
**Target:** ESP32 + KC868-A16  
**Framework:** Arduino (PlatformIO)

---

## Executive Summary
Modular firmware for a stone saw controller integrating motion queuing, direction interlock with automatic retry,
WJ66 encoder feedback, LCD 20×4 diagnostics, SPIFFS journaling with rotation, and a developer-oriented CLI
supporting G-code (single-axis per command) and configuration import/export.

---

## Quick Start (PlatformIO)
```bash
git clone <your repo>
cd BISSO_E350_Controller
pio run --target upload
pio device monitor
# optionally preload SPIFFS defaults
pio run --target uploadfs
```

---

## Features (High-Level)
- Motion engine with interlock retry + soft-limit enforcement
- WJ66 robust ASCII parser with stale watchdog
- 20×4 LCD UI: RUN / ERROR / CALIB / SELF_TEST
- CLI: `help`, `ver`, `cfg show`, `cfg export/import`, `set cal`, `i2cscan`, `jtail [n]`, `selftest`
- SPIFFS journal buffered, with rotation and streaming tail
- Preferences-backed configuration; JSON import/export
- Self-Test relay walker (Y01..Y09) with 30s timeout

---

## Repository Map
- `src/` — all modules (`io`, `motion`, `wj66`, `config`, `cli`, `lcd_ui`, `journal`, `selftest`, `inputs`)
- `include/globals.h` — shared enums, structs, constants
- `data/config.json` — safe defaults for first boot (SPIFFS)
- `docs/` — Developer Reference, CLI Reference, Changelog, QA Appendix, System Diagram

See `/docs/*` for full developer documentation.

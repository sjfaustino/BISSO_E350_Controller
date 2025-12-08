# Changelog

All notable changes to the BISSO E350 Bridge Saw Controller will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- Code formatting standard (.clang-format)
- Professional improvements documentation
- CHANGELOG.md (this file)
- Compile-time safety checks (static_assert)

---

## [1.0.0] - 2025-12-08

### Added
- **Production Release** - First stable release for industrial use
- **Grbl 1.1h Protocol** - Full compatibility with Universal GCode Sender
  - Real-time status query (?)
  - Feed hold (!)
  - Cycle start (~)
  - Soft reset (Ctrl-X)
  - Safe jogging protocol ($J=)
  - Settings commands ($100-$132)
- **Work Coordinate Systems** - G54-G59 with NVS persistence
  - G10 L20 Pn for setting offsets
  - Persistent storage survives power cycles
- **Sequential Motion Architecture** - Optimized for single-VFD bridge saw
  - One axis moves at a time
  - Three discrete speed profiles (SLOW/MEDIUM/FAST)
  - Feed override support (10%-200%)
- **Safety Systems**
  - Emergency stop with immediate halt
  - Soft limit enforcement
  - Safety state machine with interlocks
  - Watchdog with automatic recovery
  - Comprehensive fault logging to NVS
- **Performance Profiling**
  - Lightweight real-time profiling
  - Statistical tracking (min/max/avg/count)
  - Warning threshold detection
  - Health monitoring for critical loops
  - CLI commands (profile report/health/reset)
- **SheetCAM Integration**
  - Custom post-processor (BISSO_E350.scpost)
  - Setup guide for CAM configuration
  - Stone cutting best practices documentation
  - Complete CAM-to-CNC workflow guide
- **Universal GCode Sender Integration**
  - Comprehensive setup guide
  - Test G-code files and validation checklists
  - Quick reference card
  - Troubleshooting documentation
- **Web Interface**
  - Real-time status monitoring
  - Configuration management
  - Jog controls
  - File upload capability
- **Documentation** (270+ KB)
  - UGS setup and configuration
  - SheetCAM setup and workflow
  - Stone cutting parameters and techniques
  - Code review and validation checklists
  - Bridge saw upgrade analysis
  - Professional improvement recommendations

### Changed
- **Jog Mode Enhancement** - $J= commands now respect parser's distance mode (G90/G91)
  - Allows explicit G90/G91 in jog command to override
  - Falls back to parser state if not specified
  - Safer default behavior
- **M3/M5 Spindle Control** - Updated documentation
  - Clarified that spindle is manually controlled by operator
  - M3/M5 commands accepted for G-code compatibility but perform no action
  - Added operator notes in G-code output

### Fixed
- **Jog Mode Default** - Previously always used absolute mode, now checks parser state
- **Spindle Control Documentation** - Removed incorrect relay assignment reference
  - M3/M5 do not control any relays (manual spindle operation)
  - Commands are no-ops but accepted for Grbl compatibility

### Security
- Emergency stop protocol (immediate halt on !)
- Soft limit enforcement prevents out-of-bounds moves
- Safety interlock checking
- Watchdog monitoring for task health
- Fault logging for post-incident analysis

### Performance
- FreeRTOS task architecture with priority scheduling
  - Safety task: 5ms, P24 (critical)
  - Motion task: 10ms, P22 (high)
  - Encoder task: 20ms, P20
  - PLC communication: 50ms, P18
- I2C bus recovery with automatic retry
- Motion buffer for smoother execution
- Optimized encoder communication (WJ66 protocol)

### Hardware Support
- ESP32-S3 microcontroller (dual-core)
- KC868-A16 industrial controller board
- WJ66 digital readout (4-axis encoder consolidation)
- PCF8574 I2C I/O expanders (PLC emulation)
- Single VFD with mechanical axis switching
- Three discrete speed profiles via PLC

### Known Limitations
- **Sequential Motion Only** - Cannot move multiple axes simultaneously
  - Inherent hardware limitation (single VFD)
  - Point-to-point moves only, no true interpolation
- **Three Discrete Speeds** - Not continuously variable
  - SLOW (~300 mm/min), MEDIUM (~1200 mm/min), FAST (~2400 mm/min)
  - Feed rates are mapped to closest profile
- **Manual Spindle Control** - No automatic blade speed control
  - Operator must manually start/stop blade
  - Operator must manually set correct RPM for material
- **Manual Water Control** - No automatic coolant control
  - Operator must manually adjust water flow
- **No Arc Interpolation** - G2/G3 not supported
  - SheetCAM must convert arcs to line segments
  - Circles become polygons (100+ segments recommended)

---

## [0.9.0] - 2025-11-30

### Added
- Beta testing release
- Core motion control system
- Safety state machine
- Basic G-code parser
- CLI diagnostic commands
- Encoder integration
- PLC communication

### Changed
- Migrated from polling to interrupt-driven inputs
- Improved I2C error handling

### Fixed
- Motion stall detection false positives
- Encoder timeout issues
- Configuration validation edge cases

---

## [0.5.0] - 2025-11-15

### Added
- Alpha release for internal testing
- Basic firmware structure
- Task manager with FreeRTOS
- Configuration system with NVS
- Fault logging framework
- Boot validation system

---

## Version History Summary

| Version | Date | Status | Highlights |
|---------|------|--------|------------|
| 1.0.0 | 2025-12-08 | **PRODUCTION** | Full Grbl 1.1h, UGS integration, SheetCAM support |
| 0.9.0 | 2025-11-30 | Beta | Core functionality complete |
| 0.5.0 | 2025-11-15 | Alpha | Initial framework |

---

## Upgrade Notes

### Upgrading to 1.0.0 from 0.9.x

**Configuration Changes:**
- Work coordinate systems (G54-G59) now persist in NVS
- New calibration keys for speed profiles
- Performance profiler settings available

**G-code Compatibility:**
- $J= jog commands now respect G90/G91 mode
- M3/M5 commands accepted but perform no action (manual spindle)

**Required Actions:**
1. Review and update any custom G-code to use arc-to-line conversion
2. Set work coordinate offsets (G10 L20 P1 X0 Y0 Z0)
3. Calibrate speed profiles for your VFD (see calibration docs)
4. Test with dry run before cutting expensive material

**Breaking Changes:**
- None - fully backward compatible with 0.9.x

---

## Development Roadmap

### Planned for 1.1.0
- Additional static_assert compile-time checks
- Copyright headers on all source files
- Unit test framework
- Enhanced error messages

### Planned for 1.2.0
- CI/CD pipeline
- Doxygen API documentation
- Simulation mode for development
- Extended profiling metrics

### Planned for 2.0.0
- C++ namespace organization
- Modular architecture
- Plugin system for extensions
- Enhanced web interface

---

## Contributing

Changes should be documented in the "Unreleased" section as they are made, then moved to a versioned section upon release.

### Entry Format

```markdown
### Added
- New feature description with context

### Changed
- Modified behavior with explanation

### Deprecated
- Feature marked for removal (include version when it will be removed)

### Removed
- Deleted feature with reason

### Fixed
- Bug fix description and reference

### Security
- Vulnerability fix (if public, include CVE if applicable)
```

---

## Support

For questions, issues, or contributions:
- **Author:** Sergio Faustino <sjfaustino@gmail.com>
- **Project:** BISSO E350 Bridge Saw Controller
- **License:** TBD (add LICENSE file)

---

**[Unreleased]: https://github.com/your-repo/compare/v1.0.0...HEAD**
**[1.0.0]: https://github.com/your-repo/releases/tag/v1.0.0**

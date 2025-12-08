(==================================================================)
(BISSO E350 Controller - Comprehensive Test Program)
(Firmware: Gemini v3.5.25+)
(Test Date: 2025-12-08)
(==================================================================)
(
(PURPOSE: Verify all controller features systematically)
(
(FEATURES TESTED:)
(  1. G-code parsing and response)
(  2. Work coordinate systems (G54-G59))
(  3. Absolute and relative modes (G90/G91))
(  4. Sequential motion (X, then Y, then Z))
(  5. Feed rates and speed profiles)
(  6. Work zero setting (G10 L20))
(  7. M-codes (M3/M5 spindle, M0/M2 program control))
(  8. Position accuracy and repeatability)
(  9. Status reporting during motion)
( 10. Emergency stop recovery)
(
(SAFETY WARNINGS:)
(  - This test will move ALL axes)
(  - Ensure 200mm clearance in all directions from current position)
(  - Water and spindle are MANUAL - not controlled by this program)
(  - Emergency stop must be accessible)
(  - Operator must monitor entire test)
(
(SETUP REQUIRED:)
(  - Connect controller to UGS)
(  - Position machine in center of travel)
(  - Verify 200mm clearance in all directions)
(  - Set work zero at current position before starting)
(  - DO NOT start water or spindle - this is a dry run)
(
(==================================================================)

(TEST SECTION 1: Initial Setup and Verification)
(==================================================================)
G90            (Absolute mode)
G54            (Select work coordinate system 1)
G21            (Millimeter mode - always use MM)
G0 F1000       (Set rapid feed rate)

(Verify current position - should be at work zero)
(Check UGS display shows WPos: 0.000, 0.000, 0.000, 0.000)

(==================================================================)
(TEST SECTION 2: Single Axis Moves - X Axis)
(==================================================================)
(Test: Basic X axis motion and return)
G0 X10         (Move X to +10mm)
G4 P500        (Dwell 0.5 seconds)
G0 X20         (Move X to +20mm)
G4 P500
G0 X30         (Move X to +30mm)
G4 P500
G0 X0          (Return X to zero)
G4 P1000       (Dwell 1 second at zero)

(==================================================================)
(TEST SECTION 3: Single Axis Moves - Y Axis)
(==================================================================)
(Test: Basic Y axis motion and return)
G0 Y10         (Move Y to +10mm)
G4 P500
G0 Y20         (Move Y to +20mm)
G4 P500
G0 Y30         (Move Y to +30mm)
G4 P500
G0 Y0          (Return Y to zero)
G4 P1000

(==================================================================)
(TEST SECTION 4: Single Axis Moves - Z Axis)
(==================================================================)
(Test: Basic Z axis motion and return)
(CAUTION: Z typically moves blade up/down)
G0 Z5          (Move Z to +5mm - raises)
G4 P500
G0 Z10         (Move Z to +10mm)
G4 P500
G0 Z0          (Return Z to zero)
G4 P1000

(==================================================================)
(TEST SECTION 5: Single Axis Moves - A Axis)
(==================================================================)
(Test: Rotational axis if present)
G0 A5          (Rotate A to +5 degrees)
G4 P500
G0 A10         (Rotate A to +10 degrees)
G4 P500
G0 A0          (Return A to zero)
G4 P1000

(==================================================================)
(TEST SECTION 6: Sequential Multi-Axis Moves)
(==================================================================)
(Test: Multi-axis commands auto-split into sequential moves)
(Remember: Controller moves ONE axis at a time)
(G0 X10 Y10 will move X first, THEN Y)

G0 X10 Y10     (Move to (10,10) - X first, then Y)
G4 P500
G0 X20 Y20     (Move to (20,20) - Sequential)
G4 P500
G0 X0 Y0       (Return to origin - Sequential)
G4 P1000

(==================================================================)
(TEST SECTION 7: Rectangle Pattern - Sequential Motion)
(==================================================================)
(Test: Typical cutting pattern - moves are sequential)
(Draws 50mm x 30mm rectangle)

(Start at origin)
G0 X0 Y0 Z0

(Move to bottom-left corner)
G0 X0 Y0

(Trace rectangle perimeter)
G0 X50 Y0      (Bottom edge - X moves, then Y stays)
G4 P500
G0 X50 Y30     (Right edge - X stays, then Y moves)
G4 P500
G0 X0 Y30      (Top edge - X moves, then Y stays)
G4 P500
G0 X0 Y0       (Left edge - X stays, then Y moves)
G4 P500

(Return to origin)
G0 X0 Y0 Z0
G4 P1000

(==================================================================)
(TEST SECTION 8: Relative Mode Testing - G91)
(==================================================================)
(Test: Incremental/relative positioning)

G91            (Switch to relative mode)
G0 X10         (Move +10mm from current position)
G4 P500
G0 X10         (Move another +10mm - now at X=20 relative to start)
G4 P500
G0 Y10         (Move +10mm in Y)
G4 P500
G0 Y10         (Move another +10mm in Y - now at Y=20)
G4 P500

(Return using relative moves)
G0 X-20        (Move back -20mm in X)
G4 P500
G0 Y-20        (Move back -20mm in Y)
G4 P500

G90            (Return to absolute mode)
G4 P1000

(==================================================================)
(TEST SECTION 9: Feed Rate Testing)
(==================================================================)
(Test: Different feed rates - maps to SLOW/MED/FAST profiles)
(Your controller uses discrete speed profiles, not smooth feed rates)

(Slow feed - should use SLOW profile)
G1 X30 F100    (Move X slowly)
G4 P500

(Medium feed - should use MED profile)
G1 X60 F500    (Move X medium speed)
G4 P500

(Fast feed - should use FAST profile)
G1 X90 F1500   (Move X fast)
G4 P500

(Return to zero)
G0 X0
G4 P1000

(==================================================================)
(TEST SECTION 10: M-Code Testing)
(==================================================================)
(Test: M-codes for program control and spindle)

(NOTE: M3/M5 spindle commands are parsed but do nothing)
(Spindle is manually controlled on this machine)

M3 S1000       (Spindle ON command - MANUAL CONTROL, command accepted)
G4 P1000       (Dwell - operator would start spindle manually here)

(Simulate cutting move - dry run without spindle actually on)
G1 X50 F300    (Feed move at 300mm/min)
G4 P500

M5             (Spindle OFF command - MANUAL CONTROL, command accepted)
G4 P1000       (Dwell - operator would stop spindle manually here)

G0 X0          (Return to zero)
G4 P1000

(==================================================================)
(TEST SECTION 11: Work Coordinate System Testing)
(==================================================================)
(Test: Multiple work coordinate systems G54-G59)

(Current position should be at origin of G54)
G54            (Select G54)
G0 X0 Y0 Z0    (Go to G54 origin)
G4 P500

(Set new work zero for G55 at current position + 50mm)
G0 X50 Y0 Z0   (Move to +50mm X)
G10 L20 P2 X0 Y0 Z0 (Set G55 zero here - current pos becomes 0,0,0 in G55)

(Now switch to G55)
G55            (Select G55)
(Current machine position is now 0,0,0 in G55 coordinates)
G0 X10 Y10     (Move in G55 coordinates)
G4 P500

(Switch back to G54)
G54            (Select G54)
(Should now be at X=60, Y=10 in G54 coordinates)
G4 P500

(Return to G54 origin)
G0 X0 Y0 Z0
G4 P1000

(==================================================================)
(TEST SECTION 12: Position Accuracy Test)
(==================================================================)
(Test: Repeatability - move away and return multiple times)

G90            (Absolute mode)
G54            (Select G54)

(Cycle 1)
G0 X100        (Move to 100mm)
G4 P500
G0 X0          (Return to zero)
G4 P500

(Cycle 2)
G0 X100
G4 P500
G0 X0
G4 P500

(Cycle 3)
G0 X100
G4 P500
G0 X0
G4 P1000

(Position should be exactly at 0.000 - verify in UGS)
(If drift is observed, encoder calibration may be needed)

(==================================================================)
(TEST SECTION 13: Combined Motion Test - Square Pattern with Z)
(==================================================================)
(Test: 3D motion - sequential X/Y/Z moves)

(Trace a 40mm square at Z=5mm)
G0 Z0          (Start at Z zero)
G0 X0 Y0       (Go to corner)

G0 Z5          (Raise Z to 5mm)
G0 X40 Y0      (Move to corner 2)
G0 Z5          (Already at Z5, redundant but tests parser)
G0 X40 Y40     (Move to corner 3)
G0 Z5
G0 X0 Y40      (Move to corner 4)
G0 Z5
G0 X0 Y0       (Return to corner 1)

G0 Z0          (Lower Z back to zero)
G4 P1000

(==================================================================)
(TEST SECTION 14: Program Control Testing)
(==================================================================)
(Test: Program pause and stop commands)

G0 X20 Y20     (Move to position)
M0             (Program pause - UGS should stop and wait for resume)
(Press Resume/Cycle Start in UGS to continue)

G0 X0 Y0       (Return after resume)
G4 P500

(==================================================================)
(TEST SECTION 15: Emergency Stop Recovery Test)
(==================================================================)
(Test: Controller recovery after emergency stop)
(
(MANUAL TEST - Not automated:)
(  1. Start this section of program)
(  2. Press ! (Feed Hold) in UGS during move)
(  3. Verify motion stops immediately)
(  4. Press ~ (Cycle Start) to resume)
(  5. Motion should continue from stopped position)

G0 X50 F200    (Slow move to allow time to press !)
G4 P500
G0 X0
G4 P1000

(==================================================================)
(TEST SECTION 16: Return to Safe Position)
(==================================================================)
(Test: Ensure machine is in safe state after test)

G90            (Absolute mode)
G54            (Select original coordinate system)
G0 Z0          (Ensure Z is at safe height)
G0 X0 Y0       (Return X and Y to origin)
G0 A0          (Return A to zero)

(==================================================================)
(TEST COMPLETE)
(==================================================================)
M2             (Program end)

(==================================================================)
(VALIDATION CHECKLIST:)
(==================================================================)
( [ ] All moves completed without errors)
( [ ] No "error:N" messages in console)
( [ ] Each move returned "ok")
( [ ] Final position is 0.000, 0.000, 0.000, 0.000)
( [ ] No position drift observed)
( [ ] Status reporting worked throughout)
( [ ] Emergency stop test passed (manual))
( [ ] M3/M5 commands accepted (manual spindle))
( [ ] G54/G55 coordinate systems worked)
( [ ] Relative mode (G91) worked correctly)
( [ ] Different feed rates worked (SLOW/MED/FAST))
( [ ] Multi-axis moves split correctly)
( [ ] Machine returned to exact zero position)
(
(If all checkboxes passed:)
(  ✓ Controller is functioning correctly)
(  ✓ Ready for production use)
(  ✓ Can proceed with actual cutting operations)
(
(If any issues observed:)
(  - Check encoder connections)
(  - Verify PPM calibration)
(  - Review firmware settings ($$))
(  - Check fault log (faults show))
(  - Run diagnostics (debug all))
(
(==================================================================)
(END OF TEST PROGRAM)
(==================================================================)

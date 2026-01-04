/**
 * @file gcode_parser.cpp
 * @brief G-Code Parser with WCS Logic (PosiPro)
 */

#include "gcode_parser.h"
#include "motion.h"
#include "motion_state.h"
#include "motion_buffer.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "lcd_message.h"  // PHASE 3.2: M117 LCD message support
#include "auto_report.h"  // PHASE 4.0: M154 auto-report support
#include "lcd_sleep.h"    // PHASE 4.0: M255 LCD sleep support
#include "plc_iface.h"    // PHASE 4.0: M226 pin state reading
#include "board_inputs.h"  // PHASE 4.0: M226 board input reading
#include "hardware_config.h"  // For MachineCalibration in handleM114()
#include "firmware_version.h" // For M115 version reporting
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

GCodeParser gcodeParser;

GCodeParser::GCodeParser() : distanceMode(G_MODE_ABSOLUTE), currentFeedRate(50.0f), currentWCS(WCS_G54), machineCoordinatesMode(false), programPaused(false), pauseStartTime(0) {
    memset(wcs_offsets, 0, sizeof(wcs_offsets));
}

void GCodeParser::init() {
    logInfo("[GCODE] Initializing...");
    distanceMode = G_MODE_ABSOLUTE;
    currentFeedRate = 50.0f; 
    currentWCS = WCS_G54;
    loadWCS(); 
    logInfo("[GCODE] Ready. WCS: G54");
}

void GCodeParser::loadWCS() {
    char key[16];
    for(int s=0; s<6; s++) {
        for(int a=0; a<4; a++) {
            snprintf(key, sizeof(key), "g%d_%c", 54+s, "xyza"[a]);
            wcs_offsets[s][a] = configGetFloat(key, 0.0f);
        }
    }
}

void GCodeParser::saveWCS(uint8_t system) {
    if(system > 5) return;
    char key[16];
    for(int a=0; a<4; a++) {
        snprintf(key, sizeof(key), "g%d_%c", 54+system, "xyza"[a]);
        configSetFloat(key, wcs_offsets[system][a]);
    }
    configUnifiedFlush(); 
}

float GCodeParser::getWorkPosition(uint8_t axis, float mpos) {
    if(axis >= 4) return 0.0f;
    return mpos - wcs_offsets[currentWCS][axis];
}

void GCodeParser::getWCO(float* wco_array) {
    if(!wco_array) return;
    for(int i=0; i<4; i++) wco_array[i] = wcs_offsets[currentWCS][i];
}

float GCodeParser::getCurrentFeedRate() {
    return currentFeedRate;
}

void GCodeParser::getParserState(char* buffer, size_t len) {
    // Standard Grbl response string
    snprintf(buffer, len, "[GC:G%d G%d %s G94 M5]",
        (motionIsMoving() ? 1 : 0),
        (54 + currentWCS),
        (distanceMode == G_MODE_ABSOLUTE ? "G90" : "G91")
    );
}

/**
 * @brief Validate G-code syntax before queuing to motion buffer
 * @param line G-code command line to validate
 * @param error_msg Buffer for error message output
 * @param error_msg_len Size of error message buffer
 * @return true if syntax is valid, false otherwise
 */
bool GCodeParser::validateGCodeSyntax(const char* line, char* error_msg, size_t error_msg_len) {
    if (!line) {
        snprintf(error_msg, error_msg_len, "Null command");
        return false;
    }

    size_t len = strlen(line);

    // Check length (prevent buffer overflow)
    if (len == 0) {
        snprintf(error_msg, error_msg_len, "Empty command");
        return false;
    }
    if (len > 128) {
        snprintf(error_msg, error_msg_len, "Command too long (max 128 chars)");
        return false;
    }

    // Skip comments and whitespace
    if (line[0] == '(' || line[0] == ';' || line[0] == ' ' || line[0] == '\t') {
        return true; // Comments are valid
    }

    // Check for valid command letter (G or M)
    if (line[0] != 'G' && line[0] != 'g' && line[0] != 'M' && line[0] != 'm') {
        snprintf(error_msg, error_msg_len, "Invalid command (must start with G or M)");
        return false;
    }

    // Extract command number
    char cmd_letter = toupper(line[0]);
    const char* num_start = line + 1;
    char* end_ptr;
    long cmd_num = strtol(num_start, &end_ptr, 10);

    // Check if number was found
    if (end_ptr == num_start) {
        snprintf(error_msg, error_msg_len, "Missing command number after %c", cmd_letter);
        return false;
    }

    // Validate command ranges
    if (cmd_letter == 'G') {
        if (cmd_num < 0 || cmd_num > 99) {
            snprintf(error_msg, error_msg_len, "Invalid G-code number G%ld (range: G0-G99)", cmd_num);
            return false;
        }
        // Check for supported G-codes
        bool valid_g = (cmd_num == 0 || cmd_num == 1 || cmd_num == 4 || cmd_num == 10 ||
                       cmd_num == 28 || cmd_num == 30 || cmd_num == 53 ||
                       (cmd_num >= 54 && cmd_num <= 59) ||
                       cmd_num == 90 || cmd_num == 91 || cmd_num == 92);
        if (!valid_g) {
            snprintf(error_msg, error_msg_len, "Unsupported G-code G%ld", cmd_num);
            return false;
        }
    } else if (cmd_letter == 'M') {
        if (cmd_num < 0 || cmd_num > 999) {
            snprintf(error_msg, error_msg_len, "Invalid M-code number M%ld (range: M0-M999)", cmd_num);
            return false;
        }
        // Check for supported M-codes
        bool valid_m = (cmd_num == 0 || cmd_num == 1 || cmd_num == 114 || cmd_num == 115 ||
                       cmd_num == 117 || cmd_num == 154 || cmd_num == 226 || cmd_num == 255 ||
                       cmd_num == 999);
        if (!valid_m) {
            snprintf(error_msg, error_msg_len, "Unsupported M-code M%ld", cmd_num);
            return false;
        }
    }

    // Validate parameters (if any)
    const char* param_start = end_ptr;
    while (*param_start) {
        // Skip whitespace
        while (*param_start && (*param_start == ' ' || *param_start == '\t')) {
            param_start++;
        }
        if (*param_start == '\0') break;

        // Check for valid parameter letter
        char param_letter = toupper(*param_start);
        bool valid_param = (param_letter == 'X' || param_letter == 'Y' || param_letter == 'Z' ||
                           param_letter == 'A' || param_letter == 'F' || param_letter == 'S' ||
                           param_letter == 'P' || param_letter == 'L' || param_letter == 'R' ||
                           param_letter == 'I' || param_letter == 'J' || param_letter == 'K');

        if (!valid_param) {
            snprintf(error_msg, error_msg_len, "Invalid parameter '%c'", param_letter);
            return false;
        }

        // Check parameter value
        param_start++;
        char* param_end;
        float param_val = strtof(param_start, &param_end);

        if (param_end == param_start) {
            snprintf(error_msg, error_msg_len, "Missing value for parameter %c", param_letter);
            return false;
        }

        // Validate numeric range (prevent overflow)
        if (!isfinite(param_val)) {
            snprintf(error_msg, error_msg_len, "Invalid numeric value for %c", param_letter);
            return false;
        }

        param_start = param_end;
    }

    return true;
}

bool GCodeParser::processCommand(const char* line) {
    if (!line || strlen(line) == 0) return false;
    if (line[0] == '(' || line[0] == ';') return true;

    // PHASE 5.10: Validate G-code syntax before processing
    char error_msg[128];
    if (!validateGCodeSyntax(line, error_msg, sizeof(error_msg))) {
        logError("[GCODE] Syntax error: %s (line: %s)", error_msg, line);
        return false;
    }

    float val = -1.0f;
    
    // G Codes
    if (parseCode(line, 'G', val)) {
        int cmd = (int)val;
        switch (cmd) {
            case 0:
            case 1:  handleG0_G1(line); break;
            case 4:  handleG4(line); break;  // G4 Dwell
            case 10: handleG10(line); break; // G10 L20 P1...
            case 28: handleG28(line); break; // PHASE 5.1: G28 Home
            case 30: handleG30(line); break; // PHASE 5.1: G30 Predefined position
            case 53: handleG53(line); break; // PHASE 5.1: G53 Machine coordinates
            case 54 ... 59: handleG5x(cmd - 54); break; // WCS Select
            case 90: handleG90(); break;
            case 91: handleG91(); break;
            case 92: handleG92(line); break;
            default: return false;
        }
        return true;
    }

    // M Codes
    if (parseCode(line, 'M', val)) {
        int cmd = (int)val;
        switch (cmd) {
            case 0:
            case 1:  handleM0_M1(line); break; // PHASE 5.1: M0/M1 Program stop/pause
            case 2:  motionStop(); break;
            case 3:  elboQ73SetRelay(ELBO_Q73_SPEED_1, true); break;
            case 5:  elboQ73SetRelay(ELBO_Q73_SPEED_1, false); break;
            // PHASE 3.2: M117 - Display message on LCD (standard gcode command)
            case 117: handleM117(line); break;
            // PHASE 4.0: M114 - Get current position
            case 114: handleM114(); break;
            // PHASE 4.0: M115 - Firmware info
            case 115: handleM115(); break;
            // PHASE 4.0: M154 - Position auto-report
            case 154: handleM154(line); break;
            // PHASE 4.0: M226 - Wait for pin state
            case 226: handleM226(line); break;
            // PHASE 4.0: M255 - LCD sleep/backlight timeout
            case 255: handleM255(line); break;
            case 112: motionEmergencyStop(); break;
            default: return false;
        }
        return true;
    }

    return false;
}

void GCodeParser::handleG10(const char* line) {
    // G10 L20 P1 X0 Y0 (Set WCS G54 offset so current pos = 0)
    float pVal = 0, lVal = 0;
    if(!parseCode(line, 'L', lVal) || lVal != 20) return; 
    if(!parseCode(line, 'P', pVal)) pVal = 1; 
    
    int sys_idx = (int)pVal - 1;
    if(sys_idx < 0 || sys_idx > 5) return;

    float mPos[4] = {
        motionGetPositionMM(0), motionGetPositionMM(1), 
        motionGetPositionMM(2), motionGetPositionMM(3)
    };

    float val;
    if(parseCode(line, 'X', val)) wcs_offsets[sys_idx][0] = mPos[0] - val;
    if(parseCode(line, 'Y', val)) wcs_offsets[sys_idx][1] = mPos[1] - val;
    if(parseCode(line, 'Z', val)) wcs_offsets[sys_idx][2] = mPos[2] - val;
    if(parseCode(line, 'A', val)) wcs_offsets[sys_idx][3] = mPos[3] - val;

    saveWCS(sys_idx);
    logInfo("[GCODE] Updated G%d Offsets", 54 + sys_idx);
}

void GCodeParser::handleG5x(int system_idx) {
    currentWCS = (wcs_system_t)system_idx;
    logInfo("[GCODE] Switched to G%d", 54 + system_idx);
}

void GCodeParser::handleG0_G1(const char* line) {
    float fVal = 0.0f;
    if (parseCode(line, 'F', fVal) && fVal > 0) currentFeedRate = fVal;

    float req[4] = {0};
    bool has[4] = { false };
    const char axes_char[] = "XYZA";

    for(int i=0; i<4; i++) {
        if(parseCode(line, axes_char[i], req[i])) has[i] = true;
    }

    if (!has[0] && !has[1] && !has[2] && !has[3]) return;

    float curM[4] = {
        motionGetPositionMM(0), motionGetPositionMM(1),
        motionGetPositionMM(2), motionGetPositionMM(3)
    };

    float targetM[4];

    for(int i=0; i<4; i++) {
        if (has[i]) {
            if (distanceMode == G_MODE_ABSOLUTE) {
                // PHASE 5.1: G53 machine coordinates mode (ignore WCS offset)
                if (machineCoordinatesMode) {
                    targetM[i] = req[i];
                } else {
                    targetM[i] = req[i] + wcs_offsets[currentWCS][i];
                }
            } else {
                targetM[i] = curM[i] + req[i];
            }
        } else {
            targetM[i] = curM[i];
        }
    }

    bool move = false;
    for(int i=0; i<4; i++) if(fabs(targetM[i] - curM[i]) > 0.01) move=true;

    if(move) pushMove(targetM[0], targetM[1], targetM[2], targetM[3]);
}

void GCodeParser::handleG4(const char* line) {
    // G4 Dwell command - non-blocking pause
    // G4 P<ms>  - Dwell for P milliseconds
    // G4 S<sec> - Dwell for S seconds
    // Example: G4 P500  (dwell 500ms)
    // Example: G4 S2    (dwell 2 seconds)

    float p_val = 0.0f;
    float s_val = 0.0f;
    uint32_t dwell_ms = 0;

    // Check for P parameter (milliseconds)
    if (parseCode(line, 'P', p_val) && p_val > 0) {
        dwell_ms = (uint32_t)p_val;
    }
    // Check for S parameter (seconds) - takes precedence if both specified
    else if (parseCode(line, 'S', s_val) && s_val > 0) {
        dwell_ms = (uint32_t)(s_val * 1000.0f);
    }

    if (dwell_ms > 0) {
        if (motionDwell(dwell_ms)) {
            logInfo("[GCODE] G4 Dwell: %lu ms", (unsigned long)dwell_ms);
        } else {
            logWarning("[GCODE] G4 Dwell failed - motion may be active");
        }
    } else {
        logWarning("[GCODE] G4 requires P<ms> or S<sec> parameter");
    }
}

void GCodeParser::pushMove(float x, float y, float z, float a) {
    if (configGetInt(KEY_MOTION_BUFFER_ENABLE, 0)) {
        if (!motionBuffer.isFull()) motionBuffer.push(x, y, z, a, currentFeedRate);
    } else {
        motionMoveAbsolute(x, y, z, a, currentFeedRate);
    }
}

void GCodeParser::handleG90() { distanceMode = G_MODE_ABSOLUTE; }
void GCodeParser::handleG91() { distanceMode = G_MODE_RELATIVE; }

// PHASE 3.2: M117 - Display message on LCD
void GCodeParser::handleM117(const char* line) {
    // M117 Display Message (standard gcode format)
    // Syntax: M117 <message text>
    // Example: M117 Cutting edge...

    if (!line || strlen(line) < 4) return;

    // Find the start of the message (skip "M117" and whitespace)
    const char* msg_start = line + 4;  // Skip "M117"
    while (*msg_start && (*msg_start == ' ' || *msg_start == '\t')) msg_start++;

    if (*msg_start == '\0') return;  // No message text

    // Display message for 3 seconds (3000ms)
    lcdMessageSet(msg_start, 3000);
    logInfo("[GCODE] M117: Display message: '%s'", msg_start);
}

// PHASE 4.0: M114 - Get current position (standard gcode command)
void GCodeParser::handleM114() {
    // M114 Get Position (standard gcode format)
    // Reports: X:<value> Y:<value> Z:<value> A:<value>
    // Units: mm for X/Y/Z, degrees for A (rotary axis)

    float x_mm = motionGetPositionMM(0);
    float y_mm = motionGetPositionMM(1);
    float z_mm = motionGetPositionMM(2);

    // For A axis (rotary), convert counts to degrees using calibration
    int32_t a_counts = motionGetPosition(3);
    float a_deg = 0.0f;

    // Get calibration data for A axis (from hardware_config.h)
    if (machineCal.A.pulses_per_degree > 0) {
        a_deg = a_counts / machineCal.A.pulses_per_degree;
    } else {
        // Fallback to default scale factor if calibration not set
        a_deg = a_counts / 1000.0f;  // MOTION_POSITION_SCALE_FACTOR_DEG
    }

    // Report current position in standard Grbl format
    char response[80];
    snprintf(response, sizeof(response),
             "[POS:X:%.1f Y:%.1f Z:%.1f A:%.1f]",
             x_mm, y_mm, z_mm, a_deg);

    logPrintln(response);
    logInfo("[GCODE] M114 Position: X:%.1f Y:%.1f Z:%.1f A:%.1f",
            x_mm, y_mm, z_mm, a_deg);
}

// PHASE 4.0: M115 - Firmware info (standard gcode command)
void GCodeParser::handleM115() {
    // M115 Report Firmware Version & Capabilities
    // Standard Grbl response format

    // Build firmware info response
    char ver_buf[32];
    firmwareGetVersionString(ver_buf, sizeof(ver_buf));
    
    serialLoggerLock();
    Serial.printf("[VER:%s BISSO-E350]\n", ver_buf);
    Serial.println("[OPT:B#,M,T#]");  // Options: Block #, Messages, Real-time status

    // Report capabilities
    Serial.println("[CAPABILITY:4-axis]");        // 4 axes: X, Y, Z, A
    Serial.println("[CAPABILITY:adaptive-feed]");  // Feed override support
    Serial.println("[CAPABILITY:G4-dwell]");       // G4 dwell support
    Serial.println("[CAPABILITY:M114-position]");  // M114 position reporting
    Serial.println("[CAPABILITY:M154-auto-report]"); // M154 auto-report support
    Serial.println("[CAPABILITY:M117-lcd-msg]");  // M117 LCD message support
    Serial.println("[CAPABILITY:WCS-6-system]");  // 6 work coordinate systems
    Serial.println("[CAPABILITY:soft-limits]");   // Soft limits enabled
    serialLoggerUnlock();

    logInfo("[GCODE] M115 Firmware Info Reported");
}

// PHASE 4.0: M154 - Position auto-report (non-blocking)
void GCodeParser::handleM154(const char* line) {
    // M154 Position Auto-Report
    // M154 S<interval>  - Set auto-report interval (0 = disable)
    // Interval in seconds, 0.1 to 60.0 supported
    // Example: M154 S1   (report every 1 second)
    // Example: M154 S0   (disable auto-report)

    float s_val = 0.0f;

    // Check for S parameter (interval in seconds)
    if (parseCode(line, 'S', s_val) && s_val >= 0) {
        uint32_t interval_sec = (uint32_t)s_val;

        if (autoReportSetInterval(interval_sec)) {
            if (interval_sec == 0) {
                logInfo("[GCODE] M154 Auto-Report Disabled");
            } else {
                logInfo("[GCODE] M154 Auto-Report Enabled - %lu sec", (unsigned long)interval_sec);
            }
        } else {
            logWarning("[GCODE] M154 Failed to set interval");
        }
    } else {
        logWarning("[GCODE] M154 requires S<interval> parameter (in seconds)");
    }
}

// PHASE 4.0: M226 - Wait for pin state (non-blocking)
void GCodeParser::handleM226(const char* line) {
    // M226 Wait for Pin State (non-blocking with timeout)
    // M226 P<pin> S<state> [A<type>] [T<timeout>]
    // P: Pin ID (0-7 for I2C)
    // S: State to wait for (0 or 1)
    // A: Pin type (0=I73, 1=Board, 2=GPIO) - default 0
    // T: Timeout in seconds (0 = no timeout, default = 5)
    // Example: M226 P3 S1     (wait for I73 pin 3 to go HIGH)
    // Example: M226 P0 S0 A1 T10  (wait for Board pin 0 to go LOW, timeout 10sec)

    float p_val = -1.0f;
    float s_val = -1.0f;
    float a_val = 0.0f;  // Default to I73
    float t_val = 5.0f;  // Default 5 second timeout

    // Parse parameters
    if (!parseCode(line, 'P', p_val) || p_val < 0) {
        logWarning("[GCODE] M226 requires P<pin> parameter");
        return;
    }

    if (!parseCode(line, 'S', s_val) || (s_val != 0 && s_val != 1)) {
        logWarning("[GCODE] M226 requires S<state> parameter (0 or 1)");
        return;
    }

    parseCode(line, 'A', a_val);  // Optional, defaults to 0
    parseCode(line, 'T', t_val);  // Optional, defaults to 5

    uint8_t pin_id = (uint8_t)p_val;
    uint8_t pin_type = (uint8_t)a_val;
    uint8_t pin_state = (uint8_t)s_val;
    uint32_t timeout_sec = (uint32_t)t_val;

    // Validate pin type
    if (pin_type > 2) {
        logWarning("[GCODE] M226 invalid A<type> (0=I73, 1=Board, 2=GPIO)");
        return;
    }

    // Validate pin ranges
    if ((pin_type == 0 || pin_type == 1) && pin_id > 7) {
        logWarning("[GCODE] M226 invalid P<pin> for I2C (0-7)");
        return;
    }
    if (pin_type == 2 && pin_id > 39) {
        logWarning("[GCODE] M226 invalid P<pin> for GPIO (0-39)");
        return;
    }

    // Start waiting for pin
    if (motionWaitPin(pin_id, pin_type, pin_state, timeout_sec)) {
        logInfo("[GCODE] M226 Wait for pin %d type %d state %d timeout %lu sec",
                pin_id, pin_type, pin_state, (unsigned long)timeout_sec);
    } else {
        logWarning("[GCODE] M226 failed - motion may be active");
    }
}

// PHASE 4.0: M255 - LCD sleep/backlight timeout
void GCodeParser::handleM255(const char* line) {
    // M255 LCD Sleep/Backlight Timeout Control
    // M255 S<seconds>   - Set backlight timeout (0 = never sleep)
    // Example: M255 S0   (disable sleep, always on)
    // Example: M255 S300 (sleep after 300 seconds of inactivity)
    // Example: M255 S60  (sleep after 1 minute of inactivity)

    float s_val = 0.0f;

    // Check for S parameter (timeout in seconds)
    if (parseCode(line, 'S', s_val) && s_val >= 0) {
        uint32_t timeout_sec = (uint32_t)s_val;

        if (lcdSleepSetTimeout(timeout_sec)) {
            if (timeout_sec == 0) {
                logInfo("[GCODE] M255 LCD sleep disabled - always on");
            } else {
                logInfo("[GCODE] M255 LCD sleep enabled - timeout %lu seconds", (unsigned long)timeout_sec);
            }
        } else {
            logWarning("[GCODE] M255 failed to set timeout");
        }
    } else {
        logWarning("[GCODE] M255 requires S<timeout> parameter (in seconds)");
    }
}

// PHASE 5.1: G28 - Go to Machine Home
void GCodeParser::handleG28(const char* line) {
    // G28 Go to Machine Home
    // Homing sequence for specified axes or all if none specified
    // G28     - Home all axes
    // G28 X   - Home X axis only
    // G28 Y   - Home Y axis only
    // G28 Z   - Home Z axis only
    // G28 A   - Home A axis only
    // G28 X Y - Home X and Y axes

    // Check if homing is enabled
    if (!configGetInt(KEY_HOME_ENABLE, 0)) {
        logWarning("[GCODE] G28 homing disabled - not configured");
        return;
    }

    bool home_x = hasCode(line, 'X');
    bool home_y = hasCode(line, 'Y');
    bool home_z = hasCode(line, 'Z');
    bool home_a = hasCode(line, 'A');

    // If no axes specified, home all
    if (!home_x && !home_y && !home_z && !home_a) {
        home_x = home_y = home_z = home_a = true;
    }

    // IMPORTANT: A axis has no motor - only manual positioning with encoder feedback
    if (home_a) {
        logWarning("[GCODE] G28 A homing skipped - A axis is manual (no motor)");
        logWarning("[GCODE] Operator: Manually position A axis to zero and confirm");
        home_a = false;  // Cannot auto-home A axis
    }

    logInfo("[GCODE] G28 Homing: X=%d Y=%d Z=%d A=%d(manual)",
            home_x, home_y, home_z, home_a);

    // Execute homing sequence in SAFE ORDER: Z → Y → X
    // CRITICAL SAFETY: Always home Z first to lift tool/blade before moving X/Y
    // This prevents crashes into workpiece or fixture
    // Only one axis can home at a time due to single VFD motor constraint

    // 1. Home Z first (lift to safe height)
    if (home_z && !motionHome(2)) {
        logError("[GCODE] G28 Z homing failed - axis busy or error");
        return;
    }

    // 2. Home Y second (after Z is safe)
    if (home_y && !motionHome(1)) {
        logError("[GCODE] G28 Y homing failed - axis busy or error");
        return;
    }

    // 3. Home X last
    if (home_x && !motionHome(0)) {
        logError("[GCODE] G28 X homing failed - axis busy or error");
        return;
    }

    logInfo("[GCODE] G28 Homing sequence completed successfully");
}

// PHASE 5.1: G30 - Go to Predefined Position
void GCodeParser::handleG30(const char* line) {
    // G30 Go to Predefined Position
    // G30     - Go to safe position (default)
    // G30 P1  - Go to predefined position 1
    // G30 P2  - Go to predefined position 2

    float p_val = 0.0f;
    parseCode(line, 'P', p_val);
    int pos_id = (int)p_val;  // 0 = safe, 1 = pos1, etc.

    // Load predefined position from configuration
    float target[4];
    if (pos_id == 0) {  // Safe position
        target[0] = configGetFloat(KEY_POS_SAFE_X, 0.0f);
        target[1] = configGetFloat(KEY_POS_SAFE_Y, 0.0f);
        target[2] = configGetFloat(KEY_POS_SAFE_Z, 0.0f);
        target[3] = configGetFloat(KEY_POS_SAFE_A, 0.0f);
        logInfo("[GCODE] G30 Go to Safe Position: X:%.1f Y:%.1f Z:%.1f A:%.1f",
                target[0], target[1], target[2], target[3]);
    } else if (pos_id == 1) {  // Predefined position 1
        target[0] = configGetFloat(KEY_POS_1_X, 0.0f);
        target[1] = configGetFloat(KEY_POS_1_Y, 0.0f);
        target[2] = configGetFloat(KEY_POS_1_Z, 0.0f);
        target[3] = configGetFloat(KEY_POS_1_A, 0.0f);
        logInfo("[GCODE] G30 Go to Position 1: X:%.1f Y:%.1f Z:%.1f A:%.1f",
                target[0], target[1], target[2], target[3]);
    } else {
        logWarning("[GCODE] G30 Invalid position P%d (0-1 supported)", pos_id);
        return;
    }

    // Move to the predefined position
    pushMove(target[0], target[1], target[2], target[3]);
}

// PHASE 5.1: G53 - Machine Coordinates (Ignore WCS Offsets)
void GCodeParser::handleG53(const char* line) {
    // G53 Machine Coordinates
    // Next G0/G1 move will be in machine coordinates (ignoring WCS offset)
    // G53 G0 X100 Y200 Z50 A0  - Rapid to machine coordinates
    // G53 G1 X100 Y200 Z50 F100 - Linear move in machine coordinates

    machineCoordinatesMode = true;
    logInfo("[GCODE] G53 Machine Coordinates Mode Enabled");

    // Check if there's a move command following
    if (hasCode(line, 'G')) {
        float g_val = -1.0f;
        if (parseCode(line, 'G', g_val) && ((int)g_val == 0 || (int)g_val == 1)) {
            // Process the G0/G1 move
            handleG0_G1(line);
        }
    }

    machineCoordinatesMode = false;  // Reset for next command
}

// PHASE 5.1: G92 - Set Position / Calibration
void GCodeParser::handleG92(const char* line) {
    // G92 Set Position / Calibration
    // Sets the current position to the specified value (without moving)
    // Useful for calibration after manual adjustment
    // G92 X0 Y0 Z0 A0        - Set all axes to origin
    // G92 Z100               - Set Z to 100mm (material height)
    // G92 X10.5 Y-5.2 Z50.0  - Set specific coordinates

    // Get current machine positions first
    float curM[4] = {
        motionGetPositionMM(0), motionGetPositionMM(1),
        motionGetPositionMM(2), motionGetPositionMM(3)
    };

    // Parse any axis values provided
    float val;
    bool has_any = false;
    if (parseCode(line, 'X', val)) { curM[0] = val; has_any = true; }
    if (parseCode(line, 'Y', val)) { curM[1] = val; has_any = true; }
    if (parseCode(line, 'Z', val)) { curM[2] = val; has_any = true; }
    if (parseCode(line, 'A', val)) { curM[3] = val; has_any = true; }

    if (!has_any) {
        logWarning("[GCODE] G92 requires at least one axis value (X/Y/Z/A)");
        return;
    }

    logInfo("[GCODE] G92 Set Position - X:%.1f Y:%.1f Z:%.1f A:%.1f",
            curM[0], curM[1], curM[2], curM[3]);

    // Set the position without moving
    if (!motionSetPosition(curM[0], curM[1], curM[2], curM[3])) {
        logError("[GCODE] G92 failed - axis busy or error");
        return;
    }

    logInfo("[GCODE] G92 Position set successfully");
}

// PHASE 5.1: M0/M1 - Program Stop / Pause
void GCodeParser::handleM0_M1(const char* line) {
    // M0 Mandatory Program Stop
    // M1 Optional Program Stop (skip if ignore-optional-stops is enabled)
    // Program execution pauses - operator must press resume/continue
    // Useful for: blade changes, material inspection, manual adjustments

    bool is_optional = (strchr(line, 'M') && *(strchr(line, 'M') + 1) == '1');

    if (is_optional) {
        // M1 - Optional stop
        // Could check for a configuration flag to skip optional stops
        bool ignore_optional = configGetInt("opt_stop_skip", 0);
        if (ignore_optional) {
            logInfo("[GCODE] M1 Optional stop skipped (ignore optional stops enabled)");
            return;
        }
        logInfo("[GCODE] M1 Optional Program Stop - waiting for resume");
    } else {
        // M0 - Mandatory stop
        logInfo("[GCODE] M0 Mandatory Program Stop - waiting for resume");
    }

    // Set pause state
    programPaused = true;
    pauseStartTime = millis();

    // Display message on LCD
    logPrintln("[PAUSE] Program paused - press resume to continue");
    lcdMessageSet("PAUSED: Resume?", 0);  // Stay until operator resumes

    // Pause motion control
    if (!motionPause()) {
        logError("[GCODE] Failed to pause motion");
        return;
    }

    logInfo("[GCODE] Motion paused - waiting for operator to resume");

    // Note: Resume is handled by operator pressing physical resume button
    // or sending resume command via CLI/web interface which calls motionResume()
}

bool GCodeParser::parseCode(const char* line, char code, float& value) {
    char* ptr = strchr((char*)line, code);
    if (ptr) {
        // SAFETY FIX: Use strtod instead of atof to detect parsing errors
        // atof returns 0.0 on error (e.g., "G1 X-NaN" → moves to 0, dangerous!)
        // strtod sets endptr to original string if no conversion occurred
        char* endptr = NULL;
        double parsed_value = strtod(ptr + 1, &endptr);

        // Validation checks:
        // 1. endptr must have advanced (successful parse)
        // 2. Value must not be NaN
        // 3. Value must not be infinity
        if (endptr != (ptr + 1) && !isnan(parsed_value) && !isinf(parsed_value)) {
            value = (float)parsed_value;
            return true;
        } else {
            // Parsing failed - corrupt G-code
            logError("[GCODE] Parse error: Invalid numeric value after '%c' in: %s", code, line);
            return false;
        }
    }
    return false;
}

bool GCodeParser::hasCode(const char* line, char code) {
    return strchr((char*)line, code) != NULL;
}

gcode_distance_mode_t GCodeParser::getDistanceMode() { return distanceMode; }

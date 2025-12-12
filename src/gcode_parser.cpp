/**
 * @file gcode_parser.cpp
 * @brief G-Code Parser with WCS Logic (Gemini v3.5.25)
 */

#include "gcode_parser.h"
#include "motion.h"
#include "motion_state.h"
#include "motion_buffer.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "lcd_message.h"  // PHASE 3.2: M117 LCD message support
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

GCodeParser gcodeParser;

GCodeParser::GCodeParser() : distanceMode(G_MODE_ABSOLUTE), currentFeedRate(50.0f), currentWCS(WCS_G54) {
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

void GCodeParser::getParserState(char* buffer, size_t len) {
    // Standard Grbl response string
    snprintf(buffer, len, "[GC:G%d G%d %s G94 M5]", 
        (motionIsMoving() ? 1 : 0), 
        (54 + currentWCS),
        (distanceMode == G_MODE_ABSOLUTE ? "G90" : "G91")
    );
}

bool GCodeParser::processCommand(const char* line) {
    if (!line || strlen(line) == 0) return false;
    if (line[0] == '(' || line[0] == ';') return true; 

    float val = -1.0f;
    
    // G Codes
    if (parseCode(line, 'G', val)) {
        int cmd = (int)val;
        switch (cmd) {
            case 0:
            case 1:  handleG0_G1(line); break;
            case 4:  handleG4(line); break;  // G4 Dwell
            case 10: handleG10(line); break; // G10 L20 P1...
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
            case 2:  motionStop(); break;
            case 3:  elboQ73SetRelay(ELBO_Q73_SPEED_1, true); break;
            case 5:  elboQ73SetRelay(ELBO_Q73_SPEED_1, false); break;
            // PHASE 3.2: M117 - Display message on LCD (standard gcode command)
            case 117: handleM117(line); break;
            // PHASE 4.0: M114 - Get current position
            case 114: handleM114(); break;
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
                targetM[i] = req[i] + wcs_offsets[currentWCS][i];
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
void GCodeParser::handleG92(const char* line) { logWarning("[GCODE] G92 not supported, use G10 L20"); }

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

    // Get calibration data for A axis (from hardware_config.h extern)
    extern MachineCalibration machineCal;
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

    Serial.println(response);
    logInfo("[GCODE] M114 Position: X:%.1f Y:%.1f Z:%.1f A:%.1f",
            x_mm, y_mm, z_mm, a_deg);
}

bool GCodeParser::parseCode(const char* line, char code, float& value) {
    char* ptr = strchr((char*)line, code);
    if (ptr) {
        value = atof(ptr + 1);
        return true;
    }
    return false;
}

bool GCodeParser::hasCode(const char* line, char code) {
    return strchr((char*)line, code) != NULL;
}

gcode_distance_mode_t GCodeParser::getDistanceMode() { return distanceMode; }
/**
 * @file gcode_parser.cpp
 * @brief Simple G-Code Parser
 */

#include "gcode_parser.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides motionGetPositionMM
#include "motion_buffer.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

GCodeParser gcodeParser;

GCodeParser::GCodeParser() : distanceMode(G_MODE_ABSOLUTE), currentFeedRate(50.0f) {}

void GCodeParser::init() {
    logInfo("[GCODE] Initializing Interpreter...");
    distanceMode = G_MODE_ABSOLUTE;
    currentFeedRate = 50.0f; 
    logInfo("[GCODE] Ready. Mode: Absolute, Feed: %.1f mm/s", currentFeedRate);
}

bool GCodeParser::processCommand(const char* line) {
    if (!line || strlen(line) == 0) return false;

    // Skip comments
    if (line[0] == '(' || line[0] == ';') return true; 

    // Look for G codes
    float gVal = -1.0f;
    if (parseCode(line, 'G', gVal)) {
        int cmd = (int)gVal;
        switch (cmd) {
            case 0:
            case 1:  handleG0_G1(line); break;
            case 90: handleG90(); break;
            case 91: handleG91(); break;
            case 92: handleG92(line); break;
            default: 
                logWarning("[GCODE] Unsupported G%d", cmd);
                return false;
        }
        return true;
    }

    // Look for M codes
    float mVal = -1.0f;
    if (parseCode(line, 'M', mVal)) {
        int cmd = (int)mVal;
        switch (cmd) {
            case 0:
            case 2:  motionStop(); break;
            case 112: motionEmergencyStop(); break;
            default:
                logWarning("[GCODE] Unsupported M%d", cmd);
                return false;
        }
        return true;
    }

    return false;
}

void GCodeParser::handleG0_G1(const char* line) {
    // 1. Update Feed Rate if present
    float fVal = 0.0f;
    if (parseCode(line, 'F', fVal)) {
        if (fVal > 0) currentFeedRate = fVal;
    }

    // 2. Parse Axes
    float reqX = 0, reqY = 0, reqZ = 0, reqA = 0;
    bool hasX = parseCode(line, 'X', reqX);
    bool hasY = parseCode(line, 'Y', reqY);
    bool hasZ = parseCode(line, 'Z', reqZ);
    bool hasA = parseCode(line, 'A', reqA);

    if (!hasX && !hasY && !hasZ && !hasA) return;

    // 3. Fetch Current Positions (Physical Units)
    // Needs motion_state.h included to work
    float curX = motionGetPositionMM(0);
    float curY = motionGetPositionMM(1);
    float curZ = motionGetPositionMM(2);
    float curA = motionGetPositionMM(3);

    // 4. Determine Absolute Targets
    float targetX = curX;
    float targetY = curY;
    float targetZ = curZ;
    float targetA = curA;

    if (distanceMode == G_MODE_ABSOLUTE) {
        if (hasX) targetX = reqX;
        if (hasY) targetY = reqY;
        if (hasZ) targetZ = reqZ;
        if (hasA) targetA = reqA;
    } else {
        // Relative Mode
        if (hasX) targetX += reqX;
        if (hasY) targetY += reqY;
        if (hasZ) targetZ += reqZ;
        if (hasA) targetA += reqA;
    }

    // 5. Detect Active Axes (Change > epsilon)
    bool moveX = (fabs(targetX - curX) > 0.01f);
    bool moveY = (fabs(targetY - curY) > 0.01f);
    bool moveZ = (fabs(targetZ - curZ) > 0.01f);
    bool moveA = (fabs(targetA - curA) > 0.01f);

    int activeCount = (moveX ? 1 : 0) + (moveY ? 1 : 0) + (moveZ ? 1 : 0) + (moveA ? 1 : 0);

    // 6. Serialization Logic (The Auto-Splitter)
    if (activeCount <= 1) {
        // Single axis or no move: Pass through directly
        pushMove(targetX, targetY, targetZ, targetA);
        return;
    }

    // MULTI-AXIS DETECTED: Split into sequential moves
    logInfo("[GCODE] Auto-Splitting %d-axis move...", activeCount);

    if (moveX) {
        pushMove(targetX, curY, curZ, curA); 
        curX = targetX; 
    }
    if (moveY) {
        pushMove(targetX, targetY, curZ, curA); 
        curY = targetY;
    }
    if (moveZ) {
        pushMove(targetX, targetY, targetZ, curA); 
        curZ = targetZ;
    }
    if (moveA) {
        pushMove(targetX, targetY, targetZ, targetA); 
    }
}

// Helper to push to buffer or execute directly
void GCodeParser::pushMove(float x, float y, float z, float a) {
    int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0); 
    
    if (buffer_enabled) {
        if (motionBuffer.isFull()) {
            logWarning("[GCODE] Buffer Full! Move Dropped."); 
            return;
        }
        motionBuffer.push(x, y, z, a, currentFeedRate);
    } else {
        // Direct Mode
        motionMoveAbsolute(x, y, z, a, currentFeedRate);
    }
}

void GCodeParser::handleG90() {
    distanceMode = G_MODE_ABSOLUTE;
    logInfo("[GCODE] Mode: ABSOLUTE");
}

void GCodeParser::handleG91() {
    distanceMode = G_MODE_RELATIVE;
    logInfo("[GCODE] Mode: RELATIVE");
}

void GCodeParser::handleG92(const char* line) {
    logWarning("[GCODE] G92 not fully implemented");
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
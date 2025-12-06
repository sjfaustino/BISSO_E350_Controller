#include "gcode_parser.h"
#include "motion.h"
#include "motion_buffer.h" // <-- NEW: Buffer integration
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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
    // 1. Update Feed Rate
    float fVal = 0.0f;
    if (parseCode(line, 'F', fVal)) {
        if (fVal > 0) currentFeedRate = fVal;
    }

    // 2. Parse Axes
    float x = 0, y = 0, z = 0, a = 0;
    bool hasX = parseCode(line, 'X', x);
    bool hasY = parseCode(line, 'Y', y);
    bool hasZ = parseCode(line, 'Z', z);
    bool hasA = parseCode(line, 'A', a);

    if (!hasX && !hasY && !hasZ && !hasA) return;

    // 3. Resolve Coordinates (Relative vs Absolute)
    if (distanceMode == G_MODE_ABSOLUTE) {
        // Fill missing axes with CURRENT position to maintain state
        if (!hasX) x = motionGetPositionMM(0);
        if (!hasY) y = motionGetPositionMM(1);
        if (!hasZ) z = motionGetPositionMM(2);
        if (!hasA) a = motionGetPositionMM(3);
    } else {
        // Relative: Add delta to current position
        float currX = motionGetPositionMM(0);
        float currY = motionGetPositionMM(1);
        float currZ = motionGetPositionMM(2);
        float currA = motionGetPositionMM(3);
        
        if (hasX) x = currX + x; else x = currX;
        if (hasY) y = currY + y; else y = currY;
        if (hasZ) z = currZ + z; else z = currZ;
        if (hasA) a = currA + a; else a = currA;
    }

    // 4. Execute or Buffer
    int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0); // Default OFF (Direct)
    
    if (buffer_enabled) {
        if (motionBuffer.isFull()) {
            logWarning("[GCODE] Buffer Full! Dropping cmd..."); 
            // In a real system we might block here, but in async loop we return false
            return;
        }
        motionBuffer.push(x, y, z, a, currentFeedRate);
        logInfo("[GCODE] Buffered: X%.1f Y%.1f", x, y);
    } else {
        // Direct Mode (Legacy/Manual)
        // Note: Will fail if axis busy
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

// --- Helpers ---

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
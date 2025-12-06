/**
 * @file gcode_parser.h
 * @brief Lightweight G-Code Interpreter for Gemini v1.2.0
 * @project Gemini v1.2.0
 */

#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <Arduino.h>

typedef enum {
    G_MODE_ABSOLUTE = 0,
    G_MODE_RELATIVE = 1
} gcode_distance_mode_t;

class GCodeParser {
public:
    GCodeParser();
    void init();
    
    /**
     * @brief Parse and execute a single line of G-Code
     * @param line Null-terminated string (e.g., "G1 X100 Y50 F200")
     * @return true if command was valid and accepted
     */
    bool processCommand(const char* line);

    // State Accessors
    gcode_distance_mode_t getDistanceMode();

private:
    gcode_distance_mode_t distanceMode;
    float currentFeedRate; // Stored F value (mm/s)

    // Parsing Helpers
    bool parseCode(const char* line, char code, float& value);
    bool hasCode(const char* line, char code);
    
    // Command Handlers
    void handleG0_G1(const char* line); // Linear Move
    void handleG90(); // Absolute
    void handleG91(); // Relative
    void handleG92(const char* line); // Set Position
};

extern GCodeParser gcodeParser;

#endif
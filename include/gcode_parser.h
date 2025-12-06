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
    bool processCommand(const char* line);
    gcode_distance_mode_t getDistanceMode();

private:
    gcode_distance_mode_t distanceMode;
    float currentFeedRate;

    bool parseCode(const char* line, char code, float& value);
    bool hasCode(const char* line, char code);
    
    void handleG0_G1(const char* line);
    void handleG90();
    void handleG91();
    void handleG92(const char* line);
    
    // Internal helper
    void pushMove(float x, float y, float z, float a);
};

extern GCodeParser gcodeParser;

#endif
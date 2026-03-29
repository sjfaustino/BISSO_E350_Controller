/**
 * @file gcode_parser.h
 * @brief G-Code Parser with WCS Support (PosiPro)
 */

#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <stdint.h>
#include <stddef.h> // Required for size_t

typedef enum {
    G_MODE_ABSOLUTE = 90,
    G_MODE_RELATIVE = 91
} gcode_distance_mode_t;

typedef enum {
    WCS_G54 = 0, WCS_G55, WCS_G56, WCS_G57, WCS_G58, WCS_G59
} wcs_system_t;

// Dry-run validation result
typedef struct {
    uint32_t total_lines;
    uint32_t move_count;
    uint32_t error_count;
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
    float total_distance_mm;
    char first_error[64];
} dryrun_result_t;

class GCodeParser {
public:
    GCodeParser();
    void init();

    // Core Processing
    bool processCommand(const char* line);

    // Syntax Validation
    bool validateGCodeSyntax(const char* line, char* error_msg, size_t error_msg_len);

    // Status Reporting
    gcode_distance_mode_t getDistanceMode();
    float getCurrentFeedRate();
    void getParserState(char* buffer, size_t len); // Signature fixed

    // WCS Helpers
    float getWorkPosition(uint8_t axis, float mpos);
    void getWCO(float* wco_array); 
    wcs_system_t getCurrentWCOSystem() { return currentWCS; }

    // Dry-run mode (G-code validation without motion)
    void setDryRun(bool enable);
    bool isDryRun() const { return dryRunMode; }
    dryrun_result_t getDryRunResult() const { return dryRunResult; }

private:
    gcode_distance_mode_t distanceMode;
    float currentFeedRate;
    wcs_system_t currentWCS;
    bool machineCoordinatesMode;  // G53 machine coordinates

    float wcs_offsets[6][4]; 

    bool parseCode(const char* line, char code, float& value);
    bool hasCode(const char* line, char code);
    
    bool handleG0_G1(const char* line);
    bool handleG2_G3(const char* line, bool clockwise); // G2/G3 Arc
    void handleG4(const char* line);   // G4 Dwell command
    void handleG10(const char* line);
    void handleG5x(int system_idx);
    void handleG28(const char* line);  // G28 Go to Machine Home
    void handleG30(const char* line);  // G30 Go to Predefined Position
    void handleG53(const char* line);  // G53 Machine Coordinates
    void handleG90();
    void handleG91();
    void handleG92(const char* line);  // G92 Set Position/Calibration
    // M117 LCD message handler
    void handleM117(const char* line);
    // M114 Get current position handler
    void handleM114();
    // M115 Firmware info handler
    void handleM115();
    // M154 Position auto-report handler
    void handleM154(const char* line);
    // M226 Wait for pin state handler
    void handleM226(const char* line);
    // M255 LCD sleep/backlight timeout handler
    void handleM255(const char* line);
    // M0/M1 Program Stop/Pause handler
    void handleM0_M1(const char* line);
    void handleM402(const char* line); // Coordinated Motion Mode toggle
    void handleM403(const char* line); // VFD Frequency Control

    // State for M0/M1 program pause
    bool programPaused;
    uint32_t pauseStartTime;

    void loadWCS();
    void saveWCS(uint8_t system);
    bool pushMove(float x, float y, float z, float a);

    // Dry-run state
    bool dryRunMode;
    dryrun_result_t dryRunResult;
    float dryRunLastPos[4]; // Last known position for distance calc
};

extern GCodeParser gcodeParser;

#endif

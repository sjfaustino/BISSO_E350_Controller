// hardware_config.h — Gemini v1.0.0 FINAL PRODUCTION VERSION

#pragma once
#include <Arduino.h>
#include <cstdint>

// Forward declaration for CLI types
typedef void (*cli_handler_t)(int argc, char** argv);
typedef struct cli_command_t cli_command_t; 

enum class BoardType : uint8_t { UNKNOWN = 0, A16, A32 };
BoardType detectBoard();
extern const BoardType BOARD;

struct PinInfo {
    int8_t   gpio;
    const char* silk;
    const char* type;
    const char* voltage;
    const char* current;
    const char* note;
};

constexpr PinInfo pinDatabase[] = {
    {0,  "X1",  "input",  "5-24V",  "10mA", "Opto input"},
    {1,  "X2",  "input",  "5-24V",  "10mA", "Opto input"},
    {2,  "X3",  "input",  "5-24V",  "10mA", "Opto input"},
    {3,  "X4",  "input",  "5-24V",  "10mA", "Opto input"},
    {4,  "X5",  "input",  "5-24V",  "10mA", "Opto input"},
    {5,  "X6",  "input",  "5-24V",  "10mA", "Opto input"},
    {6,  "X7",  "input",  "5-24V",  "10mA", "Opto input"},
    {7,  "X8",  "input",  "5-24V",  "10mA", "Opto input"},
    {8,  "X9",  "input",  "5-24V",  "10mA", "Opto input"},
    {9,  "X10", "input",  "5-24V",  "10mA", "Opto input"},
    {10, "X11", "input",  "5-24V",  "10mA", "Opto input"},
    {11, "X12", "input",  "5-24V",  "10mA", "Opto input"},
    {12, "X13", "input",  "5-24V",  "10mA", "Opto input"},
    {13, "X14", "input",  "5-24V",  "10mA", "Opto input"},
    {14, "X15", "input",  "5-24V",  "10mA", "Opto input"},
    {15, "X16", "input",  "5-24V",  "10mA", "Opto input"},

    {16, "Y1",  "output", "12-30V", "10A", "Output"},
    {17, "Y2",  "output", "12-30V", "10A", "Output"},
    {18, "Y3",  "output", "12-30V", "10A", "Output"},
    {19, "Y4",  "output", "12-30V", "10A", "Output"},
    {20, "Y5",  "output", "12-30V", "10A", "Output"},
    {21, "Y6",  "output", "12-30V", "10A", "Output"},
    {22, "Y7",  "output", "12-30V", "10A", "Output"},
    {23, "Y8",  "output", "12-30V", "10A", "Output"},
    {24, "Y9",  "output", "12-30V", "10A", "A32 only"},
    {25, "Y10", "output", "12-30V", "10A", "A32 only"},
    {26, "Y11", "output", "12-30V", "10A", "A32 only"},
    {27, "Y12", "output", "12-30V", "10A", "A32 only"},
    {28, "Y13", "output", "12-30V", "10A", "A32 only"},
    {29, "Y14", "output", "12-30V", "10A", "A32 only"},
    {30, "Y15", "output", "12-30V", "10A", "A32 only"},
    {31, "Y16", "output", "12-30V", "10A", "A32 only"},

    {14, "HT1", "both", "3.3V", "5mA", "GPIO14 – WJ66 RX"},
    {33, "HT2", "both", "3.3V", "5mA", "GPIO33 – WJ66 TX"},
    {32, "HT3", "both", "3.3V", "5mA", "GPIO32 – Free"},
    {16, "RS485_RX", "input", "3.3V", "5mA", "GPIO16 – RS485 RXD"},
    {13, "RS485_TX", "output", "3.3V", "5mA", "GPIO13 – RS485 TXD"},
    {37, "IN1", "input", "0-20mA", "20mA", "Current loop"},
    {38, "IN2", "input", "0-20mA", "20mA", "Current loop"},
    {39, "IN3", "input", "0-5V", "1mA", "Analog 0-5V"},
    {40, "IN4", "input", "0-5V", "1mA", "Analog 0-5V"}
};

constexpr size_t PIN_COUNT = sizeof(pinDatabase)/sizeof(pinDatabase[0]);

struct SignalDef {
    const char* key;
    const char* name;
    const char* desc;
    int8_t     default_gpio;
    const char* type;
};

const SignalDef signalDefinitions[] = {
    {"input_c",        "PLC Input C",        "C mode consenso",        0, "input"},
    {"input_t",        "PLC Input T",        "T mode consenso",        1, "input"},
    {"input_ct",       "PLC Input C+T",      "C+T mode consenso",       2, "input"},
    {"input_manual",   "PLC Input Manual",   "Manual mode",           3, "input"},
    {"input_estop",    "E-Stop Input",       "Emergency stop",        4, "input"},
    {"input_pause",    "Pause Button",       "Pause operation",       5, "input"},
    {"input_resume",   "Resume Button",      "Resume operation",      6, "input"},

    {"output_axis_x",     "Axis X Select",     "Select X axis",        16, "output"},
    {"output_axis_y",     "Axis Y Select",     "Select Y axis",        17, "output"},
    {"output_axis_z",     "Axis Z Select",     "Select Z axis",        18, "output"},
    {"output_dir_plus",   "Direction +",       "Positive direction",          19, "output"},
    {"output_dir_minus",  "Direction -",       "Negative direction",    20, "output"},
    {"output_speed_fast", "Speed Fast",        "Fast speed",           21, "output"},
    {"output_speed_med",  "Speed Medium",      "Medium speed",        22, "output"},
    {"output_speed_slow", "Speed Slow",        "Slow speed",           23, "output"}
};

constexpr size_t SIGNAL_COUNT = sizeof(signalDefinitions)/sizeof(signalDefinitions[0]);

// Per-axis calibration storage
struct AxisCalibration {
    float pulses_per_mm      = 0.0f;
    float pulses_per_degree  = 0.0f;
    float speed_slow_mm_min  = 300.0f;
    float speed_med_mm_min   = 900.0f;
    float speed_fast_mm_min  = 2400.0f;
    float backlash_mm        = 0.0f;
    float pitch_error        = 1.0000f;
};

struct MachineCalibration {
    AxisCalibration X;
    AxisCalibration Y;
    AxisCalibration Z;
    AxisCalibration A;
};

extern MachineCalibration machineCal;

// Helper functions
const PinInfo* getPinInfo(int8_t gpio);
const char* checkPinConflict(int8_t gpio, const char* currentKey = nullptr);
bool setPin(const char* key, int8_t gpio);
int8_t getPin(const char* key);
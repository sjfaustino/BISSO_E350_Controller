// hardware_config.h — BISSO E350 Controller Hardware Abstraction

#pragma once
#include <Arduino.h>
#include <cstdint>
#include "board_variant.h"  // Board-specific GPIO definitions

// Forward declaration for CLI types
typedef void (*cli_handler_t)(int argc, char** argv);
typedef struct cli_command_t cli_command_t; 

enum class BoardType : uint8_t { UNKNOWN = 0, A16, A32 };
BoardType detectBoard();
extern const BoardType BOARD;

struct PinInfo {
    int16_t  gpio;  // Changed to int16_t for virtual pins 100+
    const char* silk;
    const char* type;
    const char* voltage;
    const char* current;
    const char* note;
};

constexpr PinInfo pinDatabase[] = {
    // I2C Expander Inputs (Virtual 100-115) - X1-X16 on KC868-A16
    {100, "X1",  "input",  "5-24V",  "10mA", "Opto input"},
    {101, "X2",  "input",  "5-24V",  "10mA", "Opto input"},
    {102, "X3",  "input",  "5-24V",  "10mA", "Opto input"},
    {103, "X4",  "input",  "5-24V",  "10mA", "Opto input"},
    {104, "X5",  "input",  "5-24V",  "10mA", "Opto input"},
    {105, "X6",  "input",  "5-24V",  "10mA", "Opto input"},
    {106, "X7",  "input",  "5-24V",  "10mA", "Opto input"},
    {107, "X8",  "input",  "5-24V",  "10mA", "Opto input"},
    {108, "X9",  "input",  "5-24V",  "10mA", "Opto input"},
    {109, "X10", "input",  "5-24V",  "10mA", "Opto input"},
    {110, "X11", "input",  "5-24V",  "10mA", "Opto input"},
    {111, "X12", "input",  "5-24V",  "10mA", "Opto input"},
    {112, "X13", "input",  "5-24V",  "10mA", "Opto input"},
    {113, "X14", "input",  "5-24V",  "10mA", "Opto input"},
    {114, "X15", "input",  "5-24V",  "10mA", "Opto input"},
    {115, "X16", "input",  "5-24V",  "10mA", "Opto input"},

    // I2C Expander Outputs (Virtual 116-131) - Y1-Y16 on KC868-A16
    {116, "Y1",  "output", "12-30V", "10A", "Relay output"},
    {117, "Y2",  "output", "12-30V", "10A", "Relay output"},
    {118, "Y3",  "output", "12-30V", "10A", "Relay output"},
    {119, "Y4",  "output", "12-30V", "10A", "Relay output"},
    {120, "Y5",  "output", "12-30V", "10A", "Relay output"},
    {121, "Y6",  "output", "12-30V", "10A", "Relay output"},
    {122, "Y7",  "output", "12-30V", "10A", "Relay output"},
    {123, "Y8",  "output", "12-30V", "10A", "Relay output"},
    {124, "Y9",  "output", "12-30V", "10A", "A32 only"},
    {125, "Y10", "output", "12-30V", "10A", "A32 only"},
    {126, "Y11", "output", "12-30V", "10A", "A32 only"},
    {127, "Y12", "output", "12-30V", "10A", "A32 only"},
    {128, "Y13", "output", "12-30V", "10A", "A32 only"},
    {129, "Y14", "output", "12-30V", "10A", "A32 only"},
    {130, "Y15", "output", "12-30V", "10A", "A32 only"},
    {131, "Y16", "output", "12-30V", "10A", "A32 only"},

    // Direct GPIO pins (board-variant specific, see board_variant.h)
    {PIN_1WIRE_HT1, "HT1", "both", "3.3V", "5mA", "1-Wire / WJ66 RX"},
    {PIN_1WIRE_HT2, "HT2", "both", "3.3V", "5mA", "1-Wire / WJ66 TX"},
    {PIN_1WIRE_HT3, "HT3", "both", "3.3V", "5mA", "1-Wire / Free"},
    {PIN_RS485_RX, "RS485_A", "input", "3.3V", "5mA", "RS485 RXD"},
    {PIN_RS485_TX, "RS485_B", "output", "3.3V", "5mA", "RS485 TXD"},

    // Analog channels (board-variant specific)
    {PIN_ANALOG_CH1, "CH1", "analog", "0-20mA", "20mA", "Current loop ADC"},
    {PIN_ANALOG_CH2, "CH2", "analog", "0-20mA", "20mA", "Current loop ADC"},
    {PIN_ANALOG_CH3, "CH3", "analog", "0-5V",   "1mA",  "Voltage ADC"},
    {PIN_ANALOG_CH4, "CH4", "analog", "0-5V",   "1mA",  "Voltage ADC"}
};

constexpr size_t PIN_COUNT = sizeof(pinDatabase)/sizeof(pinDatabase[0]);

struct SignalDef {
    const char* key;
    const char* name;
    const char* desc;
    int16_t     default_gpio;
    const char* type;
    const char* nvs_key; // Short key for NVS (max 15 chars)
};

const SignalDef signalDefinitions[] = {
    // PLC Inputs (X1-X16, Virtual 100-115)
    {"input_c",        "PLC Input C",        "C mode consenso",       100, "input", "i_c"},
    {"input_t",        "PLC Input T",        "T mode consenso",       101, "input", "i_t"},
    {"input_ct",       "PLC Input C+T",      "C+T mode consenso",     102, "input", "i_ct"},
    {"input_manual",   "PLC Input Manual",   "Manual mode",           103, "input", "i_man"},
    {"input_estop",    "E-Stop Button",      "Emergency stop",        104, "input", "i_estop"},
    {"input_pause",    "Pause Button",       "Pause operation",       105, "input", "i_pause"},
    {"input_resume",   "Resume Button",      "Resume operation",      106, "input", "i_resume"},

    // PLC Outputs (Y1-Y16, Virtual 116-131)
    {"output_axis_x",     "Axis X Select",     "Select X axis",        116, "output", "o_axis_x"},
    {"output_axis_y",     "Axis Y Select",     "Select Y axis",        117, "output", "o_axis_y"},
    {"output_axis_z",     "Axis Z Select",     "Select Z axis",        118, "output", "o_axis_z"},
    {"output_dir_plus",   "Direction +",       "Positive direction",   119, "output", "o_dir_p"},
    {"output_dir_minus",  "Direction -",       "Negative direction",   120, "output", "o_dir_m"},
    {"output_speed_fast", "Speed Fast",        "Fast speed",           121, "output", "o_spd_fst"},
    {"output_speed_med",  "Speed Medium",      "Medium speed",         122, "output", "o_spd_med"},
    {"output_speed_slow", "Speed Slow",        "Slow speed",           123, "output", "o_spd_slo"},
    
    // Status Light (Tower Light)
    {"output_status_green",  "Status Light Green",  "Status light green",   124, "output", "sl_green"},
    {"output_status_yellow", "Status Light Yellow", "Status light yellow",  125, "output", "sl_yellow"},
    {"output_status_red",    "Status Light Red",    "Status light red",     126, "output", "sl_red"},
    {"output_buzzer",        "Buzzer",            "Audible alarm",        127, "output", "buzzer_pin"},

    // Auxiliary Peripherals (Y13-Y16, Virtual 128-131)
    {"output_coolant",       "Coolant Relay",     "Flood coolant control", 128, "output", "o_cool"},
    {"output_vacuum",        "Vacuum Relay",      "Vacuum/Dust control",   129, "output", "o_vac"},

    // WJ66 Encoder (RS232 -> RS485 Converter)
    {"wj66_rx", "WJ66 RX", "Encoder RX", 16, "input", "wj66_rx"},
    {"wj66_tx", "WJ66 TX", "Encoder TX", 13, "output", "wj66_tx"}
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
    AxisCalibration axes[4]; // 0=X, 1=Y, 2=Z, 3=A
};

extern MachineCalibration machineCal;

// Helper functions
const PinInfo* getPinInfo(int16_t gpio);
const char* checkPinConflict(int16_t gpio, const char* currentKey = nullptr);
bool setPin(const char* key, int16_t gpio, bool skip_save = false);
int16_t getPin(const char* key);

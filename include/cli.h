#ifndef CLI_H
#define CLI_H

#include <Arduino.h>

#define CLI_BUFFER_SIZE 512
#define CLI_HISTORY_SIZE 10
#define CLI_MAX_ARGS 16
#define CLI_MAX_COMMANDS 128

// ============================================================================
// P3 DRY: CLI Usage Helper Macros
// Provides consistent formatting for command help and usage messages
// ============================================================================

/**
 * @brief Print a usage line for a command
 * @param cmd Command name (e.g., "config")
 * @param usage Usage pattern (e.g., "get <key> | set <key> <value>")
 */
#define CLI_USAGE(cmd, usage) logPrintln("[" cmd "] Usage: " cmd " " usage)

/**
 * @brief Print a formatted help line for a subcommand
 * @param subcmd Subcommand name (e.g., "get")
 * @param desc Description of the subcommand
 */
#define CLI_HELP_LINE(subcmd, desc) logPrintf("  %-12s %s\r\n", subcmd, desc)

/**
 * @brief Print an example command line
 * @param example Example command (e.g., "config get wifi_ssid")
 */
#define CLI_EXAMPLE(example) logPrintln("  Example: " example)

// Definition of the shared command structure
typedef void (*cli_handler_t)(int argc, char** argv);

typedef struct cli_command_t { 
  const char* command;
  const char* help;
  cli_handler_t handler;
} cli_command_t;


// ============================================================================
// CORE CLI FUNCTIONS
// ============================================================================
void cliInit();
void cliUpdate();
void cliProcessCommand(const char* cmd);
void cliPrintHelp();
bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler);

// --- Table Rendering Helpers (Box Drawing) ---
void cliPrintTableHeader(int w1, int w2, int w3, int w4 = 0, int w5 = 0);
void cliPrintTableDivider(int w1, int w2, int w3, int w4 = 0, int w5 = 0);
void cliPrintTableFooter(int w1, int w2, int w3, int w4 = 0, int w5 = 0);
void cliPrintTableRow(const char* c1, const char* c2, const char* c3, 
                      int w1, int w2, int w3,
                      const char* c4 = nullptr, int w4 = 0,
                      const char* c5 = nullptr, int w5 = 0);

// --- Subcommand Dispatch Helper (DRY pattern for nested commands) ---
typedef struct {
    const char* name;
    cli_handler_t handler;
    const char* help;
} cli_subcommand_t;

/**
 * @brief Table-driven subcommand dispatcher
 * @param prefix Prefix for error messages (e.g., "[SPINDLE]")
 * @param argc Argument count from parent command
 * @param argv Argument values from parent command
 * @param table Array of subcommand definitions
 * @param table_size Number of entries in table
 * @param arg_index Which argv index contains the subcommand (typically 1 or 2)
 * @return true if subcommand was found and executed
 */
bool cliDispatchSubcommand(const char* prefix, int argc, char** argv,
                           const cli_subcommand_t* table, size_t table_size,
                           int arg_index = 1);

// ============================================================================
// MODULE REGISTRATION FUNCTIONS
// ============================================================================
void cliRegisterConfigCommands();
void cliRegisterMotionCommands();
void cliRegisterDiagCommands();
void cliRegisterI2CCommands();
void cliRegisterCalibCommands();
void cliRegisterWifiCommands(); 

// ============================================================================
// COMMAND HANDLER PROTOTYPES
// ============================================================================

// --- General / System (cli_base.cpp) ---
void cmd_system_info(int argc, char** argv);
void cmd_system_reset(int argc, char** argv);
void cmd_help(int argc, char** argv);

// --- Motion / Safety (cli_motion.cpp) ---
// Note: Standard move commands are typically local to cli_motion.cpp
void cmd_estop_status(int argc, char** argv);
void cmd_estop_on(int argc, char** argv);
void cmd_estop_off(int argc, char** argv);

// --- Diagnostics & Hardware (cli_diag.cpp) ---
// Consolidated Dispatchers
void cmd_faults_main(int argc, char** argv);        // 'faults' command
void cmd_encoder_main(int argc, char** argv);       // 'encoder' command
void cmd_debug_main(int argc, char** argv);         // 'debug' command
void cmd_diag_scheduler_main(int argc, char** argv);// 'wdt' and 'task' dispatcher

// --- I2C Management (cli_i2c.cpp) ---
void cmd_i2c_main(int argc, char** argv);           // 'i2c' command

// Standalone Diagnostic Commands
void cmd_timeout_diag(int argc, char** argv);
void cmd_encoder_set_baud(int argc, char** argv);

// --- Configuration (cli_config.cpp) ---
void cmd_config_main(int argc, char** argv);        // 'config' command

// --- Network (cli_wifi.cpp) ---
void cmd_wifi_main(int argc, char** argv);          // 'wifi' command

// --- VFD Calibration (cli_calib.cpp) - PHASE 5.5 ---
void cmd_vfd_calib_current(int argc, char** argv);  // 'calibrate vfd current' command
void cmd_vfd_diagnostics(int argc, char** argv);    // 'vfd diagnostics' command
void cmd_vfd_config(int argc, char** argv);         // 'vfd config' command

#endif // CLI_H

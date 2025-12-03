#ifndef CLI_H
#define CLI_H

#include <Arduino.h>

#define CLI_BUFFER_SIZE 512
#define CLI_HISTORY_SIZE 10
#define CLI_MAX_ARGS 16
#define CLI_MAX_COMMANDS 128

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
void cliCleanup();
void cliUpdate();
void cliProcessCommand(const char* cmd);
void cliPrintHelp();
void cliPrintPrompt();
bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler);
int cliGetCommandCount();

// ============================================================================
// MODULE REGISTRATION FUNCTIONS
// ============================================================================
void cliRegisterConfigCommands();
void cliRegisterMotionCommands();
void cliRegisterDiagCommands();
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
void cmd_i2c_main(int argc, char** argv);           // 'i2c' command
void cmd_encoder_main(int argc, char** argv);       // 'encoder' command
void cmd_debug_main(int argc, char** argv);         // 'debug' command
void cmd_diag_scheduler_main(int argc, char** argv);// 'wdt' and 'task' dispatcher

// Standalone Diagnostic Commands
void cmd_timeout_diag(int argc, char** argv);
void cmd_encoder_set_baud(int argc, char** argv);

// --- Configuration (cli_config.cpp) ---
void cmd_config_main(int argc, char** argv);        // 'config' command

// --- Network (cli_wifi.cpp) ---
void cmd_wifi_main(int argc, char** argv);          // 'wifi' command

#endif // CLI_H
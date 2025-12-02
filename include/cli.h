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


// Main functions
void cliInit();
void cliCleanup();
void cliUpdate();
void cliProcessCommand(const char* cmd);
void cliPrintHelp();
void cliPrintPrompt();
bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler);
int cliGetCommandCount();

// Registration functions for modular files
void cliRegisterConfigCommands();
void cliRegisterMotionCommands();
void cliRegisterDiagCommands();
void cliRegisterCalibCommands();

// --- General Command Prototypes (Implemented in cli_base.cpp) ---
void cmd_system_info(int argc, char** argv);
void cmd_system_reset(int argc, char** argv);
void cmd_help(int argc, char** argv);

// --- E-Stop Command Prototypes (Implemented in cli_motion.cpp) ---
void cmd_estop_status(int argc, char** argv);
void cmd_estop_on(int argc, char** argv);
void cmd_estop_off(int argc, char** argv);

// --- Diagnostic Command Prototypes (Implemented in cli_diag.cpp) ---
void cmd_debug_main(int argc, char** argv); 
void cmd_faults_show(int argc, char** argv);
void cmd_faults_stats(int argc, char** argv); 
void cmd_faults_clear(int argc, char** argv); 
void cmd_timeout_diag(int argc, char** argv);
void cmd_i2c_diag(int argc, char** argv);
void cmd_i2c_recover(int argc, char** argv);
void cmd_encoder_diag(int argc, char** argv);
void cmd_encoder_baud_detect(int argc, char** argv);
void cmd_wdt_main(int argc, char** argv); 
void cmd_task_main(int argc, char** argv); 

// --- Configuration Diagnostic Prototypes (Implemented in cli_config.cpp, referenced in cli_diag.cpp) ---
void cmd_config_main(int argc, char** argv); // NEW: Consolidated Config Dispatcher

void cmd_config_show(int argc, char** argv);
void cmd_config_reset(int argc, char** argv);
void cmd_config_save(int argc, char** argv);

#endif
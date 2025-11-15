#ifndef CLI_H
#define CLI_H

#include <Arduino.h>

#define CLI_BUFFER_SIZE 512
#define CLI_HISTORY_SIZE 10
#define CLI_MAX_ARGS 16
#define CLI_MAX_COMMANDS 128

typedef void (*cli_handler_t)(int argc, char** argv);

typedef struct {
  const char* command;
  const char* help;
  cli_handler_t handler;
} cli_command_t;

void cliInit();
void cliCleanup();
void cliUpdate();
void cliProcessCommand(const char* cmd);
void cliPrintHelp();
void cliPrintPrompt();
bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler);
int cliGetCommandCount();

#endif

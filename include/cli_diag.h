/**
 * @file cli_diag.h
 * @brief Shared declarations for diagnostic CLI handlers
 */

#ifndef CLI_DIAG_H
#define CLI_DIAG_H

#include "cli.h"

// --- Motion Diagnostics (cli_diag_motion.cpp) ---
void cmd_encoder_status(int argc, char** argv);
void cmd_encoder_read(int argc, char** argv);
void cmd_encoder_diag(int argc, char** argv);
void cmd_encoder_test(int argc, char** argv);
void cmd_encoder_baud_detect(int argc, char** argv);
void cmd_encoder_deviation_diag(int argc, char** argv);
void cmd_axis_sync_diag(int argc, char** argv);

// --- Network Diagnostics (cli_diag_network.cpp) ---
void cmd_net_diag(int argc, char** argv);

// --- Hardware Diagnostics (cli_diag_hardware.cpp) ---
void cmd_dio_main(int argc, char** argv);
void cmd_spindle_diag(int argc, char** argv);
void cmd_spindle_alarm(int argc, char** argv);
void cmd_rs485_raw(int argc, char** argv);
void cmd_rs485_hex(int argc, char** argv);
void cmd_rs485_diag(int argc, char** argv);
void cmd_rs485_reset(int argc, char** argv);
void cmd_i2c_diag(int argc, char** argv);

// --- System Diagnostics (cli_diag.cpp) ---
void cmd_status_dashboard(int argc, char** argv);
void cmd_runtime(int argc, char** argv);
void cmd_memory_main(int argc, char** argv);
void cmd_faults_main(int argc, char** argv);
void cmd_selftest(int argc, char** argv);
void cmd_wdt_main(int argc, char** argv);
void cmd_task_main(int argc, char** argv);
void cmd_memory_leak_check(int argc, char** argv);
void cmd_diag_summary(int argc, char** argv);

#endif // CLI_DIAG_H

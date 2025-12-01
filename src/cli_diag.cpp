#include "cli.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "boot_validation.h"
#include "encoder_wj66.h"
#include "encoder_comm_stats.h"
#include "i2c_bus_recovery.h"
#include "task_manager.h"
#include "watchdog_manager.h"
#include "timeout_manager.h"
#include "config_schema_versioning.h"
#include "config_validator.h"
#include "memory_monitor.h"
#include "plc_iface.h"
#include "motion.h" // For motionDiagnostics
#include "safety.h" // For safetyDiagnostics
#include "encoder_calibration.h" // For encoderCalibrationDiagnostics
#include "encoder_motion_integration.h" // For encoderMotionDiagnostics
#include <stdlib.h>

// ============================================================================
// FORWARD DECLARATIONS (Local to this module)
// ============================================================================

void cmd_debug_all(int argc, char** argv);
void cmd_safety_status(int argc, char** argv);
void cmd_plc_status(int argc, char** argv);
void cmd_i2c_diag(int argc, char** argv);
void cmd_i2c_recover(int argc, char** argv);
void cmd_encoder_diag(int argc, char** argv);
void cmd_encoder_baud_detect(int argc, char** argv);
void cmd_task_stats(int argc, char** argv);
void cmd_task_list(int argc, char** argv);
void cmd_task_cpu(int argc, char** argv);
void cmd_wdt_status(int argc, char** argv);
void cmd_wdt_tasks(int argc, char** argv);
void cmd_wdt_stats(int argc, char** argv);
void cmd_wdt_report(int argc, char** argv);
void cmd_memory_diag(int argc, char** argv);
void cmd_fault_show(int argc, char** argv);
void cmd_fault_clear(int argc, char** argv);
void cmd_timeout_diag(int argc, char** argv);


// ============================================================================
// REGISTRATION (Defines cliRegisterDiagCommands())
// ============================================================================

void cliRegisterDiagCommands() {
  cliRegisterCommand("debug", "Show all diagnostics", cmd_debug_all);
  cliRegisterCommand("safety", "Safety status", cmd_safety_status);
  cliRegisterCommand("plc", "PLC status", cmd_plc_status);
  cliRegisterCommand("faults", "Show fault history", cmd_fault_show);
  cliRegisterCommand("faults_clear", "Clear fault history", cmd_fault_clear);
  cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
  cliRegisterCommand("i2c_diag", "Show I²C diagnostics", cmd_i2c_diag);
  cliRegisterCommand("i2c_recover", "Recover I²C bus", cmd_i2c_recover);
  cliRegisterCommand("encoder_diag", "Show encoder diagnostics", cmd_encoder_diag);
  cliRegisterCommand("encoder_baud", "Auto-detect encoder baud rate", cmd_encoder_baud_detect);
  cliRegisterCommand("task_stats", "Show FreeRTOS task statistics", cmd_task_stats);
  cliRegisterCommand("task_list", "List all FreeRTOS tasks", cmd_task_list);
  cliRegisterCommand("task_cpu", "Show CPU usage", cmd_task_cpu);
  cliRegisterCommand("wdt_status", "Show watchdog status", cmd_wdt_status);
  cliRegisterCommand("wdt_tasks", "List monitored tasks", cmd_wdt_tasks);
  cliRegisterCommand("wdt_stats", "Show watchdog statistics", cmd_wdt_stats);
  cliRegisterCommand("wdt_report", "Detailed watchdog report", cmd_wdt_report);
  cliRegisterCommand("mem_diag", "Show memory diagnostics", cmd_memory_diag);
}

// ============================================================================
// COMMAND IMPLEMENTATIONS (Rely on defined modules)
// ============================================================================

void cmd_debug_all(int argc, char** argv) {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║                FULL SYSTEM DIAGNOSTICS DUMP                  ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  bootShowDetailedReport();
  motionDiagnostics();
  wj66Diagnostics();
  encoderCalibrationDiagnostics();
  plcDiagnostics();
  i2cShowStats();
  timeoutShowDiagnostics();
  faultShowHistory();
  watchdogPrintDetailedReport();
  memoryMonitorPrintStats();
  taskShowAllTasks();
  // configValidatorPrintReport(); // Assuming config_validator.h is included where needed
  
  Serial.println("\n✅ Diagnostics Dump Complete.");
}

void cmd_safety_status(int argc, char** argv) {
  safetyDiagnostics();
}

void cmd_plc_status(int argc, char** argv) {
  plcDiagnostics();
}

void cmd_fault_show(int argc, char** argv) {
  faultShowHistory();
}

void cmd_fault_clear(int argc, char** argv) {
  faultClearHistory();
}

void cmd_timeout_diag(int argc, char** argv) {
  timeoutShowDiagnostics();
}

void cmd_i2c_diag(int argc, char** argv) {
  i2cShowStats();
}

void cmd_i2c_recover(int argc, char** argv) {
  Serial.println("[CLI] Attempting I²C bus recovery...");
  i2cRecoverBus();
  Serial.println("[CLI] ✅ I²C bus recovery complete. Check status with i2c_diag.");
}

void cmd_encoder_diag(int argc, char** argv) {
  wj66Diagnostics();
  encoderCalibrationDiagnostics();
  encoderMotionDiagnostics();
}

void cmd_encoder_baud_detect(int argc, char** argv) {
  Serial.println("[CLI] Starting encoder baud rate detection...");
  baud_detect_result_t result = encoderDetectBaudRate();
  if (result.detected) {
      Serial.print("[CLI] ✅ Detected Baud Rate: ");
      Serial.println(result.baud_rate);
  } else {
      Serial.println("[CLI] ❌ Baud rate detection failed. Defaulting to 9600.");
  }
}

void cmd_task_stats(int argc, char** argv) {
  taskShowStats();
}

void cmd_task_list(int argc, char** argv) {
  taskShowAllTasks();
}

void cmd_task_cpu(int argc, char** argv) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║       FreeRTOS CPU USAGE              ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.print("[TASKS] CPU Usage: ");
  Serial.print(taskGetCpuUsage());
  Serial.println("%");
  
  Serial.print("[TASKS] System Uptime: ");
  Serial.print(taskGetUptime());
  Serial.println(" seconds\n");
}

void cmd_wdt_status(int argc, char** argv) {
  watchdogShowStatus();
}

void cmd_wdt_tasks(int argc, char** argv) {
  watchdogShowTasks();
}

void cmd_wdt_stats(int argc, char** argv) {
  watchdogShowStats();
}

void cmd_wdt_report(int argc, char** argv) {
  watchdogPrintDetailedReport();
}

void cmd_memory_diag(int argc, char** argv) {
  memoryMonitorPrintStats();
}
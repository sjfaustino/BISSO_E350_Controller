#include "board_inputs.h"
#include "boot_validation.h"
#include "cli.h"
#include "config_schema_versioning.h"
#include "config_unified.h"
#include "encoder_calibration.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include "firmware_version.h"
#include "lcd_interface.h"
#include "motion.h"
#include "plc_iface.h"
#include "safety.h"
#include "serial_logger.h" 
#include "system_constants.h"
#include "task_manager.h"
#include "task_performance_monitor.h"  // PHASE 5.1: Task performance monitoring
#include "config_validator_schema.h"  // PHASE 5.2: Configuration schema validation
#include "timeout_manager.h"
#include "watchdog_manager.h"
#include "web_server.h"
#include "network_manager.h"
#include "encoder_diagnostics.h"  // PHASE 5.3: Advanced encoder diagnostics
#include "load_manager.h"  // PHASE 5.3: Graceful degradation under load
#include "dashboard_metrics.h"  // PHASE 5.3: Web UI dashboard metrics
#include "axis_synchronization.h"  // PHASE 5.6: Axis synchronization validation
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static uint32_t boot_time_ms = 0;
extern WebServerManager webServer;

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  static volatile bool handling = false;
  if (handling) return;
  handling = true;
  Serial.println("\n\n[CRITICAL] STACK OVERFLOW");
  faultLogCritical(FAULT_CRITICAL_SYSTEM_ERROR, "Stack Overflow");
  delay(1000);
  ESP.restart();
}

// Wrappers
bool init_fault_logging_wrapper() { faultLoggingInit(); return true; }
bool init_watchdog_wrapper() { watchdogInit(); return true; }
bool init_timeout_wrapper() { timeoutManagerInit(); return true; }
bool init_config_wrapper() { configUnifiedInit(); return true; }
bool init_schema_wrapper() { configSchemaVersioningInit(); configSchemaInit(); return !configIsMigrationNeeded(); }
bool init_calib_wrapper() { loadAllCalibration(); encoderCalibrationInit(); return true; }

// UPDATED: Using correct init function
bool init_plc_wrapper() { elboInit(); return true; }

bool init_lcd_wrapper() { lcdInterfaceInit(); return true; }
bool init_enc_wrapper() { wj66Init(); return true; }
bool init_safety_wrapper() { safetyInit(); return true; }
bool init_motion_wrapper() { motionInit(); return true; }
bool init_cli_wrapper() { cliInit(); return true; }
bool init_inputs_wrapper() { boardInputsInit(); return true; }
bool init_network_wrapper() { networkManager.init(); webServer.init(); webServer.begin(); return true; }

// PHASE 5.3: Initialize advanced diagnostics and load management
bool init_encoder_diag_wrapper() { encoderDiagnosticsInit(); return true; }
bool init_load_mgr_wrapper() { loadManagerInit(); return true; }
bool init_dashboard_wrapper() { dashboardMetricsInit(); return true; }

// PHASE 5.6: Initialize axis synchronization validation
bool init_axis_sync_wrapper() { axisSynchronizationInit(); return true; }

#define BOOT_INIT(name, func, code) \
  do { if (func()) { logInfo("[BOOT] Init %s [OK]", name); bootMarkInitialized(name); } \
       else { logError("[BOOT] Init %s [FAIL]", name); bootMarkFailed(name, "Init failed", code); } } while (0)

void setup() {
  Serial.begin(115200);
  delay(1000);
  serialLoggerInit(LOG_LEVEL);
  boot_time_ms = millis();

  char ver_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(ver_str, sizeof(ver_str));
  logInfo("=== %s STARTING ===", ver_str);

  bootValidationInit();

  BOOT_INIT("Fault Log", init_fault_logging_wrapper, BOOT_ERROR_FAULT_LOGGING);
  BOOT_INIT("Watchdog", init_watchdog_wrapper, BOOT_ERROR_WATCHDOG);
  BOOT_INIT("Config", init_config_wrapper, BOOT_ERROR_CONFIG);
  BOOT_INIT("Schema", init_schema_wrapper, BOOT_ERROR_SCHEMA);
  BOOT_INIT("PLC", init_plc_wrapper, BOOT_ERROR_PLC_IFACE);
  BOOT_INIT("LCD", init_lcd_wrapper, (boot_status_code_t)19);
  BOOT_INIT("Inputs", init_inputs_wrapper, (boot_status_code_t)14);
  BOOT_INIT("Encoder", init_enc_wrapper, BOOT_ERROR_ENCODER);
  BOOT_INIT("Safety", init_safety_wrapper, BOOT_ERROR_SAFETY);
  BOOT_INIT("Motion", init_motion_wrapper, BOOT_ERROR_MOTION);
  BOOT_INIT("CLI", init_cli_wrapper, BOOT_ERROR_CLI);
  BOOT_INIT("Network", init_network_wrapper, (boot_status_code_t)13);
  BOOT_INIT("Encoder Diag", init_encoder_diag_wrapper, (boot_status_code_t)15);
  BOOT_INIT("Load Manager", init_load_mgr_wrapper, (boot_status_code_t)16);
  BOOT_INIT("Dashboard", init_dashboard_wrapper, (boot_status_code_t)17);
  BOOT_INIT("Axis Sync", init_axis_sync_wrapper, (boot_status_code_t)18);

  logInfo("[BOOT] Validating system health...");
  if (!bootValidateAllSystems()) {
    bootHandleCriticalError("Boot validation failed.");
    return;
  }

  taskManagerInit();
  perfMonitorInit();  // PHASE 5.1: Initialize performance monitoring
  taskManagerStart();
  logInfo("[BOOT] [OK] Complete in %lu ms", (unsigned long)(millis() - boot_time_ms));
}

void loop() {
  networkManager.update();
  delay(10); 
}
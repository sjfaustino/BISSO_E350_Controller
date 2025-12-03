#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "system_constants.h"
#include "config_unified.h"
#include "motion.h"
#include "encoder_wj66.h"
#include "encoder_calibration.h" 
#include "plc_iface.h"
#include "safety.h"
#include "cli.h"
#include "lcd_interface.h"
#include "fault_logging.h"
#include "timeout_manager.h"
#include "task_manager.h"
#include "config_schema_versioning.h"
#include "watchdog_manager.h"
#include "web_server.h"
#include "boot_validation.h"
#include "board_inputs.h" 

static uint32_t boot_time_ms = 0;
extern WebServerManager webServer;

// --- Stack Overflow Hook ---
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    static volatile bool handling = false;
    if (handling) return;
    handling = true;
    Serial.println("\n\n[CRITICAL] STACK OVERFLOW DETECTED");
    Serial.printf("Task: %s\n", pcTaskName);
    Serial.println("System Halting.");
    faultLogCritical(FAULT_CRITICAL_SYSTEM_ERROR, "Stack Overflow");
    delay(1000);
    ESP.restart();
}

// --- Init Wrappers ---
bool init_fault_logging_wrapper() { faultLoggingInit(); return true; }
bool init_watchdog_wrapper() { watchdogInit(); return true; }
bool init_timeout_wrapper() { timeoutManagerInit(); return true; }
bool init_config_wrapper() { configUnifiedInit(); return true; }
bool init_schema_wrapper() { configSchemaVersioningInit(); return !configIsMigrationNeeded(); }
bool init_calib_wrapper() { loadAllCalibration(); encoderCalibrationInit(); return true; }
bool init_plc_wrapper() { plcIfaceInit(); return true; }
bool init_enc_wrapper() { wj66Init(); return true; }
bool init_safety_wrapper() { safetyInit(); return true; }
bool init_motion_wrapper() { motionInit(); return true; }
bool init_cli_wrapper() { cliInit(); return true; }
bool init_web_wrapper() { webServer.init(); webServer.begin(); return true; }
bool init_inputs_wrapper() { boardInputsInit(); return true; }

#define BOOT_INIT(name, func, code) \
  do { \
    Serial.printf("[BOOT] Init %s... ", name); \
    if (func()) { \
      Serial.println("[OK]"); \
      bootMarkInitialized(name); \
    } else { \
      Serial.println("[FAIL]"); \
      bootMarkFailed(name, "Init failed", code); \
    } \
  } while(0)

void setup() {
  Serial.begin(115200);
  delay(1000);
  boot_time_ms = millis();
  
  Serial.println("\n=== BISSO v4.2 PRO FIRMWARE ===");
  
  bootValidationInit();
  
  BOOT_INIT("Fault Log", init_fault_logging_wrapper, BOOT_ERROR_FAULT_LOGGING);
  BOOT_INIT("Watchdog", init_watchdog_wrapper, BOOT_ERROR_WATCHDOG);
  BOOT_INIT("Config", init_config_wrapper, BOOT_ERROR_CONFIG);
  BOOT_INIT("Schema", init_schema_wrapper, BOOT_ERROR_SCHEMA);
  BOOT_INIT("PLC", init_plc_wrapper, BOOT_ERROR_PLC_IFACE);
  BOOT_INIT("Inputs", init_inputs_wrapper, (boot_status_code_t)14);
  BOOT_INIT("Encoder", init_enc_wrapper, BOOT_ERROR_ENCODER);
  BOOT_INIT("Safety", init_safety_wrapper, BOOT_ERROR_SAFETY);
  BOOT_INIT("Motion", init_motion_wrapper, BOOT_ERROR_MOTION);
  BOOT_INIT("CLI", init_cli_wrapper, BOOT_ERROR_CLI);
  BOOT_INIT("Web", init_web_wrapper, (boot_status_code_t)13);

  Serial.println("\n[BOOT] Validating system...");
  if (!bootValidateAllSystems()) {
    bootHandleCriticalError("Boot validation failed.");
    return;
  }
  
  taskManagerInit();
  taskManagerStart();
  
  Serial.printf("[BOOT] [OK] Complete in %lu ms\n\n", millis() - boot_time_ms);
  vTaskDelete(NULL);
}

void loop() {
  webServer.handleClient();
  delay(10); 
}
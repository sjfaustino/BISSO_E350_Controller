#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "system_constants.h"
#include "string_safety.h"
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

static bool system_ready = false;
static uint32_t boot_time_ms = 0;

// External Web Server instance
extern WebServerManager webServer;

// ============================================================================
// FREERTOS HARDENING HOOKS
// ============================================================================

/**
 * @brief FreeRTOS Stack Overflow Hook
 * Called by the kernel when a task exceeds its allocated stack size.
 * This function must remain lean and avoid heavy operations.
 */
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    // 1. Prevent recursion/re-entry
    static volatile bool handling_overflow = false;
    if (handling_overflow) return;
    handling_overflow = true;

    // 2. Direct Emergency Serial Output
    // We print immediately to UART to capture the culprit before the system dies.
    // Using simple prints to minimize stack usage.
    Serial.println("\n\n#########################################");
    Serial.println("!!! CRITICAL: STACK OVERFLOW DETECTED !!!");
    Serial.print("Faulty Task: ");
    Serial.println(pcTaskName);
    Serial.println("Action: Forcing System Restart");
    Serial.println("#########################################\n");

    // 3. Attempt to log to NVS (Best Effort)
    // Note: This might fail if the stack is totally corrupted or if mutexes are held,
    // but we attempt it to preserve the record for the next boot.
    // We bypass standard wrappers if possible, but here we use the critical logger.
    faultLogCritical(FAULT_CRITICAL_SYSTEM_ERROR, "Stack Overflow");

    // 4. Force Restart
    // Allow a brief moment for the serial buffer to flush
    delay(1000);
    ESP.restart();
}

// ============================================================================
// BOOT WRAPPER PROTOTYPES (Required by BOOT_INIT_SUBSYSTEM macro)
// ============================================================================

bool init_fault_logging_wrapper() {
  faultLoggingInit();
  return true; 
}

bool init_watchdog_wrapper() {
  watchdogInit();
  return true; 
}

bool init_timeout_manager_wrapper() {
  timeoutManagerInit();
  return true;
}

bool init_config_unified_wrapper() {
  configUnifiedInit();
  return true; 
}

bool init_config_schema_wrapper() {
  configSchemaVersioningInit();
  return !configIsMigrationNeeded(); 
}

bool init_encoder_calibration_wrapper() {
  loadAllCalibration(); 
  encoderCalibrationInit(); 
  return true;
}

bool init_plc_iface_wrapper() {
  plcIfaceInit();
  return true; 
}

bool init_encoder_wj66_wrapper() {
  wj66Init(); 
  return true;
}

bool init_safety_wrapper() {
  safetyInit();
  return true; 
}

bool init_motion_wrapper() {
  motionInit();
  return true;
}

bool init_cli_wrapper() {
  cliInit(); 
  return true;
}

bool init_web_server_wrapper() {
  webServer.init();
  webServer.begin();
  return true;
}

bool init_board_inputs_wrapper() {
    boardInputsInit();
    return true;
}

// ============================================================================
// BOOT SEQUENCE - Runs on core 0 before tasks start
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  boot_time_ms = millis();
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     BISSO v4.2 Production Firmware     ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // --- STAGE 0: Validation Initialization ---
  bootValidationInit();
  
  // --- STAGE 1: Logging and Base Services ---
  BOOT_INIT_SUBSYSTEM("Fault Logging", init_fault_logging_wrapper, BOOT_ERROR_FAULT_LOGGING);
  BOOT_INIT_SUBSYSTEM("Watchdog Timer", init_watchdog_wrapper, BOOT_ERROR_WATCHDOG);
  BOOT_INIT_SUBSYSTEM("Timeout Manager", init_timeout_manager_wrapper, BOOT_ERROR_TIMEOUT_MANAGER);

  // --- STAGE 2: Configuration and Calibration ---
  BOOT_INIT_SUBSYSTEM("Config Unified", init_config_unified_wrapper, BOOT_ERROR_CONFIG);
  BOOT_INIT_SUBSYSTEM("Schema Versioning", init_config_schema_wrapper, BOOT_ERROR_SCHEMA);
  BOOT_INIT_SUBSYSTEM("Encoder Calibration", init_encoder_calibration_wrapper, BOOT_ERROR_ENCODER_CALIB);

  // --- STAGE 3: Hardware Interfaces ---
  BOOT_INIT_SUBSYSTEM("PLC Interface", init_plc_iface_wrapper, BOOT_ERROR_PLC_IFACE);
  BOOT_INIT_SUBSYSTEM("Board Inputs", init_board_inputs_wrapper, (boot_status_code_t)14);
  BOOT_INIT_SUBSYSTEM("Encoder Comm", init_encoder_wj66_wrapper, BOOT_ERROR_ENCODER);

  // --- STAGE 4: Safety and Motion Logic ---
  BOOT_INIT_SUBSYSTEM("Safety System", init_safety_wrapper, BOOT_ERROR_SAFETY);
  BOOT_INIT_SUBSYSTEM("Motion System", init_motion_wrapper, BOOT_ERROR_MOTION);
  
  // --- STAGE 5: User Interfaces ---
  BOOT_INIT_SUBSYSTEM("CLI Interface", init_cli_wrapper, BOOT_ERROR_CLI);
  // NOTE: Manually casting 13 for BOOT_ERROR_WEB_SERVER if not defined in enum
  BOOT_INIT_SUBSYSTEM("Web Server", init_web_server_wrapper, (boot_status_code_t)13); 

  // --- Final Validation and Task Start ---
  
  Serial.println("\n[BOOT] Running final validation and starting tasks...");
  
  if (!bootValidateAllSystems()) {
    bootHandleCriticalError("Final boot validation failed (too many subsystem failures)");
    return; // Should halt here
  }
  
  // Start the dedicated safety monitoring task first (Core 1)
  // Note: Include safety_task.h at top if safetyTaskStart is not visible, 
  // but here we assume it is handled via the task_manager or direct prototype.
  // In the current file set, safetyTaskStart is declared in safety_task.h.
  // Since we haven't included safety_task.h in main.cpp, we rely on taskManager.
  // However, safetyTaskStart was defined in tasks_safety.cpp but traditionally called here.
  // To keep it clean, we will assume taskManagerStart() handles the dispatcher logic 
  // OR we add the specific start call if the architecture demands it.
  
  // Initialize FreeRTOS task manager and start scheduler
  // taskManagerStart() creates all tasks including Safety, Motion, etc.
  taskManagerInit();
  taskManagerStart();
  
  system_ready = true;
  
  uint32_t total_boot_ms = millis() - boot_time_ms;
  Serial.print("[BOOT] Total boot time: ");
  Serial.print(total_boot_ms);
  Serial.println(" ms\n");
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║      ✅ SYSTEM BOOT COMPLETE          ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Delete setup task
  vTaskDelete(NULL);
}

// ============================================================================
// LOOP - Runs on core 0 (idle loop)
// ============================================================================

void loop() {
  // Handle web server requests
  webServer.handleClient();
  delay(10); 
}
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

static bool system_ready = false;
static uint32_t boot_time_ms = 0;

// Forward declaration for external web server instance
extern WebServerManager webServer;

// ============================================================================
// BOOT SEQUENCE - Runs on core 0 before tasks start
// ============================================================================

void setup() {
  // Serial initialization
  Serial.begin(115200);
  delay(1000);
  
  boot_time_ms = millis();
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     BISSO v4.2 Production Firmware     ║");
  Serial.println("║   ESP32-S3 Bridge Saw Controller        ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Started at: ");
  Serial.print(boot_time_ms);
  Serial.println(" ms\n");

  // Initialize core systems in order (Strict dependency order)
  Serial.println("[BOOT] Initializing core systems (pre-task)...\n");
  
  // 1. Logging and Base Services (Faults, WDT, Timeouts)
  Serial.println("[BOOT] 1/12 - Fault logging system");
  faultLoggingInit();
  delay(50);
  
  Serial.println("[BOOT] 2/12 - Watchdog timer system");
  watchdogInit();
  delay(50);
  
  Serial.println("[BOOT] 3/12 - Timeout manager");
  timeoutManagerInit();
  delay(50);

  // 2. Configuration and Calibration
  Serial.println("[BOOT] 4/12 - Configuration system");
  configUnifiedInit();
  delay(50);
  
  Serial.println("[BOOT] 5/12 - Schema versioning");
  configSchemaVersioningInit();
  delay(50);
  
  // FIX: Load calibration data from NVS first, then initialize the encoder state array.
  Serial.println("[BOOT] 6/12 - Encoder calibration (Load NVS data)");
  loadAllCalibration(); 
  encoderCalibrationInit(); 
  delay(50);

  // 3. Hardware Interfaces (PLC, Encoder)
  Serial.println("[BOOT] 7/12 - PLC interface");
  plcIfaceInit();
  delay(50);
  
  Serial.println("[BOOT] 8/12 - Encoder communication (Baud detection/Init)");
  wj66Init(); 
  delay(50);

  // 4. Safety and Motion Logic (Relies on NVS/Hardware being ready)
  Serial.println("[BOOT] 9/12 - Safety system");
  safetyInit();
  delay(50);
  
  Serial.println("[BOOT] 10/12 - Motion system");
  motionInit();
  delay(50);
  
  // 5. User Interfaces (CLI, Web, LCD)
  Serial.println("[BOOT] 11/12 - CLI interface");
  cliInit(); 
  delay(50);

  Serial.println("[BOOT] 12/12 - Web Server");
  webServer.init();
  webServer.begin();
  delay(50);
  
  // --- Validation and Task Start ---
  
  Serial.println("\n[BOOT] Running pre-task boot validation...");
  bootValidationInit();
  
  if (bootValidateAllSystems()) {
    Serial.println("✅ Pre-task validation PASSED\n");
  } else {
    Serial.println("❌ Pre-task validation FAILED\n");
    faultLogError(FAULT_BOOT_FAILED, "Pre-task validation failed");
  }
  
  uint32_t pre_task_boot_ms = millis() - boot_time_ms;
  Serial.print("[BOOT] Pre-task initialization: ");
  Serial.print(pre_task_boot_ms);
  Serial.println(" ms\n");
  
  // Initialize FreeRTOS task manager and start scheduler
  Serial.println("[BOOT] Initializing FreeRTOS task manager...");
  taskManagerInit();
  
  delay(100);
  
  Serial.println("[BOOT] Starting FreeRTOS tasks...");
  taskManagerStart();
  
  system_ready = true;
  
  uint32_t total_boot_ms = millis() - boot_time_ms;
  Serial.print("[BOOT] Total boot time: ");
  Serial.print(total_boot_ms);
  Serial.println(" ms\n");
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║      ✅ SYSTEM BOOT COMPLETE          ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Delete setup task (tasks are running on Core 1/Core 0)
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
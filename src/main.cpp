#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
  Serial.println("║   FreeRTOS Multi-Task Architecture      ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.print("[BOOT] Started at: ");
  Serial.print(boot_time_ms);
  Serial.println(" ms\n");

  // Initialize core systems in order (sequential initialization)
  Serial.println("[BOOT] Initializing core systems (pre-task)...\n");
  
  // Fault logging FIRST (needed for error reporting)
  Serial.println("[BOOT] 1/10 - Fault logging system");
  faultLoggingInit();
  delay(50);
  
  // Watchdog SECOND (protects everything)
  Serial.println("[BOOT] 2/10 - Watchdog timer system");
  watchdogInit();
  delay(50);
  
  // Timeout manager
  Serial.println("[BOOT] 3/10 - Timeout manager");
  timeoutManagerInit();
  delay(50);
  
  // Configuration system with schema versioning
  Serial.println("[BOOT] 4/10 - Configuration system");
  configUnifiedInit();
  delay(50);
  
  Serial.println("[BOOT] 5/10 - Schema versioning");
  configSchemaVersioningInit();
  delay(50);
  
  // Safety system
  Serial.println("[BOOT] 6/10 - Safety system");
  safetyInit();
  delay(50);
  
  // Motion system
  Serial.println("[BOOT] 7/10 - Motion system");
  motionInit();
  delay(50);
  
  // Encoder system
  Serial.println("[BOOT] 8/10 - Encoder system");
  wj66Init();
  delay(50);
  
  Serial.println("[BOOT] 9/10 - Encoder calibration");
  encoderCalibrationInit();
  delay(50);
  
  // PLC interface
  Serial.println("[BOOT] 10/10 - PLC interface");
  plcIfaceInit();
  delay(50);
  
  // Boot validation
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
  
  // Initialize Web Server
  Serial.println("[BOOT] Initializing Web Server...");
  webServer.init();
  webServer.begin();
  delay(50);
  
  // Initialize FreeRTOS task manager
  Serial.println("[BOOT] Initializing FreeRTOS task manager...");
  taskManagerInit();
  
  delay(100);
  
  // Start all FreeRTOS tasks
  Serial.println("[BOOT] Starting FreeRTOS tasks...");
  taskManagerStart();
  
  // All systems initialized, tasks will take over
  system_ready = true;
  
  uint32_t total_boot_ms = millis() - boot_time_ms;
  Serial.print("[BOOT] Total boot time: ");
  Serial.print(total_boot_ms);
  Serial.println(" ms\n");
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║      ✅ SYSTEM BOOT COMPLETE          ║");
  Serial.println("║   All FreeRTOS tasks running...        ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  bootShowStatus();
  cliPrintPrompt();
  
  // Delete setup task (no longer needed)
  // The FreeRTOS tasks will handle everything
  vTaskDelete(NULL);
}

// ============================================================================
// LOOP - Runs on core 0 (idle loop)
// ============================================================================

void loop() {
  // In FreeRTOS architecture, loop() is not the main control flow
  // All real-time work is done in FreeRTOS tasks
  
  // Handle web server requests
  webServer.handleClient();
  
  // Keep core 0 available for WiFi/BLE (if needed in future)
  // Monitor system health periodically
  
  delay(10);  // Minimal idle loop on core 0 with web server handling
  
  // Optional: System health monitoring every few seconds
  // This runs on core 0 and doesn't interfere with core 1 tasks
}

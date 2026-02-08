#include "board_inputs.h"
#include "boot_validation.h"
#include "cli.h"
#include "config_schema_versioning.h"
#include "config_unified.h"
#include "config_keys.h"
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
#include "auth_manager.h"  // PHASE 5.10: SHA-256 password hashing
#include "web_server.h"
#include "network_manager.h"
#include "ota_manager.h" // Needed for otaCheckForUpdate at boot
#include "encoder_diagnostics.h"  // PHASE 5.3: Advanced encoder diagnostics
#include "load_manager.h"  // PHASE 5.3: Graceful degradation under load
#include "dashboard_metrics.h"  // PHASE 5.3: Web UI dashboard metrics
#include "axis_synchronization.h"  // PHASE 5.6: Axis synchronization validation
#include "job_recovery.h"  // Power loss recovery
#include "operator_alerts.h"  // Buzzer and tower light
#include "spindle_current_monitor.h" // PHASE 5.0
#include "job_manager.h" // G-Code Job Manager
#include "sd_card_manager.h"  // SD Card support
#include "rtc_manager.h"       // RTC auto-sync
#include "system_utils.h"      // Safe reboot helper
#include "trash_bin_manager.h" // Trash bin with auto-delete
#include "memory_prealloc.h"  // PHASE 6.12: Memory pre-allocation
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static uint32_t boot_time_ms = 0;
extern WebServerManager webServer;

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  static volatile bool handling = false;
  if (handling) return;
  handling = true;
  logError("[CRITICAL] STACK OVERFLOW in task: %s", pcTaskName);
  faultLogCritical(FAULT_CRITICAL_SYSTEM_ERROR, "Stack Overflow");
  delay(1000);
  systemEmergencyReboot();  // Critical error - minimal cleanup
}

#include "api_config.h"

// Wrappers
bool init_fault_logging_wrapper() { faultLoggingInit(); return true; }
bool init_watchdog_wrapper() { watchdogInit(); return true; }
bool init_timeout_wrapper() { timeoutManagerInit(); return true; }
bool init_config_wrapper() { 
    configUnifiedInit(); 
    apiConfigInit(); 
    return true; 
}
bool init_schema_wrapper() { configSchemaVersioningInit(); configSchemaInit(); return !configIsMigrationNeeded(); }
bool init_auth_wrapper() { authInit(); return true; }  // PHASE 5.10: SHA-256 authentication
bool init_prealloc_wrapper() { return memoryPreallocInit(); } // PHASE 6.12

// PHASE 5.7: Cursor AI Fix - Proper error checking for calibration initialization
bool init_calib_wrapper() {
    // Calibration is SAFETY CRITICAL - must succeed for safe operation
    // Note: loadAllCalibration() and encoderCalibrationInit() return void
    // They log errors internally if they fail
    loadAllCalibration();
    encoderCalibrationInit();
    
    // Since these functions don't return status, we assume success
    // If they fail, they will log errors internally
    return true;
}

// UPDATED: Using correct init function
bool init_plc_wrapper() { elboInit(); return true; }

bool init_lcd_wrapper() { lcdInterfaceInit(); return true; }
bool init_enc_wrapper() { wj66Init(); return true; }
bool init_safety_wrapper() { safetyInit(); return true; }
bool init_motion_wrapper() { motionInit(); return true; }
bool init_cli_wrapper() { cliInit(); return true; }
bool init_inputs_wrapper() { boardInputsInit(); return true; }

// PHASE 5.7: Cursor AI Fix - Proper error checking for network initialization
bool init_network_wrapper() {
    // Note: networkManager.init(), webServer.init(), and webServer.begin() return void
    // They log errors internally if they fail
    networkManager.init();
    webServer.init();
    webServer.begin();
    
    // Non-critical: system can operate without network (local control via serial)
    // Return true to allow boot to continue even if network initialization fails
    return true;
}

bool init_sd_card_wrapper() {
    // SD card is optional - system works without it
    sdCardInit();  // Returns false if no card, but that's OK
    return true;   // Always succeed - boot continues without SD card
}


// PHASE 5.3: Initialize advanced diagnostics and load management
bool init_encoder_diag_wrapper() { encoderDiagnosticsInit(); return true; }
bool init_load_mgr_wrapper() { loadManagerInit(); return true; }
bool init_dashboard_wrapper() { dashboardMetricsInit(); return true; }

// PHASE 5.6: Initialize axis synchronization validation
bool init_axis_sync_wrapper() { axisSynchronizationInit(); return true; }

// Operator features: Power loss recovery, buzzer, tower light
bool init_recovery_wrapper() { recoveryInit(); return true; }
bool init_alerts_wrapper() { buzzerInit(); statusLightInit(); return true; }
// PHASE 5.0: Initialize Spindle Monitor with default JXK-10 address (1) and threshold (30A)
bool init_spindle_wrapper() { 
    uint8_t addr = (uint8_t)configGetInt(KEY_JXK10_ADDR, 1); // Consolidate on single key
    // Fix: Use dedicated threshold key for shutdown, NOT pause threshold
    float thr = (float)configGetInt(KEY_SPINDLE_THRESHOLD, 30);
    return spindleMonitorInit(addr, thr); 
}

bool init_job_wrapper() { jobManager.init(); return true; }

// YH-TC05 Tachometer
#include "yhtc05_modbus.h"
bool init_yhtc05_wrapper() {
    bool enabled = configGetInt(KEY_YHTC05_ENABLED, 1); // Default enabled
    int addr = configGetInt(KEY_YHTC05_ADDR, 3);
    yhtc05ModbusInit((uint8_t)addr, 9600); 
    if (enabled) {
        yhtc05RegisterWithBus(1000, 100);
    }
    return true;
}

#define BOOT_INIT(name, func, code) \
  do { if (func()) { logInfo("[BOOT] Init %s [OK]", name); bootMarkInitialized(name); } \
       else { logError("[BOOT] Init %s [FAIL]", name); bootMarkFailed(name, "Init failed", code); } } while (0)

void setup() {
  Serial.begin(115200);
  
  // PHASE 16 FIX: On ESP32-S3 with USB CDC, wait for Serial to connect 
  // so we don't miss the initial boot text. Timeout after 5 seconds.
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(ARDUINO_USB_CDC_ON_BOOT)
  uint32_t start_wait = millis();
  while (!Serial && (millis() - start_wait < 5000)) {
    delay(10);
  }
#endif
  
  delay(2000);  // Robust buffer for USB CDC / UART stability
  
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
  BOOT_INIT("Auth", init_auth_wrapper, (boot_status_code_t)20);
  BOOT_INIT("Prealloc", init_prealloc_wrapper, (boot_status_code_t)25);

  // CRITICAL: Initialize task manager BEFORE Motion to create mutexes/queues
  taskManagerInit();

  BOOT_INIT("PLC", init_plc_wrapper, BOOT_ERROR_PLC_IFACE);
  BOOT_INIT("LCD", init_lcd_wrapper, (boot_status_code_t)19);
  BOOT_INIT("Inputs", init_inputs_wrapper, (boot_status_code_t)14);
  BOOT_INIT("Encoder", init_enc_wrapper, BOOT_ERROR_ENCODER);
  BOOT_INIT("Tachometer", init_yhtc05_wrapper, (boot_status_code_t)22);
  BOOT_INIT("Safety", init_safety_wrapper, BOOT_ERROR_SAFETY);
  BOOT_INIT("Motion", init_motion_wrapper, BOOT_ERROR_MOTION);
  BOOT_INIT("CLI", init_cli_wrapper, BOOT_ERROR_CLI);
  BOOT_INIT("Network", init_network_wrapper, (boot_status_code_t)13);
  BOOT_INIT("SD Card", init_sd_card_wrapper, (boot_status_code_t)23);
  BOOT_INIT("Encoder Diag", init_encoder_diag_wrapper, (boot_status_code_t)15);
  BOOT_INIT("Load Manager", init_load_mgr_wrapper, (boot_status_code_t)16);
  BOOT_INIT("Dashboard", init_dashboard_wrapper, (boot_status_code_t)17);
  BOOT_INIT("Axis Sync", init_axis_sync_wrapper, (boot_status_code_t)18);
  BOOT_INIT("Recovery", init_recovery_wrapper, (boot_status_code_t)21);
  BOOT_INIT("Alerts", init_alerts_wrapper, (boot_status_code_t)22);
  // PHASE 5.0: Spindle Current Monitor
  // PHASE 5.0: Spindle Current Monitor
  BOOT_INIT("Spindle Mon", init_spindle_wrapper, (boot_status_code_t)23);
  BOOT_INIT("Job Manager", init_job_wrapper, (boot_status_code_t)24);


  logInfo("[BOOT] Validating system health...");
  if (!bootValidateAllSystems()) {
    bootHandleCriticalError("Boot validation failed.");
    return;
  }

  // taskManagerInit() already called earlier (before Motion init)
  
  // PHASE 6.1: REMOVED synchronous OTA check at boot
  // The OTA check was allocating ~16KB SSL buffer that fragmented the heap.
  // Now deferred to background task via otaStartBackgroundCheck() after tasks start.
  // This allows task stacks to be allocated contiguously first.
  if (WiFi.status() == WL_CONNECTED) {
      logInfo("[BOOT] WiFi Connected. IP: %s", WiFi.localIP().toString().c_str());
      logInfo("[BOOT] OTA check deferred to background task (fragmentation fix)");
  } else {
      logWarning("[BOOT] WiFi not connected - OTA check will run when connected");
  }

  perfMonitorInit();  // PHASE 5.1: Initialize performance monitoring
  
  // Stop boot log capture before tasks start (prevents CLI output from being logged)
  // bootLogStop();
  
  taskManagerStart();

  // RTC auto-sync: Check if time needs sync from NTP (v3.1 boards only)
  #if BOARD_HAS_RTC_DS3231
  rtcCheckAndSync();
  #endif
  
  // Initialize Trash Bin
  trashBinInit();
  trashBinStartBackgroundHandler();
  
  // PHASE 6.2: OTA check is now OPTIONAL and disabled by default
  // The SSL buffer allocation (~16KB) causes heap fragmentation
  // Enable via config: config set ota_chk_en 1
  int ota_check_enabled = configGetInt(KEY_OTA_CHECK_EN, 0);
  if (ota_check_enabled && WiFi.status() == WL_CONNECTED) {
      logInfo("[BOOT] OTA GitHub check enabled - starting background check");
      delay(1000);  // Let tasks initialize their stacks first
      otaStartBackgroundCheck();
  } else if (!ota_check_enabled) {
      logInfo("[BOOT] OTA GitHub check disabled (saves 16KB SSL memory)");
  }
  
  logInfo("[BOOT] [OK] Complete in %lu ms", (unsigned long)(millis() - boot_time_ms));
}

volatile uint32_t accumulated_loop_count = 0;

void loop() {
  networkManager.update();
  jobManager.update();
  accumulated_loop_count++;
  delay(10); 
}

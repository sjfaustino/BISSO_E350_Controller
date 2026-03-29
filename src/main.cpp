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
#include "dac_interface.h"
#include "safety.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "task_manager.h"
#include "task_performance_monitor.h" // Task performance monitoring
#include "config_validator_schema.h" // Configuration schema validation
#include "timeout_manager.h"
#include "watchdog_manager.h"
#include "spinlock_timing.h"
#include "auth_manager.h" // SHA-256 password hashing
#include "web_server.h"
#include "network_manager.h"
#include "ota_manager.h" // Needed for otaCheckForUpdate at boot
#include "api_ota_updater.h" // OTA rollback support
#include "encoder_diagnostics.h" // Advanced encoder diagnostics
#include "load_manager.h" // Graceful degradation under load
#include "dashboard_metrics.h" // Web UI dashboard metrics
#include "axis_synchronization.h" // Axis synchronization validation
#include "job_recovery.h" // Power loss recovery
#include "operator_alerts.h" // Buzzer and tower light
#include "spindle_current_monitor.h"
#include "job_manager.h" // G-Code Job Manager
#include "sd_card_manager.h" // SD Card support
#include "rtc_manager.h" // RTC auto-sync
#include "system_utils.h" // Safe reboot helper
#include "trash_bin_manager.h" // Trash bin with auto-delete
#include "memory_prealloc.h" // Memory pre-allocation
#include "engineering_menu.h" // BOOT button menu
#include "altivar31_modbus.h"
#include "api_config.h"
#include "yhtc05_modbus.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h> // Fixed: Required for gpio_install_isr_service

static uint32_t boot_time_ms = 0;
extern WebServerManager webServer;

static void otaValidationTimerCallback(TimerHandle_t xTimer) {
 (void)xTimer;
 otaValidateRunningFirmware();
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
 static volatile bool handling = false;
 if (handling) return;
 handling = true;
 logError("[CRITICAL] STACK OVERFLOW in task: %s", pcTaskName);
 faultLogCritical(FAULT_CRITICAL_SYSTEM_ERROR, "Stack Overflow");
 delay(1000);
 systemEmergencyReboot(); // Critical error - minimal cleanup
}

// Boot init wrapper functions.
// Each calls the underlying module init and returns a success/failure bool.
// Where the underlying init returns void, we return true and note it below.
bool init_fault_logging_wrapper() { faultLoggingInit(); return true; } // void init
bool init_watchdog_wrapper() { watchdogInit(); return true; } // void init
bool init_timeout_wrapper() { timeoutManagerInit(); return true; } // void init
bool init_config_wrapper() {
 result_t r = configUnifiedInit();
 apiConfigInit(); // void — no return value, always runs
 return (r == RESULT_OK);
}
bool init_schema_wrapper() { configSchemaVersioningInit(); configSchemaInit(); return !configIsMigrationNeeded(); }
bool init_auth_wrapper() { authInit(); return true; } // void init
bool init_prealloc_wrapper(){ return memoryPreallocInit(); }

// Calibration: underlying functions return void and log errors internally.
// Cannot propagate success/failure — see loadAllCalibration() / encoderCalibrationInit().
bool init_calib_wrapper() {
 loadAllCalibration();
 encoderCalibrationInit();
 return true;
}

bool init_plc_wrapper() { return elboInit() == RESULT_OK; }
bool init_lcd_wrapper() { lcdInterfaceInit(); return true; } // void init
bool init_enc_wrapper() { wj66Init(); return true; } // void init
bool init_safety_wrapper() { return safetyInit() == RESULT_OK; }
bool init_motion_wrapper() { return motionInit() == RESULT_OK; }
bool init_cli_wrapper() { cliInit(); return true; } // void init
bool init_inputs_wrapper() { boardInputsInit(); return true; } // void init
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
 sdCardInit(); // Returns false if no card, but that's OK
 return true; // Always succeed - boot continues without SD card
}


// Initialize advanced diagnostics and load management
bool init_encoder_diag_wrapper() { encoderDiagnosticsInit(); return true; }
bool init_load_mgr_wrapper() { loadManagerInit(); return true; }
bool init_dashboard_wrapper() { dashboardMetricsInit(); return true; }

// Initialize axis synchronization validation
bool init_axis_sync_wrapper() { axisSynchronizationInit(); return true; }

// Operator features: Power loss recovery, buzzer, tower light
bool init_recovery_wrapper() { recoveryInit(); return true; }
bool init_alerts_wrapper() { buzzerInit(); statusLightInit(); return true; }
// Initialize Spindle Monitor with default JXK-10 address (1) and threshold (30A)
bool init_spindle_wrapper() { 
 // Initialize Analog Speed Card (DAC)
 dacInit();

 uint8_t addr = (uint8_t)configGetInt(KEY_JXK10_ADDR, 1);
 float thr = (float)configGetInt(KEY_SPINDLE_THRESHOLD, 30);
 
 // Initialize Modbus VFDs (handles both instances)
 uint32_t rs485_baud = (uint32_t)configGetInt(KEY_RS485_BAUD, 19200);
 altivar31ModbusInit(2, rs485_baud); 

 return spindleMonitorInit(addr, thr); 
}

bool init_job_wrapper() { jobManager.init(); return true; }

// YH-TC05 Tachometer
// yhtc05_modbus.h included at top of file
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
 do { if (func()) { logModuleInitOK(name); bootMarkInitialized(name); } \
 else { logModuleInitFail(name); bootMarkFailed(name, "Init failed", code); } } while (0)

void setup() {
 // 0. Initialize GPIO ISR Service (Create global ISR handler)
 // This MUST be called before any driver (Ethernet, Inputs) attaches an interrupt
 // ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_install_isr_service(0));
 if (gpio_install_isr_service(0) != ESP_OK) {
 // Driver might be already installed by Arduino framework (unlikely at this stage but safe to check)
 // logError("ISR Service install failed"); // logging not ready yet
 }
 
 Serial.begin(115200);
 
 // FIX: On ESP32-S3 with USB CDC, wait for Serial to connect 
 // so we don't miss the initial boot text. Timeout after 5 seconds.
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(ARDUINO_USB_CDC_ON_BOOT)
 uint32_t start_wait = millis();
 while (!Serial && (millis() - start_wait < 5000)) {
 delay(10);
 }
#endif
 
 delay(2000); // Robust buffer for USB CDC / UART stability
 
 serialLoggerInit(LOG_LEVEL);
 spinlockTimingInit();

 boot_time_ms = millis();

 // Initialize Engineering Menu (GPIO 0)
 engineeringMenu.init();

 char ver_str[FIRMWARE_VERSION_STRING_LEN];
 firmwareGetVersionString(ver_str, sizeof(ver_str));
 logInfo("=== %s STARTING ===", ver_str);

 bootValidationInit();

 BOOT_INIT("Fault Log", init_fault_logging_wrapper, BOOT_ERROR_FAULT_LOGGING);
 BOOT_INIT("Watchdog", init_watchdog_wrapper, BOOT_ERROR_WATCHDOG);
 BOOT_INIT("Config", init_config_wrapper, BOOT_ERROR_CONFIG);
 BOOT_INIT("Schema", init_schema_wrapper, BOOT_ERROR_SCHEMA);
 BOOT_INIT("Auth", init_auth_wrapper, BOOT_ERROR_AUTH);
 BOOT_INIT("Prealloc", init_prealloc_wrapper, BOOT_ERROR_PREALLOC);

 // CRITICAL: Initialize task manager BEFORE Motion to create mutexes/queues
 taskManagerInit();

 BOOT_INIT("PLC", init_plc_wrapper, BOOT_ERROR_PLC_IFACE);
 BOOT_INIT("LCD", init_lcd_wrapper, BOOT_ERROR_LCD);
 BOOT_INIT("Inputs", init_inputs_wrapper, BOOT_ERROR_INPUTS);
 BOOT_INIT("Encoder", init_enc_wrapper, BOOT_ERROR_ENCODER);
 BOOT_INIT("Tachometer", init_yhtc05_wrapper, BOOT_ERROR_TACHOMETER);
 BOOT_INIT("Safety", init_safety_wrapper, BOOT_ERROR_SAFETY);
 BOOT_INIT("Motion", init_motion_wrapper, BOOT_ERROR_MOTION);
 BOOT_INIT("CLI", init_cli_wrapper, BOOT_ERROR_CLI);
 BOOT_INIT("Network", init_network_wrapper, BOOT_ERROR_NETWORK);
 BOOT_INIT("SD Card", init_sd_card_wrapper, BOOT_ERROR_SD_CARD);
 
 // Initialize persistent system logging to SD after mount
 if (sdCardIsMounted()) {
 systemLogInit("/var/log/boot.log");
 }

 BOOT_INIT("Encoder Diag", init_encoder_diag_wrapper, BOOT_ERROR_ENCODER_DIAG);
 BOOT_INIT("Load Manager", init_load_mgr_wrapper, BOOT_ERROR_LOAD_MGR);
 BOOT_INIT("Dashboard", init_dashboard_wrapper, BOOT_ERROR_DASHBOARD);
 BOOT_INIT("Axis Sync", init_axis_sync_wrapper, BOOT_ERROR_AXIS_SYNC);
 BOOT_INIT("Recovery", init_recovery_wrapper, BOOT_ERROR_RECOVERY);
 BOOT_INIT("Alerts", init_alerts_wrapper, BOOT_ERROR_ALERTS);
 BOOT_INIT("Spindle Mon", init_spindle_wrapper, BOOT_ERROR_SPINDLE);
 BOOT_INIT("Job Manager", init_job_wrapper, BOOT_ERROR_JOB_MGR);


 logInfo("[BOOT] Validating system health...");
 if (!bootValidateAllSystems()) {
 bootHandleCriticalError("Boot validation failed.");
 return;
 }

 // taskManagerInit() already called earlier (before Motion init)
 
 // REMOVED synchronous OTA check at boot
 // The OTA check was allocating ~16KB SSL buffer that fragmented the heap.
 // Now deferred to background task via otaStartBackgroundCheck() after tasks start.
 // This allows task stacks to be allocated contiguously first.
 if (WiFi.status() == WL_CONNECTED) {
 logInfo("[BOOT] WiFi Connected. IP: %s", WiFi.localIP().toString().c_str());
 logInfo("[BOOT] OTA check deferred to background task (fragmentation fix)");
 } else {
 logWarning("[BOOT] WiFi not connected - OTA check will run when connected");
 }

 perfMonitorInit(); // Initialize performance monitoring
 
 // Stop boot log capture before tasks start (prevents CLI output from being logged)
 // bootLogStop();
 
 taskManagerStart();

 // Start background maintenance task (distance persistence, spindle monitor, etc.)
 // These were extracted from the 10ms motion loop to prevent NVS flash writes
 // from blocking the CPU during axis movement.
 motionStartMaintenanceTask();

 // RTC auto-sync: Check if time needs sync from NTP (v3.1 boards only)
 #if BOARD_HAS_RTC_DS3231
 rtcCheckAndSync();
 #endif
 
 // Initialize Trash Bin
 trashBinInit();
 trashBinStartBackgroundHandler();
 
 // OTA check is now OPTIONAL and disabled by default
 // The SSL buffer allocation (~16KB) causes heap fragmentation
 // Enable via config: config set ota_chk_en 1
 int ota_check_enabled = configGetInt(KEY_OTA_CHECK_EN, 0);
 if (ota_check_enabled && WiFi.status() == WL_CONNECTED) {
 logInfo("[BOOT] OTA GitHub check enabled - starting background check");
 delay(1000); // Let tasks initialize their stacks first
 otaStartBackgroundCheck();
 } else if (!ota_check_enabled) {
 logInfo("[BOOT] OTA GitHub check disabled (saves 16KB SSL memory)");
 }
 
 logInfo("[BOOT] [OK] Complete in %lu ms", (unsigned long)(millis() - boot_time_ms));

 // OTA Rollback: Validate firmware 60 seconds after boot
 // If firmware crashes before this timer fires, ESP-IDF auto-reverts to previous partition
 TimerHandle_t otaTimer = xTimerCreate("ota_valid", pdMS_TO_TICKS(60000), pdFALSE, NULL, otaValidationTimerCallback);
 if (otaTimer) {
 xTimerStart(otaTimer, 0);
 logInfo("[OTA] Firmware validation timer started (60s)");
 }
}

volatile uint32_t accumulated_loop_count = 0;

void loop() {
 networkManager.update();
 jobManager.update();
 engineeringMenu.update();
 accumulated_loop_count++;
 delay(10); 
}

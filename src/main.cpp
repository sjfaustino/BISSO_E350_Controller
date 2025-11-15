#include <Arduino.h>
#include "config_unified.h"
#include "motion.h"
#include "encoder_wj66.h"
#include "encoder_calibration.h"
#include "plc_iface.h"
#include "safety.h"
#include "cli.h"
#include "lcd_interface.h"

static bool system_ready = false;
static uint32_t boot_time_ms = 0;
static uint32_t uptime_seconds = 0;
static uint32_t last_uptime_update = 0;

void setup() {
  // Serial initialization
  Serial.begin(115200);
  delay(1000);
  
  boot_time_ms = millis();
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     BISSO v4.2 Production Firmware     ║");
  Serial.println("║          ESP32-S3 Bridge Saw            ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.print("Boot started at: ");
  Serial.print(boot_time_ms);
  Serial.println(" ms");

  // Initialize systems in order
  Serial.println("\n[BOOT] Initializing systems...\n");
  
  Serial.println("[BOOT] 1/8 - Configuration system");
  configUnifiedInit();
  delay(100);
  
  Serial.println("[BOOT] 2/8 - Safety system");
  safetyInit();
  delay(100);
  
  Serial.println("[BOOT] 3/8 - Motion system");
  motionInit();
  delay(100);
  
  Serial.println("[BOOT] 4/8 - Encoder system");
  wj66Init();
  delay(100);
  
  Serial.println("[BOOT] 5/8 - Encoder calibration");
  encoderCalibrationInit();
  delay(100);
  
  Serial.println("[BOOT] 6/8 - PLC interface");
  plcIfaceInit();
  delay(100);
  
  Serial.println("[BOOT] 7/8 - LCD interface");
  lcdInterfaceInit();
  delay(100);
  
  Serial.println("[BOOT] 8/8 - Command Line Interface");
  cliInit();
  delay(100);
  
  system_ready = true;
  uint32_t boot_duration = millis() - boot_time_ms;
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.print("║ System ready in ");
  Serial.print(boot_duration);
  Serial.println(" ms");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Set initial LCD display
  lcdInterfacePrintLine(0, "BISSO v4.2");
  lcdInterfacePrintLine(1, "System Ready");
  lcdInterfacePrintLine(2, "Waiting for commands");
  lcdInterfacePrintLine(3, "Type: help");
  
  last_uptime_update = millis();
}

void loop() {
  if (!system_ready) {
    delay(10);
    return;
  }
  
  // Update uptime counter
  if (millis() - last_uptime_update >= 1000) {
    uptime_seconds++;
    last_uptime_update = millis();
  }
  
  // Safety checks FIRST (highest priority)
  safetyUpdate();
  
  // Motion control
  motionUpdate();
  
  // Encoder updates
  wj66Update();
  encoderCalibrationUpdate();
  
  // PLC interface
  plcIfaceUpdate();
  
  // LCD updates
  lcdInterfaceUpdate();
  
  // CLI processing (user input)
  cliUpdate();
  
  // Minimal delay to prevent watchdog timeout
  delayMicroseconds(100);
}

/**
 * @file cli_lcd.cpp
 * @brief LCD control CLI commands (PosiPro)
 */

#include "cli.h"
#include "lcd_interface.h"
#include "lcd_sleep.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>
#include <stdlib.h>
#include <Wire.h>

void cmd_lcd_on() {
    configSetInt(KEY_LCD_EN, 1);
    lcdInterfaceSetMode(LCD_MODE_I2C);
    lcdInterfaceBacklight(true);
    logInfo("[LCD] Enabled");
}

void cmd_lcd_off() {
    configSetInt(KEY_LCD_EN, 0);
    lcdInterfaceBacklight(false);
    lcdInterfaceSetMode(LCD_MODE_NONE);
    logInfo("[LCD] Disabled");
}

void cmd_lcd_backlight(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("Usage: lcd backlight [on|off]");
        return;
    }
    bool on = (strcasecmp(argv[2], "on") == 0);
    lcdInterfaceBacklight(on);
    logInfo("[LCD] Backlight %s", on ? "ON" : "OFF");
}

void cmd_lcd_timeout(int argc, char** argv) {
    if (argc < 3) {
        logPrintf("Current timeout: %lu seconds\r\n", (unsigned long)lcdSleepGetTimeout());
        logPrintln("Usage: lcd timeout <seconds>");
        return;
    }
    uint32_t seconds = strtoul(argv[2], NULL, 10);
    lcdSleepSetTimeout(seconds);
}

void cmd_lcd_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("\r\n[LCD] === LCD Control ===");
        logPrintln("Usage: lcd [on|off|backlight|sleep|wakeup|timeout|status|reset|scan|test]");
        logPrintln("  on        - Enable LCD and save setting");
        logPrintln("  off       - Disable LCD and save setting");
        logPrintln("  reset     - Reset I2C errors and try to re-enable I2C mode");
        logPrintln("  backlight - Control backlight (on/off)");
        logPrintln("  sleep     - Force display to sleep");
        logPrintln("  wakeup    - Force display to wake up");
        logPrintln("  timeout   - Set sleep timeout in seconds (0=never)");
        logPrintln("  status    - Show LCD status");
        logPrintln("  scan      - Scan I2C bus for LCD backpack (0x27, 0x3F)");
        logPrintln("  test      - Run hardware test pattern");
        return;
    }

    if (strcasecmp(argv[1], "on") == 0) {
        cmd_lcd_on();
    } else if (strcasecmp(argv[1], "off") == 0) {
        cmd_lcd_off();
    } else if (strcasecmp(argv[1], "backlight") == 0) {
        cmd_lcd_backlight(argc, argv);
    } else if (strcasecmp(argv[1], "sleep") == 0) {
        lcdSleepSleep();
    } else if (strcasecmp(argv[1], "wakeup") == 0) {
        lcdSleepWakeup();
    } else if (strcasecmp(argv[1], "timeout") == 0) {
        cmd_lcd_timeout(argc, argv);
    } else if (strcasecmp(argv[1], "reset") == 0) {
        lcdInterfaceResetErrors();
    } else if (strcasecmp(argv[1], "status") == 0) {
        logPrintln("\r\n[LCD] === Status ===");
        logPrintf("Enabled:   %s\r\n", configGetInt(KEY_LCD_EN, 1) ? "YES" : "NO");
        logPrintf("Mode:      %d\r\n", (int)lcdInterfaceGetMode());
        logPrintf("Sleeping:  %s\r\n", lcdSleepIsAsleep() ? "YES" : "NO");
        logPrintf("Timeout:   %lu sec\r\n", (unsigned long)lcdSleepGetTimeout());
        lcdInterfaceDiagnostics();
    } else if (strcasecmp(argv[1], "scan") == 0) {
        logPrintln("\r\n[LCD] Scanning I2C Bus for LCD...");
        uint8_t addrs[] = {0x27, 0x3F};
        bool found = false;
        for (uint8_t a : addrs) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
                logInfo("[LCD] Found LCD at 0x%02X", a);
                found = true;
            }
        }
        if (!found) logWarning("[LCD] No LCD found at standard addresses (0x27, 0x3F)");
    } else if (strcasecmp(argv[1], "test") == 0) {
        lcdInterfaceTest();
    } else {
        logWarning("[LCD] Unknown subcommand: %s", argv[1]);
    }
}

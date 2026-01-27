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
        CLI_USAGE("lcd", "backlight [on|off]");
        return;
    }
    bool on = (strcasecmp(argv[2], "on") == 0);
    lcdInterfaceBacklight(on);
    logInfo("[LCD] Backlight %s", on ? "ON" : "OFF");
}

void cmd_lcd_timeout(int argc, char** argv) {
    if (argc < 3) {
        logPrintf("Current timeout: %lu seconds\r\n", (unsigned long)lcdSleepGetTimeout());
        CLI_USAGE("lcd", "timeout <seconds>");
        return;
    }
    uint32_t seconds = strtoul(argv[2], NULL, 10);
    lcdSleepSetTimeout(seconds);
}

// ============================================================================
// WRAPPER/HANDLER FUNCTIONS (for table-driven dispatch)
// ============================================================================

static void wrap_lcd_on(int argc, char** argv) { (void)argc; (void)argv; cmd_lcd_on(); }
static void wrap_lcd_off(int argc, char** argv) { (void)argc; (void)argv; cmd_lcd_off(); }
static void wrap_lcd_sleep(int argc, char** argv) { (void)argc; (void)argv; lcdSleepSleep(); }
static void wrap_lcd_wakeup(int argc, char** argv) { (void)argc; (void)argv; lcdSleepWakeup(); }
static void wrap_lcd_reset(int argc, char** argv) { (void)argc; (void)argv; lcdInterfaceResetErrors(); }
static void wrap_lcd_test(int argc, char** argv) { (void)argc; (void)argv; lcdInterfaceTest(); }

static void wrap_lcd_status(int argc, char** argv) {
    (void)argc; (void)argv;
    logPrintln("\r\n[LCD] === Status ===");
    logPrintf("Enabled:   %s\r\n", configGetInt(KEY_LCD_EN, 1) ? "YES" : "NO");
    logPrintf("Mode:      %d\r\n", (int)lcdInterfaceGetMode());
    logPrintf("Sleeping:  %s\r\n", lcdSleepIsAsleep() ? "YES" : "NO");
    logPrintf("Timeout:   %lu sec\r\n", (unsigned long)lcdSleepGetTimeout());
    lcdInterfaceDiagnostics();
}

static void wrap_lcd_scan(int argc, char** argv) {
    (void)argc; (void)argv;
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
}

// ============================================================================
// MAIN COMMAND HANDLER (P7: Table-Driven Dispatch)
// ============================================================================

void cmd_lcd_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"on",        wrap_lcd_on,        "Enable LCD and save setting"},
        {"off",       wrap_lcd_off,       "Disable LCD and save setting"},
        {"backlight", cmd_lcd_backlight,  "Control backlight (on/off)"},
        {"sleep",     wrap_lcd_sleep,     "Force display to sleep"},
        {"wakeup",    wrap_lcd_wakeup,    "Force display to wake up"},
        {"timeout",   cmd_lcd_timeout,    "Set sleep timeout in seconds (0=never)"},
        {"reset",     wrap_lcd_reset,     "Reset I2C errors and re-enable"},
        {"status",    wrap_lcd_status,    "Show LCD status"},
        {"scan",      wrap_lcd_scan,      "Scan I2C bus for LCD backpack"},
        {"test",      wrap_lcd_test,      "Run hardware test pattern"}
    };

    if (argc < 2) {
        logPrintln("\r\n[LCD] === LCD Control ===");
    }

    cliDispatchSubcommand("[LCD]", argc, argv, subcmds,
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

/**
 * @file cli_jxk10.cpp
 * @brief JXK-10 Current Sensor CLI Commands
 * @project BISSO E350 Controller
 */

#include "cli.h"
#include "jxk10_modbus.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// SUBCOMMAND HANDLERS
// ============================================================================

static void cmd_jxk10_read() {
    const jxk10_state_t* state = jxk10GetState();
    if (!state->enabled) {
        logWarning("[JXK10] Sensor is disabled");
        return;
    }
    
    // Trigger immediate poll and wait briefly
    jxk10ModbusReadCurrent();
    delay(150);  // Allow time for response
    
    logPrintln("\n[JXK10] === Current Reading ===");
    logPrintf("Current:    %.2f A\r\n", state->current_amps);
    logPrintf("Raw Value:  %d\r\n", state->current_raw);
    logPrintf("Last Read:  %lu ms ago\r\n", (unsigned long)(millis() - state->last_read_time_ms));
}

static void cmd_jxk10_info() {
    const jxk10_state_t* state = jxk10GetState();
    
    logPrintln("\n[JXK10] === Device Info ===");
    logPrintf("Enabled:       %s\r\n", state->enabled ? "YES" : "NO");
    logPrintf("Slave Address: %u (0x%02X)\r\n", state->slave_address, state->slave_address);
    logPrintf("Baud Rate:     %lu bps\r\n", (unsigned long)state->baud_rate);
    logPrintf("Read Count:    %lu\r\n", (unsigned long)state->read_count);
    logPrintf("Error Count:   %lu\r\n", (unsigned long)state->error_count);
}

static void cmd_jxk10_addr(int argc, char** argv) {
    if (argc < 3) {
        CLI_USAGE("jxk10", "addr <new_address>");
        logPrintln("  Address range: 0-254");
        logPrintln("  NOTE: Power cycle required after change!");
        return;
    }
    
    uint8_t new_addr = (uint8_t)strtoul(argv[2], NULL, 10);
    if (new_addr > 254) {
        logError("[JXK10] Invalid address: %u (max 254)", new_addr);
        return;
    }
    
    logInfo("[JXK10] Changing address to %u...", new_addr);
    if (jxk10ModbusSetSlaveAddress(new_addr)) {
        logInfo("[JXK10] Address changed successfully");
        logWarning("[JXK10] POWER CYCLE REQUIRED for change to take effect!");
    } else {
        logError("[JXK10] Failed to change address");
    }
}

static void cmd_jxk10_status() {
    jxk10PrintDiagnostics();
}

static void cmd_jxk10_enable() {
    configSetInt(KEY_JXK10_ENABLED, 1);
    logInfo("[JXK10] Enabled in configuration (restart required)");
}

static void cmd_jxk10_disable() {
    configSetInt(KEY_JXK10_ENABLED, 0);
    logInfo("[JXK10] Disabled in configuration (restart required)");
}

// ============================================================================
// MAIN COMMAND HANDLER
// ============================================================================

void cmd_jxk10_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("\n[JXK10] === JXK-10 Current Sensor ===");
        CLI_USAGE("jxk10", "[read|info|addr|status|enable|disable]");
        CLI_HELP_LINE("read", "Read current value");
        CLI_HELP_LINE("info", "Show device info (address, baud, stats)");
        CLI_HELP_LINE("addr", "Change slave address");
        CLI_HELP_LINE("status", "Show full diagnostics");
        CLI_HELP_LINE("enable", "Enable JXK-10 in config");
        CLI_HELP_LINE("disable", "Disable JXK-10 in config");
        return;
    }

    if (strcasecmp(argv[1], "read") == 0) {
        cmd_jxk10_read();
    } else if (strcasecmp(argv[1], "info") == 0) {
        cmd_jxk10_info();
    } else if (strcasecmp(argv[1], "addr") == 0) {
        cmd_jxk10_addr(argc, argv);
    } else if (strcasecmp(argv[1], "status") == 0) {
        cmd_jxk10_status();
    } else if (strcasecmp(argv[1], "enable") == 0) {
        cmd_jxk10_enable();
    } else if (strcasecmp(argv[1], "disable") == 0) {
        cmd_jxk10_disable();
    } else {
        logWarning("[JXK10] Unknown subcommand: %s", argv[1]);
    }
}

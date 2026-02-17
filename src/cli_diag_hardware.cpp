/**
 * @file cli_diag_hardware.cpp
 * @brief Hardware-related diagnostic CLI commands (DIO, Spindle, I2C, Modbus)
 */

#include "cli.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "spindle_current_monitor.h"
#include "jxk10_modbus.h"
#include "rs485_device_registry.h"
#include "i2c_bus_recovery.h"
#include "board_inputs.h"
#include "board_variant.h"
#include <Wire.h>
#include <Arduino.h>

// Shared CLI utilities provided by cli.h

void cmd_dio_main(int argc, char** argv) {
    (void)argc; (void)argv;
    watchdogFeed("CLI");
    
    static char output[2048];
    int pos = 0;
    
    static const char* input1_labels[] = {"Limit-X", "Limit-Y", "Limit-Z", "E-Stop", "Pause", "Resume", "Probe", "Door"};
    static const char* input2_labels[] = {"Home-X", "Home-Y", "Home-Z", "Home-A", "ToolSns", "Coolant", "In-15", "In-16"};
    static const char* output1_labels[] = {"Spindle", "SpinDir", "Coolant", "Mist", "Clamp", "Vacuum", "Light", "Out-8"};
    static const char* output2_labels[] = {"AirBlast", "Lube", "Alarm", "Ready", "Running", "Error", "Out-15", "Out-16"};
    
    struct { uint8_t addr; const char* name; const char** labels; bool is_output; } banks[] = {
        {0x22, "INPUTS-SAFE", input1_labels, false},
        {0x21, "INPUTS-AUX", input2_labels, false},
        {0x24, "OUTPUTS-MAIN", output1_labels, true},
        {0x25, "OUTPUTS-AUX", output2_labels, true}
    };
    
    pos += snprintf(output + pos, sizeof(output) - pos, "\n=== Digital I/O Status ===\n\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "+---------+----------------+------------------------------------------------------------------+\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "| Addr    | Name           | State (MSB..LSB)                                                 |\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "+---------+----------------+------------------------------------------------------------------+\n");
    
    for (int b = 0; b < 4; b++) {
        Wire.beginTransmission(banks[b].addr);
        if (Wire.endTransmission() != 0) {
            pos += snprintf(output + pos, sizeof(output) - pos, "| N/A     | %-14s | [NOT CONNECTED]                                                  |\n", banks[b].name);
            continue;
        }
        
        Wire.requestFrom(banks[b].addr, (uint8_t)1);
        uint8_t state = Wire.available() ? Wire.read() : 0xFF;
        
        char bits[9];
        for (int i = 7; i >= 0; i--) bits[7-i] = (state & (1 << i)) ? '1' : '0';
        bits[8] = '\0';
        
        pos += snprintf(output + pos, sizeof(output) - pos, "| 0x%02X    | %-14s | %s (0x%02X)                                                  |\n", 
                        banks[b].addr, banks[b].name, bits, state);
        
        char active_buf[128] = "";
        int apos = 0;
        int count = 0;
        for (int i = 0; i < 8; i++) {
            bool active = banks[b].is_output ? !(state & (1 << i)) : (state & (1 << i));
            if (active) {
                if (count > 0) apos += snprintf(active_buf + apos, sizeof(active_buf) - apos, ", ");
                apos += snprintf(active_buf + apos, sizeof(active_buf) - apos, "%s", banks[b].labels[i]);
                count++;
            }
        }
        if (count == 0) snprintf(active_buf, sizeof(active_buf), "(none active)");
        pos += snprintf(output + pos, sizeof(output) - pos, "|         |                | %-64s |\n", active_buf);
    }
    
    pos += snprintf(output + pos, sizeof(output) - pos, "+---------+----------------+------------------------------------------------------------------+\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "Legend: Inputs=HIGH when active, Outputs=LOW when relay ON\n");
    
    serialLoggerLock();
    Serial.print(output);
    Serial.flush();
    serialLoggerUnlock();
}

void cmd_spindle_diag(int argc, char** argv) { 
    (void)argc; (void)argv; 
    spindleMonitorPrintDiagnostics();
    jxk10PrintDiagnostics();
    rs485PrintDiagnostics();
}

void cmd_spindle_alarm(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("\nAlarm commands:");
        logPrintln("  spindle alarm status   - Show alarm states");
        logPrintln("  spindle alarm clear    - Clear all alarms");
        logPrintln("  spindle alarm toolbreak <amps> - Set threshold (1-20A)");
        logPrintln("  spindle alarm stall <amps> <ms> - Set stall params");
        return;
    }
    
    const spindle_monitor_state_t* state = spindleMonitorGetState();
    
    if (strcasecmp(argv[2], "status") == 0) {
        logPrintln("\n=== Alarm Status ===");
        logPrintf("Tool Breakage: %s (count: %lu)\r\n", 
                     state->alarm_tool_breakage ? "ACTIVE" : "OK",
                     (unsigned long)state->tool_breakage_count);
        logPrintf("Stall:         %s (count: %lu)\r\n",
                     state->alarm_stall ? "ACTIVE" : "OK",
                     (unsigned long)state->stall_count);
        logPrintf("Thresholds: %.1f A drop, %.1f A for %lu ms\r\n", 
                     state->tool_breakage_drop_amps,
                     state->stall_threshold_amps,
                     (unsigned long)state->stall_timeout_ms);
    } else if (strcasecmp(argv[2], "clear") == 0) {
        spindleMonitorClearAlarms();
    } else if (strcasecmp(argv[2], "toolbreak") == 0 && argc >= 4) {
        spindleMonitorSetToolBreakageThreshold(atof(argv[3]));
    } else if (strcasecmp(argv[2], "stall") == 0 && argc >= 5) {
        spindleMonitorSetStallParams(atof(argv[3]), atoi(argv[4]));
    }
}

static void rs485_send_and_receive(const uint8_t* payload, size_t len) {
    rs485ClearBuffer();
    if (!rs485Send(payload, len)) {
        logError("Failed to send");
        return;
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
    uint8_t rx_buf[128];
    uint8_t rx_len = 0;
    if (rs485Receive(rx_buf, &rx_len) && rx_len > 0) {
        logPrintf("Received %d bytes: ", rx_len);
        for (int i = 0; i < rx_len; i++) {
            if (rx_buf[i] >= 32 && rx_buf[i] <= 126) logPrintf("%c", rx_buf[i]);
            else logPrintf("[%02X]", rx_buf[i]);
        }
        logPrintln("");
    } else {
        logPrintf("No response received\n");
    }
}

void cmd_rs485_raw(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("Usage: rs485 raw <string>");
        logPrintln("  Example: rs485 raw #00\\r");
        return;
    }
    char payload[64];
    strncpy(payload, argv[1], sizeof(payload)-1);
    payload[sizeof(payload)-1] = '\0';
    char* r = strstr(payload, "\\r");
    if (r) { *r = '\r'; memmove(r+1, r+2, strlen(r+2)+1); }
    char* n = strstr(payload, "\\n");
    if (n) { *n = '\n'; memmove(n+1, n+2, strlen(n+2)+1); }
    logPrintf("Sending: %s (%d bytes)\r\n", argv[1], (int)strlen(payload));
    rs485_send_and_receive((const uint8_t*)payload, strlen(payload));
}

void cmd_rs485_hex(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("Usage: rs485 hex <hex bytes...>");
        logPrintln("  Example: rs485 hex 23 30 30 0D (sends #00\\r)");
        return;
    }
    uint8_t payload[64];
    uint8_t len = 0;
    for (int i = 1; i < argc && len < sizeof(payload); i++) {
        payload[len++] = (uint8_t)strtol(argv[i], NULL, 16);
    }
    logPrintf("Sending Hex (%d bytes)\r\n", len);
    rs485_send_and_receive(payload, len);
}

void cmd_rs485_diag(int argc, char** argv) { (void)argc; (void)argv; rs485PrintDiagnostics(); }
void cmd_rs485_reset(int argc, char** argv) { (void)argc; (void)argv; rs485ResetErrorCounters(); }

// ============================================================================
// MODBUS SNIFFER
// ============================================================================

static uint32_t sniff_end_time = 0;

static void rs485_sniff_cb(bool is_tx, const uint8_t* data, uint16_t len) {
    if (millis() > sniff_end_time) {
        return;
    }
    
    // Format to hex
    char hex[512] = "";
    int hpos = 0;
    for (uint16_t i = 0; i < len && hpos < (int)sizeof(hex)-4; i++) {
        hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "%02X ", data[i]);
    }
    
    // Print packet (logPrintf is thread-safe)
    logPrintf("[%8lu] [RS485] %s: %s\n", (unsigned long)millis(), is_tx ? "TX" : "RX", hex);
}

void cmd_rs485_sniff(int argc, char** argv) {
    uint32_t duration_sec = 10;
    if (argc >= 3) duration_sec = atoi(argv[2]);
    if (duration_sec == 0) duration_sec = 10;
    if (duration_sec > 600) duration_sec = 600; // Cap at 10 mins
    
    logPrintf("Sniffing RS485 for %lu seconds... (Press any key to stop)\n", (unsigned long)duration_sec);
    sniff_end_time = millis() + (duration_sec * 1000);
    
    rs485SetSniffer(rs485_sniff_cb);
    
    while (millis() < sniff_end_time) {
        if (Serial.available()) {
            while (Serial.available()) Serial.read(); // Clear input
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
        watchdogFeed("CLI");
    }
    
    rs485SetSniffer(NULL);
    logPrintln("Sniffer stopped.");
}

// ============================================================================
// MAIN RS485 DISPATCHER
// ============================================================================

void cmd_rs485_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"raw",     cmd_rs485_raw,      "<string> - Send raw ASCII string"},
        {"hex",     cmd_rs485_hex,      "<hex...> - Send raw hex bytes"},
        {"sniff",   cmd_rs485_sniff,    "[duration_sec] - Monitor bus traffic"},
        {"diag",    cmd_rs485_diag,     "Show detailed registry diagnostics"},
        {"reset",   cmd_rs485_reset,    "Reset error counters"}
    };
    
    if (argc < 2) {
        logPrintln("\n=== RS-485 Bus Management ===");
    }
    
    cliDispatchSubcommand("[RS485]", argc, argv, subcmds, sizeof(subcmds)/sizeof(subcmds[0]), 1);
}



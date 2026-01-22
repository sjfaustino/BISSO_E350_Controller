/**
 * @file rs485_autodetect.cpp
 * @brief RS485 Bus Baud Rate Autodetect Implementation
 */

#include "rs485_autodetect.h"
#include "rs485_device_registry.h"
#include "modbus_rtu.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <Arduino.h>

int32_t rs485AutodetectBaud(void) {
    // Suspend RS485 bus activity during scan
    rs485SetBusPaused(true);
    
    // Acquire bus mutex (critical for preventing background task interference)
    if (!rs485TakeBus(500)) {
        logError("[RS485_DET] Failed to acquire bus mutex for scan");
        rs485SetBusPaused(false);
        return -1;
    }

    const uint32_t rates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200};
    uint32_t found_rate = 0;
    
    logInfo("[RS485_DET] Starting baud rate scan...");
    
    // Determine which devices to probe based on configuration
    bool jxk_en = configGetInt(KEY_JXK10_ENABLED, 1) != 0;
    bool vfd_en = configGetInt(KEY_VFD_EN, 1) != 0;
    bool yhtc05_en = configGetInt(KEY_YHTC05_ENABLED, 1) != 0;
    
    uint8_t probe_addrs[3];
    uint8_t probe_count = 0;
    
    if (jxk_en) probe_addrs[probe_count++] = (uint8_t)configGetInt(KEY_JXK10_ADDR, 1);
    if (vfd_en) probe_addrs[probe_count++] = (uint8_t)configGetInt(KEY_VFD_ADDR, 2);
    // Hardcoded address 3 for now until KEY_YHTC05_ADDR is added
    if (yhtc05_en) probe_addrs[probe_count++] = 3; 

    if (probe_count == 0) {
        logWarning("[RS485_DET] No RS485 devices are enabled in configuration. Aborting.");
        return -1;
    }

    uint8_t tx_buffer[8];
    uint8_t rx_buffer[32];

    for (uint32_t rate : rates) {
        logInfo("[RS485_DET] Probing %lu baud (devices: %u)...", (unsigned long)rate, probe_count);
        
        // Re-init registry/UART with this baud
        rs485SetBaudRate(rate);
        
        for (uint8_t i = 0; i < probe_count; i++) {
            uint8_t addr = probe_addrs[i];
            // Build a Modbus Read Registers request (Read 1 register from 0x0000)
            // This is a safe query for both devices
            uint16_t tx_len = modbusReadRegistersRequest(addr, 0x0000, 1, tx_buffer);
            
            // Clear RX
            rs485ClearBuffer();
            
            // Send request
            rs485Send(tx_buffer, (uint8_t)tx_len);
            
            // Wait for response
            uint32_t start = millis();
            bool got_reply = false;
            while (millis() - start < 150) {
                if (rs485Available() >= 5) { // Min Modbus response size
                    uint8_t rx_len = sizeof(rx_buffer);
                    if (rs485Receive(rx_buffer, &rx_len)) {
                        // Check if it's a valid response for this address
                        if (rx_buffer[0] == addr) {
                            found_rate = rate;
                            got_reply = true;
                            logInfo("[RS485_DET] Found device at address %u @ %lu baud!", addr, (unsigned long)rate);
                            break;
                        }
                    }
                }
                vTaskDelay(5 / portTICK_PERIOD_MS);
            }
            if (got_reply) break;
            
            vTaskDelay(20 / portTICK_PERIOD_MS); // Gap between probes
        }
        
        if (found_rate) break;
    }
    
    if (found_rate) {
        logInfo("[RS485_DET] SUCCESS: Bus baud set to %lu", (unsigned long)found_rate);
        configSetInt(KEY_RS485_BAUD, found_rate);
        configUnifiedSave();
    } else {
        logWarning("[RS485_DET] FAILED: No devices responded.");
        // Restore to config value
        uint32_t restore_rate = configGetInt(KEY_RS485_BAUD, 9600);
        rs485SetBaudRate(restore_rate);
    }
    
    // Release bus mutex
    rs485ReleaseBus();
    
    // Resume background activity
    rs485SetBusPaused(false);
    
    return found_rate;
}

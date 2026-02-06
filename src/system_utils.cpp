/**
 * @file system_utils.cpp
 * @brief System-wide utility functions implementation
 * @project BISSO E350 Controller
 */

#include "system_utils.h"
#include "serial_logger.h"
#include "board_variant.h"

// Conditional includes for optional features
#if BOARD_HAS_SDCARD
#include "sd_card_manager.h"
#endif

/**
 * @brief Safely reboot the system with proper cleanup
 */
void systemSafeReboot(const char* reason) {
    // Log the reboot reason
    if (reason) {
        logWarning("[SYSTEM] Rebooting: %s", reason);
    } else {
        logInfo("[SYSTEM] Rebooting...");
    }
    
    // Step 1: Unmount SD card if mounted (prevent FAT corruption)
    #if BOARD_HAS_SDCARD
    if (sdCardIsMounted()) {
        logInfo("[SYSTEM] Unmounting SD card...");
        sdCardUnmount();
    }
    #endif
    
    // Step 2: Flush serial buffers to ensure messages are sent
    Serial.flush();
    
    // Step 3: Brief delay to allow cleanup
    delay(100);
    
    // Step 4: Perform the actual restart
    ESP.restart();
    
    // Should never reach here
    while(1) { delay(100); }
}

/**
 * @brief Emergency reboot with minimal cleanup
 * 
 * Used in critical error handlers where full cleanup may cause issues
 */
void systemEmergencyReboot() {
    // Minimal logging (may fail if in critical state)
    Serial.println("[SYSTEM] Emergency reboot!");
    Serial.flush();
    delay(50);
    
    // Direct restart - skip SD unmount to avoid potential issues
    ESP.restart();
    
    while(1) { delay(100); }
}

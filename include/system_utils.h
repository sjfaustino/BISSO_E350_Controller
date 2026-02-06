/**
 * @file system_utils.h
 * @brief System-wide utility functions
 * @project BISSO E350 Controller
 * 
 * Provides safe reboot, shutdown, and other system-wide utilities.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Safely reboot the system
 * 
 * Performs a controlled shutdown sequence before rebooting:
 * 1. Unmounts SD card (if mounted) to prevent filesystem corruption
 * 2. Flushes any pending configuration writes to NVS
 * 3. Allows serial buffers to empty
 * 4. Calls ESP.restart()
 * 
 * @param reason Optional reason for the reboot (for logging)
 */
void systemSafeReboot(const char* reason = nullptr);

/**
 * @brief Emergency reboot (minimal cleanup)
 * 
 * Use this only for critical error recovery where full cleanup
 * may cause issues (e.g., stack overflow handler)
 */
void systemEmergencyReboot();

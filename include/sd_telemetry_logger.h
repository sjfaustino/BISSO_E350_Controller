/**
 * @file sd_telemetry_logger.h
 * @brief Background machine state logging to SD card
 */

#ifndef SD_TELEMETRY_LOGGER_H
#define SD_TELEMETRY_LOGGER_H

#include <Arduino.h>

/**
 * @brief Initialize the SD telemetry logger
 * Creates log directories and opens the initial file
 * @return true if successful
 */
bool sdTelemetryLoggerInit();

/**
 * @brief Log current system state to SD card
 * Should be called periodically (e.g., 1Hz)
 */
void sdTelemetryLoggerUpdate();

#endif // SD_TELEMETRY_LOGGER_H

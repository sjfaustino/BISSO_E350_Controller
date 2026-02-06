/**
 * @file telemetry_history.h
 * @brief 1-Hour Rolling History for System Telemetry (PSRAM Optimized)
 */

#ifndef TELEMETRY_HISTORY_H
#define TELEMETRY_HISTORY_H

#include "system_telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

// 3600 samples = 1 hour @ 1Hz
#define TELEMETRY_HISTORY_DEPTH 3600

/**
 * @brief Initialize the telemetry history buffer in PSRAM
 */
void telemetryHistoryInit();

/**
 * @brief Add a new sample to the history
 * @param packet Binary telemetry packet
 */
void telemetryHistoryAdd(const telemetry_packet_t* packet);

/**
 * @brief Get all available history samples
 * @param buffer Output buffer (must be at least TELEMETRY_HISTORY_DEPTH * sizeof(telemetry_packet_t))
 * @param count Pointer to store actual number of samples returned
 */
void telemetryHistoryGet(telemetry_packet_t* buffer, uint16_t* count);

/**
 * @brief Get the number of samples currently in history
 */
uint16_t telemetryHistoryGetCount();

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_HISTORY_H

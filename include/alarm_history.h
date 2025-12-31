/**
 * @file alarm_history.h
 * @brief Alarm History Storage for Web UI
 * @project BISSO E350 Controller
 */

#ifndef ALARM_HISTORY_H
#define ALARM_HISTORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ALARM_HISTORY_MAX 50  // Store last 50 alarms

typedef enum {
    ALARM_SEVERITY_INFO = 0,
    ALARM_SEVERITY_WARNING,
    ALARM_SEVERITY_ERROR,
    ALARM_SEVERITY_CRITICAL
} alarm_severity_t;

typedef struct {
    uint32_t timestamp;         // Unix timestamp
    uint32_t uptime_ms;         // System uptime when alarm occurred
    alarm_severity_t severity;  // Alarm severity level
    char message[64];           // Alarm message
    char source[16];            // Source module (SPINDLE, MOTION, etc.)
    bool acknowledged;          // Has operator acknowledged
} alarm_entry_t;

/**
 * @brief Initialize alarm history system
 */
void alarmHistoryInit(void);

/**
 * @brief Add new alarm to history
 * @param severity Alarm severity level
 * @param source Source module name
 * @param message Alarm message
 */
void alarmHistoryAdd(alarm_severity_t severity, const char* source, const char* message);

/**
 * @brief Get alarm entry by index (0 = newest)
 * @param index Alarm index (0 to count-1)
 * @param entry Pointer to entry to fill
 * @return true if entry found
 */
bool alarmHistoryGet(uint32_t index, alarm_entry_t* entry);

/**
 * @brief Get total alarm count
 */
uint32_t alarmHistoryCount(void);

/**
 * @brief Clear all alarms
 */
void alarmHistoryClear(void);

/**
 * @brief Acknowledge alarm by index
 */
void alarmHistoryAcknowledge(uint32_t index);

/**
 * @brief Acknowledge all alarms
 */
void alarmHistoryAcknowledgeAll(void);

/**
 * @brief Get unacknowledged alarm count
 */
uint32_t alarmHistoryUnacknowledgedCount(void);

/**
 * @brief Print alarm history to CLI
 */
void alarmHistoryPrint(void);

/**
 * @brief Export alarm history as JSON string
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of bytes written
 */
int alarmHistoryExportJSON(char* buffer, size_t buffer_size);

// Convenience macros for adding alarms
#define ALARM_INFO(src, msg) alarmHistoryAdd(ALARM_SEVERITY_INFO, src, msg)
#define ALARM_WARNING(src, msg) alarmHistoryAdd(ALARM_SEVERITY_WARNING, src, msg)
#define ALARM_ERROR(src, msg) alarmHistoryAdd(ALARM_SEVERITY_ERROR, src, msg)
#define ALARM_CRITICAL(src, msg) alarmHistoryAdd(ALARM_SEVERITY_CRITICAL, src, msg)

#endif // ALARM_HISTORY_H

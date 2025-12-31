/**
 * @file alarm_history.cpp
 * @brief Alarm History Storage for Web UI
 * @project BISSO E350 Controller
 */

#include "alarm_history.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>

// Circular buffer for alarm history
static alarm_entry_t alarm_buffer[ALARM_HISTORY_MAX];
static uint32_t alarm_head = 0;      // Newest entry index
static uint32_t alarm_count = 0;     // Total entries stored
static uint32_t total_alarms = 0;    // Total alarms ever recorded

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

static uint32_t getTimestamp(void) {
    time_t now;
    time(&now);
    return (uint32_t)now;
}

static const char* severityToString(alarm_severity_t severity) {
    switch (severity) {
        case ALARM_SEVERITY_INFO:     return "INFO";
        case ALARM_SEVERITY_WARNING:  return "WARNING";
        case ALARM_SEVERITY_ERROR:    return "ERROR";
        case ALARM_SEVERITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================

void alarmHistoryInit(void) {
    memset(alarm_buffer, 0, sizeof(alarm_buffer));
    alarm_head = 0;
    alarm_count = 0;
    total_alarms = 0;
    logDebug("[ALARM] Alarm history initialized");
}

void alarmHistoryAdd(alarm_severity_t severity, const char* source, const char* message) {
    // Move head to next slot (circular buffer)
    alarm_head = (alarm_head + 1) % ALARM_HISTORY_MAX;
    
    alarm_entry_t* entry = &alarm_buffer[alarm_head];
    entry->timestamp = getTimestamp();
    entry->uptime_ms = millis();
    entry->severity = severity;
    entry->acknowledged = false;
    
    strncpy(entry->source, source ? source : "SYSTEM", sizeof(entry->source) - 1);
    entry->source[sizeof(entry->source) - 1] = '\0';
    
    strncpy(entry->message, message ? message : "", sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    
    if (alarm_count < ALARM_HISTORY_MAX) {
        alarm_count++;
    }
    total_alarms++;
    
    // Log based on severity
    switch (severity) {
        case ALARM_SEVERITY_CRITICAL:
            logError("[ALARM] [%s] %s", source, message);
            break;
        case ALARM_SEVERITY_ERROR:
            logError("[ALARM] [%s] %s", source, message);
            break;
        case ALARM_SEVERITY_WARNING:
            logWarning("[ALARM] [%s] %s", source, message);
            break;
        default:
            logInfo("[ALARM] [%s] %s", source, message);
            break;
    }
}

bool alarmHistoryGet(uint32_t index, alarm_entry_t* entry) {
    if (index >= alarm_count || entry == NULL) {
        return false;
    }
    
    // Index 0 = newest, so work backwards from head
    uint32_t actual_index = (alarm_head - index + ALARM_HISTORY_MAX) % ALARM_HISTORY_MAX;
    memcpy(entry, &alarm_buffer[actual_index], sizeof(alarm_entry_t));
    return true;
}

uint32_t alarmHistoryCount(void) {
    return alarm_count;
}

void alarmHistoryClear(void) {
    memset(alarm_buffer, 0, sizeof(alarm_buffer));
    alarm_head = 0;
    alarm_count = 0;
    logInfo("[ALARM] History cleared");
}

void alarmHistoryAcknowledge(uint32_t index) {
    if (index >= alarm_count) return;
    
    uint32_t actual_index = (alarm_head - index + ALARM_HISTORY_MAX) % ALARM_HISTORY_MAX;
    alarm_buffer[actual_index].acknowledged = true;
}

void alarmHistoryAcknowledgeAll(void) {
    for (uint32_t i = 0; i < ALARM_HISTORY_MAX; i++) {
        alarm_buffer[i].acknowledged = true;
    }
    logInfo("[ALARM] All alarms acknowledged");
}

uint32_t alarmHistoryUnacknowledgedCount(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < alarm_count; i++) {
        uint32_t actual_index = (alarm_head - i + ALARM_HISTORY_MAX) % ALARM_HISTORY_MAX;
        if (!alarm_buffer[actual_index].acknowledged) {
            count++;
        }
    }
    return count;
}

void alarmHistoryPrint(void) {
    logPrintln("\n[ALARM] === Alarm History ===");
    logPrintf("  Total alarms: %lu\n", (unsigned long)total_alarms);
    logPrintf("  Stored:       %lu / %d\n", (unsigned long)alarm_count, ALARM_HISTORY_MAX);
    logPrintf("  Unacked:      %lu\n\n", (unsigned long)alarmHistoryUnacknowledgedCount());
    
    if (alarm_count == 0) {
        logPrintln("  No alarms recorded.");
        return;
    }
    
    logPrintln("  # | Severity  | Source   | Message");
    logPrintln("  --+-----------+----------+---------------------------------");
    
    alarm_entry_t entry;
    for (uint32_t i = 0; i < alarm_count && i < 20; i++) {  // Show max 20
        if (alarmHistoryGet(i, &entry)) {
            logPrintf("  %2lu| %-9s | %-8s | %s%s\n",
                      (unsigned long)i,
                      severityToString(entry.severity),
                      entry.source,
                      entry.message,
                      entry.acknowledged ? "" : " *");
        }
    }
    
    if (alarm_count > 20) {
        logPrintf("\n  ... and %lu more entries\n", (unsigned long)(alarm_count - 20));
    }
}

int alarmHistoryExportJSON(char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < 32) return 0;
    
    int offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset,
                       "{\"total\":%lu,\"count\":%lu,\"unacked\":%lu,\"alarms\":[",
                       (unsigned long)total_alarms,
                       (unsigned long)alarm_count,
                       (unsigned long)alarmHistoryUnacknowledgedCount());
    
    alarm_entry_t entry;
    for (uint32_t i = 0; i < alarm_count && i < 50; i++) {
        if (!alarmHistoryGet(i, &entry)) continue;
        
        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, ",");
        }
        
        offset += snprintf(buffer + offset, buffer_size - offset,
                           "{\"ts\":%lu,\"up\":%lu,\"sev\":%d,\"src\":\"%s\",\"msg\":\"%s\",\"ack\":%s}",
                           (unsigned long)entry.timestamp,
                           (unsigned long)entry.uptime_ms,
                           (int)entry.severity,
                           entry.source,
                           entry.message,
                           entry.acknowledged ? "true" : "false");
        
        if (offset >= (int)(buffer_size - 100)) break;  // Safety margin
    }
    
    offset += snprintf(buffer + offset, buffer_size - offset, "]}");
    return offset;
}

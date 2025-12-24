/**
 * @file system_events.cpp
 * @brief Implementation of centralized FreeRTOS event group system
 * @project BISSO E350 Controller
 */

#include "system_events.h"
#include "logger.h"
#include <Arduino.h>

// ============================================================================
// PRIVATE STATE
// ============================================================================

static EventGroupHandle_t safety_events = NULL;
static EventGroupHandle_t motion_events = NULL;
static EventGroupHandle_t system_events = NULL;

static bool initialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

bool systemEventsInit(void) {
    if (initialized) {
        logWarning("[EVENTS] Already initialized");
        return true;
    }

    // Create event groups
    safety_events = xEventGroupCreate();
    motion_events = xEventGroupCreate();
    system_events = xEventGroupCreate();

    if (!safety_events || !motion_events || !system_events) {
        logError("[EVENTS] Failed to create event groups");
        systemEventsCleanup();
        return false;
    }

    // Clear all event bits
    xEventGroupClearBits(safety_events, EVENT_SAFETY_ALL_BITS);
    xEventGroupClearBits(motion_events, EVENT_MOTION_ALL_BITS);
    xEventGroupClearBits(system_events, EVENT_SYSTEM_ALL_BITS);

    initialized = true;
    logInfo("[EVENTS] Event groups initialized");
    return true;
}

void systemEventsCleanup(void) {
    if (safety_events) {
        vEventGroupDelete(safety_events);
        safety_events = NULL;
    }
    if (motion_events) {
        vEventGroupDelete(motion_events);
        motion_events = NULL;
    }
    if (system_events) {
        vEventGroupDelete(system_events);
        system_events = NULL;
    }
    initialized = false;
    logInfo("[EVENTS] Event groups cleaned up");
}

// ============================================================================
// EVENT GROUP ACCESSORS
// ============================================================================

EventGroupHandle_t systemEventsGetSafety(void) {
    return safety_events;
}

EventGroupHandle_t systemEventsGetMotion(void) {
    return motion_events;
}

EventGroupHandle_t systemEventsGetSystem(void) {
    return system_events;
}

// ============================================================================
// SAFETY EVENTS
// ============================================================================

void systemEventsSafetySet(EventBits_t event_bits) {
    if (!safety_events) {
        logWarning("[EVENTS] Safety event group not initialized");
        return;
    }
    xEventGroupSetBits(safety_events, event_bits);
}

void systemEventsSafetyClear(EventBits_t event_bits) {
    if (!safety_events) {
        return;
    }
    xEventGroupClearBits(safety_events, event_bits);
}

EventBits_t systemEventsSafetyWait(EventBits_t event_bits, bool clear_on_exit,
                                    bool wait_all, TickType_t ticks_to_wait) {
    if (!safety_events) {
        logWarning("[EVENTS] Safety event group not initialized");
        return 0;
    }

    return xEventGroupWaitBits(
        safety_events,
        event_bits,
        clear_on_exit ? pdTRUE : pdFALSE,
        wait_all ? pdTRUE : pdFALSE,
        ticks_to_wait
    );
}

EventBits_t systemEventsGetSafetyStatus(void) {
    if (!safety_events) {
        return 0;
    }
    return xEventGroupGetBits(safety_events);
}

// ============================================================================
// MOTION EVENTS
// ============================================================================

void systemEventsMotionSet(EventBits_t event_bits) {
    if (!motion_events) {
        logWarning("[EVENTS] Motion event group not initialized");
        return;
    }
    xEventGroupSetBits(motion_events, event_bits);
}

void systemEventsMotionClear(EventBits_t event_bits) {
    if (!motion_events) {
        return;
    }
    xEventGroupClearBits(motion_events, event_bits);
}

EventBits_t systemEventsMotionWait(EventBits_t event_bits, bool clear_on_exit,
                                    bool wait_all, TickType_t ticks_to_wait) {
    if (!motion_events) {
        logWarning("[EVENTS] Motion event group not initialized");
        return 0;
    }

    return xEventGroupWaitBits(
        motion_events,
        event_bits,
        clear_on_exit ? pdTRUE : pdFALSE,
        wait_all ? pdTRUE : pdFALSE,
        ticks_to_wait
    );
}

EventBits_t systemEventsGetMotionStatus(void) {
    if (!motion_events) {
        return 0;
    }
    return xEventGroupGetBits(motion_events);
}

// ============================================================================
// SYSTEM EVENTS
// ============================================================================

void systemEventsSystemSet(EventBits_t event_bits) {
    if (!system_events) {
        logWarning("[EVENTS] System event group not initialized");
        return;
    }
    xEventGroupSetBits(system_events, event_bits);
}

void systemEventsSystemClear(EventBits_t event_bits) {
    if (!system_events) {
        return;
    }
    xEventGroupClearBits(system_events, event_bits);
}

EventBits_t systemEventsSystemWait(EventBits_t event_bits, bool clear_on_exit,
                                    bool wait_all, TickType_t ticks_to_wait) {
    if (!system_events) {
        logWarning("[EVENTS] System event group not initialized");
        return 0;
    }

    return xEventGroupWaitBits(
        system_events,
        event_bits,
        clear_on_exit ? pdTRUE : pdFALSE,
        wait_all ? pdTRUE : pdFALSE,
        ticks_to_wait
    );
}

EventBits_t systemEventsGetSystemStatus(void) {
    if (!system_events) {
        return 0;
    }
    return xEventGroupGetBits(system_events);
}

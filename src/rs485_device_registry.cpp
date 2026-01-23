/**
 * @file rs485_device_registry.cpp
 * @brief RS-485 Device Registration and Scheduling Implementation
 * @project BISSO E350 Controller
 * @details Priority-based scheduler for multiple Modbus RTU devices.
 */

#include "rs485_device_registry.h"
#include "rs485_device_registry.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>

// Use Serial2 for RS485 Bus (pins 16/13 on KC868-A16)
// ESP32-S2 only has Serial0 and Serial1
#if defined(CONFIG_IDF_TARGET_ESP32S2)
static HardwareSerial* bus_serial = &Serial1;
#else
static HardwareSerial* bus_serial = &Serial2;
#endif

#include "motion.h" // Added for motionIsMoving() check

// ============================================================================
// MODULE STATE
// ============================================================================

static rs485_registry_state_t registry = {
    .devices = {0},
    .device_count = 0,
    .current_device_index = 0,
    .last_switch_time_ms = 0,
    .baud_rate = RS485_DEFAULT_BAUD_RATE,
    .bus_busy = false,
    .total_transactions = 0,
    .total_errors = 0,
    .last_successful_response_ms = 0,
    .watchdog_alert_active = false,
    .bus_paused = false,
    .bus_mutex = NULL
};

// ============================================================================
// INITIALIZATION
// ============================================================================

bool rs485RegistryInit(uint32_t baud_rate) {
    if (baud_rate == 0) baud_rate = RS485_DEFAULT_BAUD_RATE;
    
    // registry.devices and device_count are preserved
    registry.baud_rate = baud_rate;
    if (!registry.bus_mutex) registry.bus_mutex = xSemaphoreCreateRecursiveMutex();
    registry.last_switch_time_ms = millis();
    registry.last_successful_response_ms = millis();  // Assume healthy at start
    registry.watchdog_alert_active = false;
    
    // Initialize RS485 UART
    bus_serial->begin(baud_rate, SERIAL_8N1, 16, 13);
    
    logInfo("[RS485] Registry initialized on Serial2 (baud: %lu, RX:16 TX:13)",
            (unsigned long)baud_rate);
    return true;
}

// ============================================================================
// DEVICE REGISTRATION
// ============================================================================

bool rs485RegisterDevice(rs485_device_t* device) {
    if (!device || registry.device_count >= RS485_MAX_DEVICES) {
        logError("[RS485] Cannot register device: %s",
                 device ? "registry full" : "null device");
        return false;
    }
    
    // Check for duplicate address
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (registry.devices[i]->slave_address == device->slave_address) {
            logError("[RS485] Duplicate address %d", device->slave_address);
            return false;
        }
    }
    
    // Initialize runtime stats
    device->last_poll_time_ms = 0;
    device->poll_count = 0;
    device->error_count = 0;
    device->consecutive_errors = 0;
    device->pending_response = false;
    
    // Add to registry (sorted by priority, highest first)
    uint8_t insert_idx = registry.device_count;
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (device->priority > registry.devices[i]->priority) {
            insert_idx = i;
            break;
        }
    }
    
    // Shift devices to make room
    for (uint8_t i = registry.device_count; i > insert_idx; i--) {
        registry.devices[i] = registry.devices[i - 1];
    }
    
    registry.devices[insert_idx] = device;
    registry.device_count++;
    
    logInfo("[RS485] Registered: %s (addr=%d, prio=%d, poll=%dms)",
            device->name, device->slave_address, device->priority,
            device->poll_interval_ms);
    return true;
}

bool rs485UnregisterDevice(rs485_device_t* device) {
    if (!device) return false;
    
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (registry.devices[i] == device) {
            // Shift remaining devices
            for (uint8_t j = i; j < registry.device_count - 1; j++) {
                registry.devices[j] = registry.devices[j + 1];
            }
            registry.device_count--;
            registry.devices[registry.device_count] = NULL;
            
            logInfo("[RS485] Unregistered: %s", device->name);
            return true;
        }
    }
    return false;
}

rs485_device_t* rs485FindDevice(rs485_device_type_t type) {
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (registry.devices[i]->type == type) {
            return registry.devices[i];
        }
    }
    return NULL;
}

rs485_device_t* rs485FindDeviceByAddress(uint8_t slave_address) {
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (registry.devices[i]->slave_address == slave_address) {
            return registry.devices[i];
        }
    }
    return NULL;
}

// ============================================================================
// BUS OPERATIONS
// ============================================================================

static rs485_device_t* selectNextDevice(void) {
    uint32_t now = millis();
    rs485_device_t* best = NULL;
    uint32_t longest_wait = 0;
    
    // Check global motion state once
    bool moving = motionIsMoving();
    
    for (uint8_t i = 0; i < registry.device_count; i++) {
        rs485_device_t* dev = registry.devices[i];
        
        if (!dev->enabled) continue;
        if (dev->pending_response) continue;
        
        uint32_t elapsed = now - dev->last_poll_time_ms;
        if (elapsed < dev->poll_interval_ms) continue;
        
        // OPTIMIZATION: Prioritization during motion
        if (moving && dev->priority < 5) {
            // Allow low priority devices to be skipped, BUT ensure they don't starve completely
            // If it's been waiting > 1000ms, force a poll anyway
            if (elapsed < 1000) {
                continue;
            }
        }
        
        // Weight by priority and wait time
        uint32_t score = elapsed * (dev->priority + 1);
        if (score > longest_wait) {
            longest_wait = score;
            best = dev;
        }
    }
    
    return best;
}

bool rs485Update(void) {
    if (registry.bus_paused) return false;
    uint32_t now = millis();
    
    // Enforce inter-frame delay
    if (now - registry.last_switch_time_ms < RS485_INTER_FRAME_DELAY_MS) {
        return false;
    }
    
    // Check for transaction timeout (100ms max)
    if (registry.bus_busy) {
        rs485_device_t* current = registry.devices[registry.current_device_index];
        if (current && current->pending_response) {
            if (now - current->last_poll_time_ms > 500) {
                // Timeout
                current->pending_response = false;
                current->error_count++;
                current->consecutive_errors++;
                registry.bus_busy = false;
                registry.total_errors++;
                logWarning("[RS485] Timeout: %s (Addr %d, after 500ms)", 
                           current->name, current->slave_address);
            }
        }
        return false;
    }
    
    // Select next device to poll
    rs485_device_t* next = selectNextDevice();
    if (!next) return false;
    
    // Find device index
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (registry.devices[i] == next) {
            registry.current_device_index = i;
            break;
        }
    }
    
    // Initiate poll
    if (next->poll && next->poll(next->user_data)) {
        next->last_poll_time_ms = now;
        next->pending_response = true;
        registry.bus_busy = true;
        registry.last_switch_time_ms = now;
        registry.total_transactions++;
        logDebug("[RS485] Poll -> %s (Addr %d)", next->name, next->slave_address);
        return true;
    }
    
    return false;
}

bool rs485ProcessResponse(const uint8_t* data, uint16_t len) {
    if (!registry.bus_busy || registry.current_device_index >= registry.device_count) {
        return false;
    }
    
    rs485_device_t* current = registry.devices[registry.current_device_index];
    if (!current || !current->pending_response) {
        return false;
    }
    
    current->pending_response = false;
    registry.bus_busy = false;
    
    bool success = false;
    if (current->on_response) {
        success = current->on_response(current->user_data, data, len);
    }
    
    if (success) {
        current->poll_count++;
        current->consecutive_errors = 0;
        registry.last_successful_response_ms = millis();  // Reset watchdog
        registry.watchdog_alert_active = false;           // Clear alert on success
    } else {
        current->error_count++;
        current->consecutive_errors++;
        registry.total_errors++;
    }
    
    return success;
}

static uint8_t bus_rx_buffer[256];
static uint16_t bus_rx_idx = 0;
static uint32_t last_byte_time_ms = 0;

void rs485HandleBus(void) {
    if (!registry.bus_mutex) return;
    
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)registry.bus_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // Bus busy or locked by another task
    }

    // 1. Run the scheduler/poller
    rs485Update();
    
    // 2. Process incoming data if we are waiting for a response
    if (registry.bus_busy) {
        uint32_t now = millis();
        
        // Read available bytes from UART
        uint8_t b;
        while (bus_serial->available()) {
            b = bus_serial->read();
            if (bus_rx_idx < sizeof(bus_rx_buffer)) {
                bus_rx_buffer[bus_rx_idx++] = b;
                last_byte_time_ms = now;
                // logDebug("R: %02X '%c'", b, (b >= 32) ? b : '.');
            }
        }
        
        // Check for frame completion (5ms silence or buffer full)
        if (bus_rx_idx > 0) {
            bool frame_complete = false;
            
            // Heuristic 1: Silence timeout (50ms for slow ASCII devices)
            if (now - last_byte_time_ms > 50) {
                frame_complete = true;
            }
            // Heuristic 2: Buffer full
            else if (bus_rx_idx >= sizeof(bus_rx_buffer)) {
                frame_complete = true;
            }
            
            if (frame_complete) {
                logDebug("[RS485] RX Frame (%d bytes)", bus_rx_idx);
                rs485ProcessResponse(bus_rx_buffer, bus_rx_idx);
                bus_rx_idx = 0;
            }
        }
    } else {
        // Bus is idle, clear any stale data
        if (bus_serial->available() > 0) {
            while (bus_serial->available()) bus_serial->read();
        }
        bus_rx_idx = 0;
    }
    
    xSemaphoreGiveRecursive((SemaphoreHandle_t)registry.bus_mutex);
}

bool rs485IsBusAvailable(void) {
    return !registry.bus_busy;
}

bool rs485RequestImmediatePoll(rs485_device_t* device) {
    if (!device || !device->enabled || registry.bus_busy) {
        return false;
    }
    
    // Force poll now
    device->last_poll_time_ms = 0;
    return true;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

bool rs485SetBaudRate(uint32_t baud_rate) {
    if (baud_rate == 0) return false;
    
    registry.baud_rate = baud_rate;
    bus_serial->updateBaudRate(baud_rate);
    
    logInfo("[RS485] Baud rate updated to %lu", (unsigned long)baud_rate);
    return true;
}

uint32_t rs485GetBaudRate(void) {
    return registry.baud_rate;
}

// ============================================================================
// BUS I/O API implementation
// ============================================================================

bool rs485Send(const uint8_t* data, uint8_t len) {
    if (!bus_serial || !data || !registry.bus_mutex) return false;
    
    bool ok = false;
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)registry.bus_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ok = (bus_serial->write(data, len) == len);
        xSemaphoreGiveRecursive((SemaphoreHandle_t)registry.bus_mutex);
    }
    return ok;
}

int rs485Available(void) {
    return bus_serial ? bus_serial->available() : 0;
}

bool rs485Receive(uint8_t* data, uint8_t* len) {
    if (!bus_serial || !data || !len || !registry.bus_mutex) return false;
    
    bool ok = false;
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)registry.bus_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int avail = bus_serial->available();
        if (avail > 0) {
            int to_read = (avail < (int)*len) ? avail : *len;
            int bytes_read = bus_serial->readBytes(data, to_read);
            *len = (uint8_t)bytes_read;
            ok = (bytes_read > 0);
        } else {
            *len = 0;
            ok = false;
        }
        xSemaphoreGiveRecursive((SemaphoreHandle_t)registry.bus_mutex);
    }
    return ok;
}

void rs485ClearBuffer(void) {
    if (bus_serial) {
        while (bus_serial->available()) bus_serial->read();
    }
}

void rs485SetDeviceEnabled(rs485_device_t* device, bool enabled) {
    if (device) {
        device->enabled = enabled;
        logInfo("[RS485] %s: %s", device->name, enabled ? "enabled" : "disabled");
    }
}

bool rs485TakeBus(uint32_t timeout_ms) {
    if (!registry.bus_mutex) return false;
    return xSemaphoreTakeRecursive((SemaphoreHandle_t)registry.bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void rs485ReleaseBus(void) {
    if (registry.bus_mutex) xSemaphoreGiveRecursive((SemaphoreHandle_t)registry.bus_mutex);
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

const rs485_registry_state_t* rs485GetState(void) {
    return &registry;
}

rs485_device_t** rs485GetDevices(uint8_t* count) {
    if (count) *count = registry.device_count;
    return registry.devices;
}

void rs485ResetErrorCounters(void) {
    for (uint8_t i = 0; i < registry.device_count; i++) {
        rs485_device_t* dev = registry.devices[i];
        dev->poll_count = 0;
        dev->error_count = 0;
        dev->consecutive_errors = 0;
    }
    registry.total_transactions = 0;
    registry.total_errors = 0;
    logInfo("[RS485] Error counters reset");
}

void rs485PrintDiagnostics(void) {
    serialLoggerLock();
    logPrintln("\n[RS485] === Device Registry ===");
    logPrintf("Baud Rate: %lu bps\n", (unsigned long)registry.baud_rate);
    logPrintf("Devices: %d/%d\n", registry.device_count, RS485_MAX_DEVICES);
    logPrintf("Total TX: %lu | Errors: %lu\n\n",
                  (unsigned long)registry.total_transactions,
                  (unsigned long)registry.total_errors);
    
    logPrintln("Device          | Addr | Prio | Interval | Polls   | Errors  | Status");
    logPrintln("----------------|------|------|----------|---------|---------|--------");
    
    for (uint8_t i = 0; i < registry.device_count; i++) {
        rs485_device_t* dev = registry.devices[i];
        logPrintf("%-15s | %4d | %4d | %6dms | %7lu | %7lu | %s\n",
                      dev->name,
                      dev->slave_address,
                      dev->priority,
                      dev->poll_interval_ms,
                      (unsigned long)dev->poll_count,
                      (unsigned long)dev->error_count,
                      dev->enabled ? "ON" : "OFF");
    }
    logPrintln("");
    serialLoggerUnlock();
}

bool rs485CheckWatchdog(void) {
    // Skip watchdog if no devices registered or no devices enabled
    if (registry.device_count == 0) {
        return false;
    }
    
    // Check if any devices are actually enabled
    bool any_enabled = false;
    for (uint8_t i = 0; i < registry.device_count; i++) {
        if (registry.devices[i] && registry.devices[i]->enabled) {
            any_enabled = true;
            break;
        }
    }
    
    if (!any_enabled) {
        return false;  // No enabled devices, nothing to watch
    }
    
    uint32_t now = millis();
    uint32_t elapsed = now - registry.last_successful_response_ms;
    
    if (elapsed > RS485_WATCHDOG_TIMEOUT_MS) {
        if (!registry.watchdog_alert_active) {
            // First time detecting issue - raise alert
            registry.watchdog_alert_active = true;
            logWarning("[RS485] Watchdog: No response from any device for %lu ms!", 
                       (unsigned long)elapsed);
        }
        return true;  // Alert condition active
    }
    
    return false;  // All good
}

void rs485ClearWatchdogAlert(void) {
    registry.watchdog_alert_active = false;
    registry.last_successful_response_ms = millis();
    logInfo("[RS485] Watchdog alert cleared");
}

void rs485SetBusPaused(bool paused) {
    registry.bus_paused = paused;
    if (paused) {
        logInfo("[RS485] Bus ACTIVITY SUSPENDED (Maintenance Mode)");
        // Clear any pending state
        registry.bus_busy = false;
        for (uint8_t i = 0; i < registry.device_count; i++) {
            registry.devices[i]->pending_response = false;
        }
    } else {
        logInfo("[RS485] Bus activity RESUMED");
        registry.last_switch_time_ms = millis();
    }
}

bool rs485IsBusPaused(void) {
    return registry.bus_paused;
}

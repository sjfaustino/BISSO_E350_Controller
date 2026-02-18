/**
 * @file rs485_device_registry.cpp
 * @brief RS-485 Device Registration and Scheduling Implementation
 * @project BISSO E350 Controller
 * @details Priority-based scheduler for multiple Modbus RTU devices.
 */

#include "rs485_device_registry.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "fault_logging.h"
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
    .bus_mutex = NULL,
    .sniffer_cb = NULL
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
    bus_serial->begin(baud_rate, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    
    logInfo("[RS485] Registry initialized on Serial2 (baud: %lu, RX:%d TX:%d)",
            (unsigned long)baud_rate, PIN_RS485_RX, PIN_RS485_TX);
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

    // Initialize latency stats
    device->last_tx_end_us = 0;
    device->first_rx_byte_us = 0;
    memset(&device->latency_hist, 0, sizeof(device->latency_hist));
    device->min_latency_us = UINT32_MAX;
    device->max_latency_us = 0;
    device->total_latency_us = 0;
    device->total_latency_sq_us = 0;
    device->latency_samples = 0;
    
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
        
        // PHASE 16: Aggressive Error Backoff Mechanism
        // If device is erroring, exponentially back off polling to prevent bus blocking.
        // Cap backoff at 2^10 (~1024x) to ensure we don't block high-priority bus traffic.
        // For a 50ms interval, 1024x = 51.2 seconds.
        uint32_t effective_interval = dev->poll_interval_ms;
        if (dev->consecutive_errors > 0) {
            uint8_t shift = (dev->consecutive_errors > 10) ? 10 : dev->consecutive_errors;
            effective_interval <<= shift; 
        }

        uint32_t elapsed = now - dev->last_poll_time_ms;
        if (elapsed < effective_interval) continue;
        
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
            if (now - current->last_poll_time_ms > 250) {
                // Timeout
                current->pending_response = false;
                current->error_count++;
                current->consecutive_errors++;
                registry.bus_busy = false;
                registry.last_switch_time_ms = now; // Enforce gap after timeout
                registry.total_errors++;
                
                // Panic Recovery Trigger: If all devices failing for > 15s
                if (now - registry.last_successful_response_ms > 15000) {
                    logError("[RS485] TOTAL BUS FAILURE > 15s. Triggering panic recovery...");
                    rs485PerformPanicRecovery();
                }

                // Critical Path: WJ66 Encoder Watchdog
                if (current->type == RS485_DEVICE_TYPE_ENCODER && motionIsMoving()) {
                    faultLogCritical(FAULT_ENCODER_TIMEOUT, "Encoder timeout during motion");
                    emergencyStopSetActive(true);
                }

                // PHASE 16: Reduced spam for bare board/bench debugging.
                if (current->consecutive_errors == 10) {
                    // Log to persistent fault history on persistent failure
                    faultLogEntry(FAULT_WARNING, FAULT_RS485_TIMEOUT, 0, current->slave_address, 
                                 "Comm loss with device %s", current->name);
                }

                if (current->consecutive_errors <= 3) {
                    logWarning("[RS485] Timeout: %s (Addr %d, after 250ms)", 
                               current->name, current->slave_address);
                    if (current->consecutive_errors == 3) {
                        logInfo("[RS485] %s timeout logging silenced until success.", current->name);
                    }
                }
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
        next->first_rx_byte_us = 0; // Reset for new txn
        registry.bus_busy = true;
        registry.last_switch_time_ms = now;
        registry.total_transactions++;
        logDebug("[RS485] Poll -> %s (Addr %d)", next->name, next->slave_address);
        return true;
    }
    
    return false;
}

static uint8_t bus_rx_buffer[256];
static uint16_t bus_rx_idx = 0;
static uint32_t last_byte_time_ms = 0;

// Sniffer Ring Buffer (PHASE 2.0)
static rs485_sniff_entry_t sniff_buffer[RS485_SNIFF_BUFFER_SIZE];
static uint32_t sniff_head = 0;
static uint32_t total_sniff_entries = 0;

static void pushSniff(uint8_t addr, uint8_t func, uint16_t len, bool tx, const uint8_t* data) {
    rs485_sniff_entry_t* entry = &sniff_buffer[sniff_head % RS485_SNIFF_BUFFER_SIZE];
    entry->timestamp = millis();
    entry->address = addr;
    entry->function = func;
    entry->length = len;
    entry->is_tx = tx;
    memset(entry->data, 0, 8);
    memcpy(entry->data, data, len > 8 ? 8 : len);
    sniff_head++;
    total_sniff_entries++;
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
    
    // Sniffer hook (RX)
    pushSniff(current->slave_address, data[1], len, false, data);
    
    if (success) {
        current->poll_count++;
        current->consecutive_errors = 0;
        registry.last_successful_response_ms = millis();  // Reset watchdog
        registry.watchdog_alert_active = false;           // Clear alert on success

        // Update Latency Histogram
        if (current->first_rx_byte_us > 0 && current->last_tx_end_us > 0) {
            uint32_t latency_us = current->first_rx_byte_us - current->last_tx_end_us;
            uint32_t latency_ms = latency_us / 1000;

            if (latency_ms < 10)         current->latency_hist.bucket_lt10ms++;
            else if (latency_ms < 25)    current->latency_hist.bucket_10to25ms++;
            else if (latency_ms < 50)    current->latency_hist.bucket_25to50ms++;
            else if (latency_ms < 100)   current->latency_hist.bucket_50to100ms++;
            else if (latency_ms < 250)   current->latency_hist.bucket_100to250ms++;
            else                         current->latency_hist.bucket_gt250ms++;

            // Update Jitter Analysis
            if (latency_us < current->min_latency_us) current->min_latency_us = latency_us;
            if (latency_us > current->max_latency_us) current->max_latency_us = latency_us;
            current->total_latency_us += latency_us;
            current->total_latency_sq_us += (uint64_t)latency_us * latency_us;
            current->latency_samples++;
        }
    } else {
        current->error_count++;
        current->consecutive_errors++;
        registry.total_errors++;
    }
    
    return success;
}


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
                if (bus_rx_idx == 0) {
                    rs485_device_t* current = registry.devices[registry.current_device_index];
                    if (current) current->first_rx_byte_us = micros();
                }
                bus_rx_buffer[bus_rx_idx++] = b;
                last_byte_time_ms = now;
                // logDebug("R: %02X '%c'", b, (b >= 32) ? b : '.');
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
                
                // Trigger sniffer if active
                if (registry.sniffer_cb) {
                    registry.sniffer_cb(false, bus_rx_buffer, bus_rx_idx);
                }
                
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
        // Trigger sniffer if active
        if (registry.sniffer_cb) {
            registry.sniffer_cb(true, data, len);
        }
        
        // Sniffer hook (TX)
        if (len >= 2) { // Assuming Modbus RTU: byte 0 is address, byte 1 is function code
            pushSniff(data[0], data[1], len, true, data);
        } else {
            pushSniff(0, 0, len, true, data); // Fallback for non-standard frames
        }
        
        ok = (bus_serial->write(data, len) == len);
        
        // Record end of TX for latency tracking
        rs485_device_t* current = registry.devices[registry.current_device_index];
        if (current) current->last_tx_end_us = micros();

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
        memset(&dev->latency_hist, 0, sizeof(dev->latency_hist));
        dev->min_latency_us = UINT32_MAX;
        dev->max_latency_us = 0;
        dev->total_latency_us = 0;
        dev->total_latency_sq_us = 0;
        dev->latency_samples = 0;
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
    
    logPrintln("Device          | Addr | Prio | Base Int | Errors | Consec | Effective");
    logPrintln("----------------|------|------|----------|--------|--------|-----------");
    
    for (uint8_t i = 0; i < registry.device_count; i++) {
        rs485_device_t* dev = registry.devices[i];
        
        // Calculate effective interval for display
        uint32_t effective = dev->poll_interval_ms;
        if (dev->consecutive_errors > 0) {
            uint8_t shift = (dev->consecutive_errors > 10) ? 10 : dev->consecutive_errors;
            effective <<= shift;
        }

        logPrintf("%-15s | %4d | %4d | %6dms | %6lu | %6d | %dms\n",
                      dev->name,
                      dev->slave_address,
                      dev->priority,
                      dev->poll_interval_ms,
                      (unsigned long)dev->error_count,
                      dev->consecutive_errors,
                      effective);
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

void rs485PerformPanicRecovery(void) {
    if (!bus_serial) return;
    
    logWarning("[RS485] Performing panic recovery: Re-initializing UART...");
    
    // Lock bus to prevent concurrent access during reset
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)registry.bus_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        bus_serial->end();
        vTaskDelay(50 / portTICK_PERIOD_MS);
        bus_serial->begin(registry.baud_rate, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
        
        registry.bus_busy = false;
        registry.last_successful_response_ms = millis(); // Reset timer to allow recovery
        
        xSemaphoreGiveRecursive((SemaphoreHandle_t)registry.bus_mutex);
        logInfo("[RS485] Recovery complete.");
    } else {
        logError("[RS485] Recovery failed: Bus mutex timeout");
    }
}

void rs485SetSniffer(void (*cb)(bool is_tx, const uint8_t* data, uint16_t len)) {
    registry.sniffer_cb = cb;
}
uint32_t rs485GetSniffData(rs485_sniff_entry_t* dest, uint32_t max_entries) {
    uint32_t count = (total_sniff_entries > RS485_SNIFF_BUFFER_SIZE) ? RS485_SNIFF_BUFFER_SIZE : total_sniff_entries;
    if (count > max_entries) count = max_entries;
    
    for (uint32_t i = 0; i < count; i++) {
        // Return in reverse chronological order (newest first)
        uint32_t idx = (sniff_head - 1 - i) % RS485_SNIFF_BUFFER_SIZE;
        dest[i] = sniff_buffer[idx];
    }
    return count;
}

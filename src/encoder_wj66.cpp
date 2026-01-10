/**
 * @file encoder_wj66.cpp
 * @brief Driver for WJ66 Absolute Encoders (PosiPro)
 * @details Merged: User's Baud/Parsing Logic + v3.5 Safety Flow Control.
 */

#include <Arduino.h>
#include "rs485_device_registry.h"
#include "encoder_hal.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "encoder_comm_stats.h" 
#include "encoder_hal.h"
#include "system_constants.h"
#include <Preferences.h>
#include "config_unified.h"
#include "config_keys.h"

// Safety Constants
// BUFFER SIZE VERIFICATION (Code Audit):
// Max frame: !<11 digits>,<11 digits>,<11 digits>,<11 digits>\r = 49 bytes
// Example:   !-2147483648,-2147483648,-2147483648,-2147483648\r
// Buffer:    64 bytes (15 byte safety margin)
// Conclusion: Adequate for full encoder frame ✓
#define MAX_BYTES_PER_CYCLE 64
#define WJ66_TIMEOUT_MS 500

// Internal State
struct {
  int32_t position[WJ66_AXES];
  int32_t zero_offset[WJ66_AXES];
  uint32_t last_read[WJ66_AXES];
  uint32_t read_count[WJ66_AXES];
  encoder_status_t status;
  uint32_t error_count;
  uint32_t last_command_time;
  bool waiting_for_response; // Added for flow control
} wj66_state = {{0}, {0}, {0}, {0}, ENCODER_OK, 0, 0, false};

// RS-485 Registry Device Descriptor
static bool wj66Poll(void* ctx);
static bool wj66OnResponse(void* ctx, const uint8_t* data, uint16_t len);

static rs485_device_t wj66_device = {
    .name = "WJ66",
    .type = RS485_DEVICE_TYPE_ENCODER,
    .slave_address = 0,         // WJ66 uses broadcast or specific logic, but address 0 is often used for simple setups
    .poll_interval_ms = 50,     // 20 Hz
    .priority = 200,            // High priority
    .enabled = true,
    .poll = wj66Poll,
    .on_response = wj66OnResponse,
    .last_poll_time_ms = 0,
    .poll_count = 0,
    .error_count = 0,
    .consecutive_errors = 0,
    .pending_response = false
};

// PHASE 5.10: Mutex for thread-safe encoder position access
// Protects wj66_state from torn reads between Encoder task and Motion task
static SemaphoreHandle_t wj66_mutex = NULL;





void wj66Init() {
  logInfo("[WJ66] Initializing...");

  // PHASE 5.10: Create mutex for thread-safe position access
  if (wj66_mutex == NULL) {
    wj66_mutex = xSemaphoreCreateMutex();
    if (wj66_mutex == NULL) {
      logError("[WJ66] Failed to create position mutex!");
    }
  }

  // 1. Load configuration
  uint32_t config_baud = configGetInt(KEY_ENC_BAUD, WJ66_BAUD);
  uint8_t config_iface = (uint8_t)configGetInt(KEY_ENC_INTERFACE, ENCODER_INTERFACE_RS232_HT);
  
  // Sanity check
  if (config_baud < 1200 || config_baud > 115200) config_baud = 9600;

  logInfo("[WJ66] Configuring @ %lu baud, Interface: %d", (unsigned long)config_baud, config_iface);
  
  // Initialize via HAL to ensure internal state (serial_port pointer) is set
  // This prevents crash when encoderHalSendString is called
  if (!encoderHalInit((encoder_interface_t)config_iface, config_baud)) {
      logError("[WJ66] HAL Init Failed!");
      // Fallback
      if (config_iface == ENCODER_INTERFACE_RS232_HT) {
          Serial1.begin(config_baud, SERIAL_8N1, 14, 33);
      } else {
          Serial2.begin(config_baud, SERIAL_8N1, 16, 13);
      }
  }

  // Init State
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    wj66_state.last_read[i] = (millis() > 60000) ? (millis() - 60000) : 0;
  }
  wj66_state.status = ENCODER_OK;
  wj66_state.waiting_for_response = false;
  
  // Register with RS-485 registry
  rs485RegisterDevice(&wj66_device);
  
  logInfo("[WJ66] Ready (Registered with RS485 bus)");
}

// Flag to prevent race conditions check (Legacy)
static volatile bool wj66_maintenance_mode = false;

bool wj66SetBaud(uint32_t baud) {
    if (baud < 1200 || baud > 115200) return false;
    
    logInfo("[WJ66] Setting baud rate to %lu...", (unsigned long)baud);
    
    // Critical Section: Shutdown HAL to prevent EncoderTask from accessing Serial1
    encoderHalEnd();
    delay(50); // Ensure any pending ISRs/Tasks clear

    // Re-initialize with new baud
    // This atomically sets up Serial1 AND enables HAL flags
    bool ok = encoderHalInit(ENCODER_INTERFACE_RS232_HT, baud);
    
    if (ok) {
        // Save to NVS only if init successful
        configSetInt(KEY_ENC_BAUD, baud);
        configUnifiedSave();
    } else {
        logError("[WJ66] Failed to set baud via HAL!");
    }
    
    return ok;
}

uint32_t wj66Autodetect() {
    logInfo("[WJ66] Starting Auto-detect...");
    
    // Lock out polling using maintenance mode flag
    wj66_maintenance_mode = true;
    vTaskDelay(100 / portTICK_PERIOD_MS); // Let any current poll finish
    
    const uint32_t rates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200};
    uint32_t found_rate = 0;
    uint8_t config_iface = (uint8_t)configGetInt(KEY_ENC_INTERFACE, 0);
    HardwareSerial* s = (config_iface == 1) ? &Serial2 : &Serial1;

    for (uint32_t rate : rates) {
        vTaskDelay(20 / portTICK_PERIOD_MS); // Feed WDT
        
        logInfo("[WJ66] Trying %lu...", (unsigned long)rate);
        s->updateBaudRate(rate);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Flush RX buffer (limited to prevent infinite loop)
        for (int i = 0; i < 200 && s->available(); i++) s->read();
        
        // Send query
        s->write((uint8_t)0x01);
        s->write((uint8_t)0x00);
        s->flush();
        
        // Wait for response
        uint32_t start = millis();
        while (millis() - start < 150) {
            if (s->available()) {
                logInfo("[WJ66] Found @ %lu baud!", (unsigned long)rate);
                found_rate = rate;
                break;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        if (found_rate) break;
    }
    
    // Restore to found or saved rate
    uint32_t final_rate = found_rate ? found_rate : configGetInt(KEY_ENC_BAUD, WJ66_BAUD);
    s->updateBaudRate(final_rate);
    
    if (found_rate) {
        configSetInt(KEY_ENC_BAUD, found_rate);
        configUnifiedSave();
    } else {
        logError("[WJ66] Autodetect failed");
    }
    
    wj66_maintenance_mode = false;
    return found_rate;
}

static bool wj66Poll(void* ctx) { (void)ctx;
    if (wj66_maintenance_mode) return false;
    
    // Check if we are on RS485 (Interface 1)
    if (configGetInt(KEY_ENC_INTERFACE, 0) == 1) {
        return rs485Send((const uint8_t*)"#00\r", 4);
    }
    
    // Otherwise use Standard UART (Serial1)
    return encoderHalSendString("#00\r");
}

static bool wj66OnResponse(void* ctx, const uint8_t* data, uint16_t len) { (void)ctx;
    if (len == 0 || data[0] != '!') {
        return false;
    }

    int commas = 0;
    int32_t values[4] = {0};
    int32_t current_value = 0;
    bool is_negative = false;
    
    // CSV Parsing Logic
    for (uint16_t i = 1; i < len; i++) {
        char ch = (char)data[i];
        if (ch == ',') {
            values[commas] = is_negative ? -current_value : current_value;
            current_value = 0; is_negative = false;
            commas++;
            if (commas >= 4) break;
        } else if (ch == '-') is_negative = true;
        else if (ch >= '0' && ch <= '9') current_value = current_value * 10 + (ch - '0');
        else if (ch == '\r' || ch == '\n') break; // End of frame
    }
    
    // Final Value & Validation
    if (commas == 3) {
        values[3] = is_negative ? -current_value : current_value;

        // PHASE 5.10: Thread-safe position and status write
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (int i = 0; i < WJ66_AXES; i++) {
                wj66_state.position[i] = values[i];
                wj66_state.last_read[i] = millis();
                wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
            xSemaphoreGive(wj66_mutex);
        }
        return true;
    } else {
        if (xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            wj66_state.status = ENCODER_CRC_ERROR;
            wj66_state.error_count++;
            xSemaphoreGive(wj66_mutex);
        }
        return false;
    }
}


int32_t wj66GetPosition(uint8_t axis) {
    if (axis >= WJ66_AXES) return 0;

    // PHASE 5.10: Thread-safe position read to prevent torn reads
    int32_t position = 0;
    if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      position = wj66_state.position[axis] - wj66_state.zero_offset[axis];
      xSemaphoreGive(wj66_mutex);
    } else {
      logWarning("[WJ66] Mutex timeout reading axis %d position", axis);
      // Return cached value without protection (better than blocking)
      position = wj66_state.position[axis] - wj66_state.zero_offset[axis];
    }

    return position;
}

uint32_t wj66GetAxisAge(uint8_t axis) { 
    return (axis < WJ66_AXES) ? millis() - wj66_state.last_read[axis] : 0xFFFFFFFF; 
}

bool wj66IsStale(uint8_t axis) { 
    return wj66GetAxisAge(axis) > WJ66_TIMEOUT_MS; 
}

// PHASE 5.10: Protect status read with mutex
encoder_status_t wj66GetStatus() {
  encoder_status_t status = ENCODER_OK;
  if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    status = wj66_state.status;
    xSemaphoreGive(wj66_mutex);
  }
  return status;
}

void wj66Reset() {
  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    // Set last_read to past timestamp to ensure IsStale() returns true (disconnected) until first response
    wj66_state.last_read[i] = (millis() > 60000) ? (millis() - 60000) : 0;
  }
  wj66_state.error_count = 0;
  wj66_state.waiting_for_response = false;
  logInfo("[WJ66] Stats reset.");
}

void wj66SetZero(uint8_t axis) {
    if (axis < WJ66_AXES) {
        // PHASE 5.10: Thread-safe zero offset update
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          wj66_state.zero_offset[axis] = wj66_state.position[axis];
          xSemaphoreGive(wj66_mutex);
          logInfo("[WJ66] Axis %d Zeroed. Offset: %ld", axis, (long)wj66_state.zero_offset[axis]);
        } else {
          logWarning("[WJ66] Mutex timeout setting zero for axis %d", axis);
        }
    }
}

void wj66Diagnostics() {
  logPrintln("\n=== ENCODER STATUS ===");
  logPrintf("Status: %d\nErrors: %lu\n", wj66_state.status, (unsigned long)wj66_state.error_count);
  logPrintf("Waiting: %s\n", wj66_state.waiting_for_response ? "YES" : "NO");
  
  for (int i = 0; i < WJ66_AXES; i++) {
    logPrintf("  Axis %d: Raw=%ld | Offset=%ld | NET=%ld\n", 
        i, 
        (long)wj66_state.position[i], 
        (long)wj66_state.zero_offset[i],
        (long)(wj66_state.position[i] - wj66_state.zero_offset[i])
    );
  }
}

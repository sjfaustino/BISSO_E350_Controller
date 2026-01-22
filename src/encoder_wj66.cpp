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
#include "modbus_rtu.h"

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
    .slave_address = 0,
    .poll_interval_ms = 50,
    .priority = 200,
    .enabled = true,
    .poll = wj66Poll,
    .on_response = wj66OnResponse,
    .user_data = nullptr,
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
  uint8_t config_iface = (uint8_t)configGetInt(KEY_ENC_INTERFACE, ENCODER_INTERFACE_RS485_RXD2);
  
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
#if !defined(CONFIG_IDF_TARGET_ESP32S2)
          Serial2.begin(config_baud, SERIAL_8N1, 16, 13);
#else
          // ESP32-S2 fallback: use Serial1 for second interface
          Serial1.begin(config_baud, SERIAL_8N1, 16, 13);
#endif
      }
  }

  // Init State
  uint8_t addr = (uint8_t)configGetInt(KEY_ENC_ADDR, 0);
  wj66_device.slave_address = addr;

  for (int i = 0; i < WJ66_AXES; i++) {
    wj66_state.position[i] = 0;
    wj66_state.zero_offset[i] = 0;
    wj66_state.last_read[i] = 0;
    wj66_state.read_count[i] = 0;
  }
  wj66_state.status = ENCODER_OK;
  wj66_state.waiting_for_response = false;
  
  // Register with RS-485 registry ONLY if in RS485 mode
  // Interface 1 = RS485 (GPIO 16/13)
  if (config_iface == 1) {
    rs485RegisterDevice(&wj66_device);
    logInfo("[WJ66] Ready (Registered with RS485 bus)");
  } else {
    logInfo("[WJ66] Ready (Using Serial interface on HT pins)");
  }
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
    
    // Suspend RS485 bus activity during scan
    rs485SetBusPaused(true);
    
    // Acquire bus mutex (critical for preventing background task interference)
    if (!rs485TakeBus(500)) {
        logError("[WJ66] Failed to acquire bus mutex for scan");
        rs485SetBusPaused(false);
        return 0;
    }

    vTaskDelay(200 / portTICK_PERIOD_MS); // Let any current poll finish and bus settle
    
    const uint32_t rates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200};
    uint32_t found_rate = 0;
    
    // Select serial channel based on hardware config
#if !defined(CONFIG_IDF_TARGET_ESP32S2)
    uint8_t config_iface = (uint8_t)configGetInt(KEY_ENC_INTERFACE, ENCODER_INTERFACE_RS485_RXD2);
    HardwareSerial* s = (config_iface == 1) ? &Serial2 : &Serial1;
#else
    HardwareSerial* s = &Serial1;
#endif

    int found_addr = -1;

    for (uint32_t rate : rates) {
        vTaskDelay(20 / portTICK_PERIOD_MS); // Feed WDT
        
        logInfo("[WJ66] Trying %lu...", (unsigned long)rate);
        s->updateBaudRate(rate);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Flush any inbound noise
        while(s->available()) s->read();
        
        // Probing addresses
        int proto = configGetInt(KEY_ENC_PROTO, 0);
        int max_addr = (proto == 1) ? 2 : 1; // Try 1-2 for Modbus, 0-1 for ASCII
        
        for (int addr_to_try = 0; addr_to_try <= max_addr; addr_to_try++) {
            if (proto == 1 && addr_to_try == 0) continue; 
            
            bool got_response = false;
            // For Modbus, we try a few variations if the first one fails
            int variations = (proto == 1) ? 2 : 1; 
            for (int var = 0; var < variations; var++) {
                uint16_t reg_addr = (var == 0) ? 0x0010 : 0x0000;
                
                if (proto == 1) {
                    uint8_t frame[8];
                    modbusReadRegistersRequest((uint8_t)addr_to_try, reg_addr, 8, frame);
                    // Also try FC 04 if FC 03 fails? Let's stick to 03 for now but log.
                    logPrintf("[WJ66] Probing Modbus Addr %d, Reg 0x%04X...\r\n", addr_to_try, reg_addr);
                    s->write(frame, 8);
                } else {
                    char cmd[8];
                    snprintf(cmd, sizeof(cmd), "#%02d2\r", addr_to_try); // Use #AA2\r for all axes
                    logPrintf("[WJ66] Probing ASCII Addr %d...\r\n", addr_to_try);
                    s->print(cmd);
                }
                s->flush();
                
                // Wait for response and verify
                s->setTimeout(100); 
                uint32_t start = millis();
                while (millis() - start < 250) {
                    if (s->available()) {
                        uint8_t buf[64];
                        int len = s->readBytes(buf, sizeof(buf)-1);
                        if (len > 0) {
                            buf[len] = '\0';
                            
                            logPrintf("[WJ66] Raw Response (%d bytes): ", len);
                            for (int i = 0; i < len; i++) {
                                if (buf[i] >= 32 && buf[i] <= 126) logPrintf("%c", buf[i]);
                                else logPrintf("[%02X]", (uint8_t)buf[i]);
                            }
                            logPrintln("");

                            if (proto == 1) {
                                // Modbus Validation
                                if (len >= 5 && buf[0] == addr_to_try && buf[1] == 0x03 && modbusVerifyCrc(buf, len)) {
                                    logInfo("[WJ66] Found Modbus @ %lu baud, Addr %d!", (unsigned long)rate, addr_to_try);
                                    found_rate = rate;
                                    found_addr = addr_to_try;
                                    got_response = true;
                                    break;
                                } else if (buf[0] == '!' || buf[0] == '>') {
                                    logWarning("[WJ66] Detected ASCII response while in Modbus mode! Device may need protocol switch.");
                                }
                            } else {
                            // ASCII Validation (Heuristic)
                            char* buf_s = (char*)buf;
                            char* sig_ptr = strchr(buf_s, '!');
                            if (!sig_ptr) sig_ptr = strchr(buf_s, '>');
                            char* start_ptr = sig_ptr ? sig_ptr : buf_s;
                            
                            int commas = 0, digits = 0;
                            for (int i = 0; start_ptr[i] != '\0'; i++) {
                                if (start_ptr[i] == ',') commas++;
                                if (isdigit(start_ptr[i])) digits++;
                            }

                            if (commas >= 1 && digits >= 4) {
                                logInfo("[WJ66] Found ASCII @ %lu baud, Addr %d!", (unsigned long)rate, addr_to_try);
                                found_rate = rate;
                                found_addr = addr_to_try;
                                got_response = true;
                                break;
                            }
                        }
                    }
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
                if (got_response) break;
                while(s->available()) s->read(); // Flush garbage
            }
            if (got_response) break;
        }
        if (found_rate) break;
    }
    
    // Restore to found or saved rate
    uint32_t final_rate = found_rate ? found_rate : configGetInt(KEY_ENC_BAUD, WJ66_BAUD);
    s->updateBaudRate(final_rate);
    
    if (found_rate) {
        configSetInt(KEY_ENC_BAUD, (int)found_rate);
        configSetInt(KEY_ENC_ADDR, found_addr);
        configUnifiedSave();
    } else {
        logError("[WJ66] Autodetect failed");
    }
    
    // Release bus mutex
    rs485ReleaseBus();
    
    // Re-enable RS485 bus
    rs485SetBusPaused(false);
    return found_rate;
}

static bool wj66Poll(void* ctx) { (void)ctx;
    if (rs485IsBusPaused()) return false;
    
    int addr = configGetInt(KEY_ENC_ADDR, 0);
    int proto = configGetInt(KEY_ENC_PROTO, 0); // 0=ASCII, 1=Modbus

    if (proto == 1) {
        // Modbus RTU Mode: Read 8 registers (4 axes * 32-bit) starting at 0x0010
        uint8_t frame[8];
        modbusReadRegistersRequest((uint8_t)addr, 0x0010, 8, frame);
        return rs485Send(frame, 8);
    }
    
    // ASCII Mode
    char cmd[8];
    snprintf(cmd, sizeof(cmd), "#%02d2\r", addr); // Use #AA2\r per manual
    
    if (configGetInt(KEY_ENC_INTERFACE, 0) == 1) {
        logDebug("[WJ66] Sending Poll: %s (Addr %d)", cmd, addr);
        return rs485Send((const uint8_t*)cmd, (uint8_t)strlen(cmd));
    }
    
    return encoderHalSendString(cmd);
}

static bool wj66OnResponse(void* ctx, const uint8_t* data, uint16_t len) { (void)ctx;
    logDebug("[WJ66] Received %d bytes", len);
    if (len == 0) return false;

    // --- MODBUS RTU PARSING ---
    if (configGetInt(KEY_ENC_PROTO, 0) == 1) {
        // Minimum Modbus response is 5 bytes (Addr, FC, Len, CRC, CRC)
        if (len < 5) return false;
        
        // Verify address and FC (0x03)
        if (data[0] != configGetInt(KEY_ENC_ADDR, 0) || data[1] != 0x03) return false;
        
        // Verify CRC
        if (!modbusVerifyCrc(data, len)) {
            logWarning("[WJ66] Modbus CRC mismatch");
            return false;
        }

        uint8_t byte_count = data[2];
        if (len < byte_count + 5) return false;

        // Parse 32-bit values (Big Endian, 2 registers per axis)
        int32_t values[4] = {0};
        int axes_found = byte_count / 4;
        if (axes_found > 4) axes_found = 4;

        for (int i = 0; i < axes_found; i++) {
            int base = 3 + (i * 4);
            values[i] = ((uint32_t)data[base] << 24) | 
                        ((uint32_t)data[base+1] << 16) | 
                        ((uint32_t)data[base+2] << 8) | 
                        ((uint32_t)data[base+3]);
        }
        
        // Update state
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (int i = 0; i < axes_found; i++) {
                wj66_state.position[i] = values[i];
                wj66_state.last_read[i] = millis();
                wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
            xSemaphoreGive(wj66_mutex);
        }
        return true;
    }

    // --- ASCII/CSV PARSING WITH LOCAL BUFFERING ---
    static char asm_buf[128];
    static uint8_t asm_idx = 0;
    static uint32_t last_frag_time = 0;

    // Clear stale buffer (over 1s)
    if (asm_idx > 0 && millis() - last_frag_time > 1000) {
        asm_idx = 0;
    }
    last_frag_time = millis();

    // Look for signatures (!) within the incoming data
    int sig_idx = -1;
    for (int i = 0; i < len; i++) {
        if (data[i] == '!' || data[i] == '>') {
            sig_idx = i;
            break;
        }
    }

    // If we find a signature, we start a fresh frame
    if (sig_idx != -1) {
        asm_idx = 0;
        for (int i = sig_idx; i < len; i++) {
            if (asm_idx < sizeof(asm_buf) - 1) asm_buf[asm_idx++] = (char)data[i];
        }
    } else {
        // Otherwise, append to existing buffer if we've started a frame
        if (asm_idx > 0) {
            for (int i = 0; i < len; i++) {
                if (asm_idx < sizeof(asm_buf) - 1) asm_buf[asm_idx++] = (char)data[i];
            }
        }
    }
    asm_buf[asm_idx] = '\0';

    // Wait for terminator (\r or \n)
    if (strchr(asm_buf, '\r') == NULL && strchr(asm_buf, '\n') == NULL) {
        return true; 
    }

    // Found terminator - we have a full line!
    char full_line[128];
    strncpy(full_line, asm_buf, sizeof(full_line));
    full_line[sizeof(full_line)-1] = '\0';
    uint16_t full_len = asm_idx;
    asm_idx = 0; // Prepare for next frame

    // Start parsing from full_line
    int start_idx = -1;
    for (int i = 0; i < full_len; i++) {
        if (full_line[i] == '!' || full_line[i] == '>' || isdigit(full_line[i]) || full_line[i] == '-' || full_line[i] == '+') {
            start_idx = i;
            break;
        }
    }
    if (start_idx == -1) return false;

    int commas = 0;
    int32_t current_values[4] = {0};
    int32_t current_num = 0;
    bool is_negative = false;
    
    // CSV Parsing Logic
    for (uint16_t i = start_idx; i < full_len; i++) {
        char ch = full_line[i];
        if (ch == '!' || ch == '>') continue; // Skip signature if present
        if (ch == ',') {
            current_values[commas] = is_negative ? -current_num : current_num;
            current_num = 0; is_negative = false;
            commas++;
            if (commas >= 4) break;
        } else if (ch == '-') is_negative = true;
        else if (ch >= '0' && ch <= '9') current_num = current_num * 10 + (ch - '0');
        else if (ch == '\r' || ch == '\n') break; // End of frame
    }
    
    // Final Value & Validation (Permissive: capture last value and support variable axes)
    if (commas < 4) {
        current_values[commas] = is_negative ? -current_num : current_num;
    }
    
    if (commas >= 0) {
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            int axes_limit = (commas + 1 > WJ66_AXES) ? WJ66_AXES : commas + 1;
            for (int i = 0; i < axes_limit; i++) {
                wj66_state.position[i] = current_values[i];
                wj66_state.last_read[i] = millis();
                wj66_state.read_count[i]++;
            }
            wj66_state.status = ENCODER_OK;
            xSemaphoreGive(wj66_mutex);
            // Diagnostic trace enabled for debugging the background issue
            logDebug("[WJ66] Background Update: %ld", (long)current_values[0]);
        }
        return true;
    } else {
        if (wj66_mutex && xSemaphoreTake(wj66_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            wj66_state.status = ENCODER_CRC_ERROR;
            wj66_state.error_count++;
            xSemaphoreGive(wj66_mutex);
        }
        return false;
    }
}

void wj66ProcessSerial() {
    // CRITICAL: Only process if we are NOT on RS485 (Interface 1)
    // Background RS485 registry handles the polling if encoder_iface == 1
    const encoder_hal_config_t* hal_cfg = encoderHalGetConfig();
    if (hal_cfg && hal_cfg->interface == ENCODER_INTERFACE_RS485_RXD2) {
        return; 
    }
    
    if (wj66_maintenance_mode) return;

    static uint32_t last_poll = 0;
    uint32_t now = millis();

    // 1. Handlers for Incoming Data
    if (encoderHalAvailable() > 0) {
        uint8_t buffer[64];
        uint8_t len = sizeof(buffer);
        if (encoderHalReceive(buffer, &len)) {
            // Process the response
            wj66OnResponse(nullptr, buffer, len);
        }
    }

    // 2. Transmit Polling (every 50ms)
    if (now - last_poll >= WJ66_READ_INTERVAL_MS) {
        last_poll = now;
        
        int addr = configGetInt(KEY_ENC_ADDR, 0);
        int proto = configGetInt(KEY_ENC_PROTO, 0);

        if (proto == 1) {
            uint8_t frame[8];
            modbusReadRegistersRequest((uint8_t)addr, 0x0010, 8, frame);
            encoderHalSend(frame, 8);
        } else {
            char cmd[8];
            snprintf(cmd, sizeof(cmd), "#%02d2\r", addr);
            encoderHalSendString(cmd);
        }
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
    if (axis >= WJ66_AXES) return 0xFFFFFFFF;
    uint32_t last = wj66_state.last_read[axis];
    if (last == 0) return 999999;
    uint32_t now = millis();
    if (now < last) return 0;
    return now - last;
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

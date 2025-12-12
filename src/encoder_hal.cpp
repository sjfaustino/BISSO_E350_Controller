/**
 * @file encoder_hal.cpp
 * @brief Encoder Hardware Abstraction Layer Implementation
 * @details Manages multiple serial interfaces for WJ66 encoder compatibility
 */

#include "encoder_hal.h"
#include "serial_logger.h"
#include <Arduino.h>

// ============================================================================
// HAL STATE
// ============================================================================

static struct {
    encoder_hal_config_t config;        // Current configuration
    HardwareSerial* serial_port;        // Pointer to active serial interface
    bool initialized;                   // Initialization flag
    uint32_t last_error;                // Last error code
    uint32_t bytes_sent;                // Statistics
    uint32_t bytes_received;
} hal_state = {
    .config = {
        .interface = ENCODER_INTERFACE_RS232_HT,
        .baud_rate = 9600,
        .rx_pin = 14,
        .tx_pin = 33,
        .read_interval_ms = 50,
        .timeout_ms = 500
    },
    .serial_port = &Serial1,
    .initialized = false,
    .last_error = 0,
    .bytes_sent = 0,
    .bytes_received = 0
};

// ============================================================================
// INTERFACE DEFINITIONS
// ============================================================================

/**
 * @brief Interface configuration table
 */
static const struct {
    encoder_interface_t interface;
    const char* name;
    const char* description;
    uint8_t rx_pin;
    uint8_t tx_pin;
    uint8_t uart_num;  // 0=Serial, 1=Serial1, 2=Serial2
} INTERFACE_TABLE[] = {
    {
        ENCODER_INTERFACE_RS232_HT,
        "RS232-HT",
        "GPIO14/33 (HT1/HT2) - RS232 3.3V - Standard",
        14, 33, 1
    },
    {
        ENCODER_INTERFACE_RS485_RXD2,
        "RS485-RXD2",
        "GPIO17/18 (RXD2/TXD2) - RS485 Differential - Alternative",
        17, 18, 2
    },
    {
        ENCODER_INTERFACE_CUSTOM,
        "Custom",
        "User-defined pins and configuration",
        0, 0, 255
    }
};

#define NUM_INTERFACES (sizeof(INTERFACE_TABLE) / sizeof(INTERFACE_TABLE[0]))

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Find interface configuration by type
 */
static const struct {
    encoder_interface_t interface;
    const char* name;
    const char* description;
    uint8_t rx_pin;
    uint8_t tx_pin;
    uint8_t uart_num;
}* findInterfaceConfig(encoder_interface_t interface) {
    for (size_t i = 0; i < NUM_INTERFACES; i++) {
        if (INTERFACE_TABLE[i].interface == interface) {
            return &INTERFACE_TABLE[i];
        }
    }
    return NULL;
}

/**
 * @brief Get HardwareSerial pointer for UART number
 */
static HardwareSerial* getSerialByUart(uint8_t uart_num) {
    switch (uart_num) {
        case 0: return &Serial;
        case 1: return &Serial1;
        case 2: return &Serial2;
        default: return NULL;
    }
}

// ============================================================================
// ENCODER HAL PUBLIC API
// ============================================================================

bool encoderHalInit(encoder_interface_t interface, uint32_t baud_rate) {
    const auto* iface_config = findInterfaceConfig(interface);

    if (!iface_config) {
        logError("[ENCODER-HAL] Unknown interface type: %d", interface);
        hal_state.last_error = 1;
        return false;
    }

    // Build configuration
    encoder_hal_config_t config = {
        .interface = interface,
        .baud_rate = (baud_rate > 0) ? baud_rate : 9600,
        .rx_pin = iface_config->rx_pin,
        .tx_pin = iface_config->tx_pin,
        .read_interval_ms = 50,
        .timeout_ms = 500
    };

    return encoderHalInitCustom(&config);
}

bool encoderHalInitCustom(const encoder_hal_config_t* config) {
    if (!config) {
        logError("[ENCODER-HAL] NULL configuration pointer");
        hal_state.last_error = 2;
        return false;
    }

    // Find UART for this interface
    const auto* iface_config = findInterfaceConfig(config->interface);
    uint8_t uart_num = (config->interface == ENCODER_INTERFACE_CUSTOM) ? 1 : iface_config->uart_num;

    // Get serial port
    HardwareSerial* serial = getSerialByUart(uart_num);
    if (!serial) {
        logError("[ENCODER-HAL] Invalid UART number: %d", uart_num);
        hal_state.last_error = 3;
        return false;
    }

    // Shutdown previous interface if active
    if (hal_state.initialized) {
        hal_state.serial_port->end();
        logInfo("[ENCODER-HAL] Shutdown previous interface");
    }

    // Initialize serial interface
    serial->begin(config->baud_rate, SERIAL_8N1, config->rx_pin, config->tx_pin);
    delay(10);  // Allow serial to stabilize

    // Update HAL state
    hal_state.config = *config;
    hal_state.serial_port = serial;
    hal_state.initialized = true;
    hal_state.bytes_sent = 0;
    hal_state.bytes_received = 0;

    logInfo("[ENCODER-HAL] Initialized: %s @ %lu baud (RX:%d TX:%d)",
            encoderHalGetInterfaceName(config->interface),
            (unsigned long)config->baud_rate,
            config->rx_pin,
            config->tx_pin);

    return true;
}

const encoder_hal_config_t* encoderHalGetConfig(void) {
    return &hal_state.config;
}

bool encoderHalSend(const uint8_t* data, uint8_t len) {
    if (!hal_state.initialized || !hal_state.serial_port) {
        logWarning("[ENCODER-HAL] Send failed - not initialized");
        hal_state.last_error = 4;
        return false;
    }

    size_t bytes_written = hal_state.serial_port->write(data, len);
    hal_state.bytes_sent += bytes_written;

    return (bytes_written == len);
}

bool encoderHalSendString(const char* str) {
    if (!hal_state.initialized || !hal_state.serial_port || !str) {
        return false;
    }

    size_t len = strlen(str);
    return encoderHalSend((const uint8_t*)str, len);
}

bool encoderHalReceive(uint8_t* data, uint8_t* len) {
    if (!hal_state.initialized || !hal_state.serial_port || !data || !len) {
        return false;
    }

    int available = hal_state.serial_port->available();
    if (available == 0) {
        *len = 0;
        return false;
    }

    // Read up to requested length
    int to_read = (available < (int)*len) ? available : *len;
    int bytes_read = hal_state.serial_port->readBytes(data, to_read);

    *len = (uint8_t)bytes_read;
    hal_state.bytes_received += bytes_read;

    return (bytes_read > 0);
}

int encoderHalAvailable(void) {
    if (!hal_state.initialized || !hal_state.serial_port) {
        return 0;
    }
    return hal_state.serial_port->available();
}

void encoderHalClearBuffer(void) {
    if (hal_state.initialized && hal_state.serial_port) {
        while (hal_state.serial_port->available()) {
            hal_state.serial_port->read();
        }
    }
}

int encoderHalRxAvailable(void) {
    return encoderHalAvailable();
}

int encoderHalTxSpace(void) {
    // Most ESP32 UARTs have 128-byte TX buffer
    // This is approximate; actual implementation varies
    return 128;
}

bool encoderHalFlush(void) {
    if (!hal_state.initialized || !hal_state.serial_port) {
        return false;
    }
    hal_state.serial_port->flush();
    return true;
}

void encoderHalEnd(void) {
    if (hal_state.initialized && hal_state.serial_port) {
        hal_state.serial_port->end();
        hal_state.initialized = false;
        logInfo("[ENCODER-HAL] Shutdown complete");
    }
}

const char* encoderHalGetInterfaceName(encoder_interface_t interface) {
    const auto* config = findInterfaceConfig(interface);
    return config ? config->name : "UNKNOWN";
}

const char* encoderHalGetInterfaceDescription(encoder_interface_t interface) {
    const auto* config = findInterfaceConfig(interface);
    return config ? config->description : "Unknown interface";
}

bool encoderHalSwitchInterface(encoder_interface_t interface, uint32_t baud_rate) {
    logInfo("[ENCODER-HAL] Switching interface from %s to %s",
            encoderHalGetInterfaceName(hal_state.config.interface),
            encoderHalGetInterfaceName(interface));

    return encoderHalInit(interface, baud_rate);
}

uint32_t encoderHalGetLastError(void) {
    return hal_state.last_error;
}

void encoderHalPrintStatus(void) {
    Serial.println("\n========== ENCODER HAL STATUS ==========");
    Serial.print("Interface: ");
    Serial.println(encoderHalGetInterfaceName(hal_state.config.interface));
    Serial.print("Description: ");
    Serial.println(encoderHalGetInterfaceDescription(hal_state.config.interface));
    Serial.print("Baud Rate: ");
    Serial.println(hal_state.config.baud_rate);
    Serial.print("RX Pin: ");
    Serial.println(hal_state.config.rx_pin);
    Serial.print("TX Pin: ");
    Serial.println(hal_state.config.tx_pin);
    Serial.print("Status: ");
    Serial.println(hal_state.initialized ? "READY" : "NOT INITIALIZED");
    Serial.print("Bytes Sent: ");
    Serial.println(hal_state.bytes_sent);
    Serial.print("Bytes Received: ");
    Serial.println(hal_state.bytes_received);
    Serial.print("RX Available: ");
    Serial.println(encoderHalAvailable());
    Serial.println("=========================================\n");

    logInfo("[ENCODER-HAL] Status: %s @ %lu baud, %lu sent, %lu received",
            encoderHalGetInterfaceName(hal_state.config.interface),
            (unsigned long)hal_state.config.baud_rate,
            (unsigned long)hal_state.bytes_sent,
            (unsigned long)hal_state.bytes_received);
}

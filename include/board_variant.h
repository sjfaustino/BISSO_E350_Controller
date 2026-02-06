/**
 * @file board_variant.h
 * @brief Board variant abstraction for KC868-A16 v1.6 and v3.1
 * @project BISSO E350 Controller
 * 
 * Board selection via platformio.ini build_flags:
 *   -DBOARD_KC868_A16_V16  (default, ESP32-WROOM-32E)
 *   -DBOARD_KC868_A16_V31  (ESP32-S3-N16R8)
 */

#pragma once

#if defined(BOARD_KC868_A16_V31)
// =============================================================================
// KC868-A16 v3.1 (ESP32-S3-WROOM-1U N16R8)
// =============================================================================
#define BOARD_NAME           "KC868-A16 v3.1"
#define BOARD_MCU            "ESP32-S3-WROOM-1U"
#define BOARD_HAS_PSRAM      1
#define BOARD_HAS_W5500      1   // W5500 SPI Ethernet (not currently implemented)
#define BOARD_HAS_RTC_DS3231 1   // On-board RTC
#define BOARD_HAS_OLED_SSD1306 1 // On-board OLED (I2C 0x3C)
#define BOARD_HAS_SDCARD     1   // SD card slot

// I2C Bus
#define PIN_I2C_SDA          9
#define PIN_I2C_SCL          10

// RS485 (MAX13487)
#define PIN_RS485_RX         17
#define PIN_RS485_TX         16

// Analog Inputs (0-5V)
#define PIN_ANALOG_CH1       4
#define PIN_ANALOG_CH2       6
#define PIN_ANALOG_CH3       7
#define PIN_ANALOG_CH4       5

// 1-Wire GPIO (with pull-up resistors on PCB)
#define PIN_1WIRE_HT1        47
#define PIN_1WIRE_HT2        48
#define PIN_1WIRE_HT3        38

// W5500 Ethernet SPI (optional, WiFi preferred)
#define PIN_ETH_CLK          42
#define PIN_ETH_MOSI         43
#define PIN_ETH_MISO         44
#define PIN_ETH_CS           15
#define PIN_ETH_INT          2
#define PIN_ETH_RST          1

// SD Card SPI
#define PIN_SD_MOSI          12
#define PIN_SD_SCK           13
#define PIN_SD_MISO          14
#define PIN_SD_CS            11
#define PIN_SD_CD            21

// RF433MHz
#define PIN_RF433_RX         8
#define PIN_RF433_TX         18

// Free GPIOs (without pull-up resistance on PCB)
#define PIN_FREE_GPIO1       39
#define PIN_FREE_GPIO2       40
#define PIN_FREE_GPIO3       41

// I2C Device Addresses (v3.1 specific)
#define I2C_ADDR_RTC_DS3231  0x68
#define I2C_ADDR_OLED_SSD1306 0x3C
#define I2C_ADDR_EEPROM_24C02 0x50

#else
// =============================================================================
// KC868-A16 v1.6 (ESP32-WROOM-32E) - DEFAULT
// =============================================================================
#define BOARD_NAME           "KC868-A16 v1.6"
#define BOARD_MCU            "ESP32-WROOM-32E"
#define BOARD_HAS_PSRAM      0
#define BOARD_HAS_W5500      0   // Uses LAN8720A RMII
#define BOARD_HAS_RTC_DS3231 0
#define BOARD_HAS_OLED_SSD1306 0
#define BOARD_HAS_SDCARD     0

// I2C Bus
#define PIN_I2C_SDA          4
#define PIN_I2C_SCL          5

// RS485
#define PIN_RS485_RX         16
#define PIN_RS485_TX         13

// Analog Inputs (0-5V / 0-20mA)
#define PIN_ANALOG_CH1       36
#define PIN_ANALOG_CH2       34
#define PIN_ANALOG_CH3       35
#define PIN_ANALOG_CH4       39

// 1-Wire GPIO / HT Terminals
#define PIN_1WIRE_HT1        14
#define PIN_1WIRE_HT2        33
#define PIN_1WIRE_HT3        32

// LAN8720A Ethernet RMII
#define PIN_ETH_MDC          23
#define PIN_ETH_MDIO         18
#define PIN_ETH_CLK          17

// RF433MHz
#define PIN_RF433_RX         2
#define PIN_RF433_TX         15

#endif

// =============================================================================
// Common I2C Addresses (both versions)
// =============================================================================
#define I2C_ADDR_PCF8574_IN1  0x21  // Digital inputs 1-8
#define I2C_ADDR_PCF8574_IN2  0x22  // Digital inputs 9-16
#define I2C_ADDR_PCF8574_OUT1 0x24  // Relay outputs 1-8
#define I2C_ADDR_PCF8574_OUT2 0x25  // Relay outputs 9-16

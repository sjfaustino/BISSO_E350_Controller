/**
 * @file rs485_autodetect.h
 * @brief RS485 Bus Baud Rate Autodetect
 */

#ifndef RS485_AUTODETECT_H
#define RS485_AUTODETECT_H

#include <Arduino.h>

/**
 * @brief Scans common Modbus baud rates for known devices
 * @return The found baud rate, or 0 if none found
 */
int32_t rs485AutodetectBaud(void);

#endif

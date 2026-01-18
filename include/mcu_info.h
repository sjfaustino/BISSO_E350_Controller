/**
 * @file mcu_info.h
 * @brief Dynamic MCU Information Retrieval
 * @details Provides functions to get chip model, revision, and other hardware details at runtime.
 */

#ifndef MCU_INFO_H
#define MCU_INFO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the specific MCU model name (e.g., "ESP32-D0WDQ6")
 * @return String pointer to a statically allocated or persistent buffer
 */
const char* mcuGetModelName();

/**
 * Get the chip revision in "vX.Y" format
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Pointer to buffer
 */
const char* mcuGetRevisionString(char* buffer, size_t size);

/**
 * Check if the chip has PSRAM
 * @return True if PSRAM is found
 */
bool mcuHasPsram();

/**
 * Get total PSRAM size in bytes
 * @return PSRAM size in bytes
 */
uint32_t mcuGetPsramSize();

/**
 * Get total Flash size in bytes
 * @return Flash size in bytes
 */
uint32_t mcuGetFlashSize();

/**
 * Get number of CPU cores
 * @return Core count
 */
uint8_t mcuGetCoreCount();

/**
 * Get CPU frequency in MHz
 * @return Frequency in MHz
 */
uint32_t mcuGetCpuFreqMHz();

#ifdef __cplusplus
}
#endif

#endif // MCU_INFO_H

#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#include <stdint.h>
#include <stddef.h> 

// --- Project Identity ---
#define FIRMWARE_CODENAME           "PosiPro"
#define FIRMWARE_VERSION_MAJOR      1
#define FIRMWARE_VERSION_MINOR      0 
#define FIRMWARE_VERSION_PATCH      0

// --- String Representation ---
#define FIRMWARE_VERSION_STRING_LEN 32 // Increased to allow for longer codenames/build info

/**
 * @brief Get the full firmware version string (e.g., "PosiPro v1.0.0").
 * @param buffer Output buffer to store the version string.
 * @param buffer_size Size of the output buffer.
 * @return The version string buffer pointer.
 */
const char* firmwareGetVersionString(char* buffer, size_t buffer_size);

#endif // FIRMWARE_VERSION_H
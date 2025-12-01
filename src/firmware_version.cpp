#include "firmware_version.h"
#include <stdio.h> // For snprintf

// Global version string buffer (used internally)
static char version_buffer[FIRMWARE_VERSION_STRING_LEN];

const char* firmwareGetVersionString(char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < FIRMWARE_VERSION_STRING_LEN) {
        return "VERSION_ERR";
    }

    // Use snprintf to combine components into the final string
    snprintf(buffer, buffer_size, 
             "%s v%u.%u.%u", 
             FIRMWARE_CODENAME, 
             FIRMWARE_VERSION_MAJOR, 
             FIRMWARE_VERSION_MINOR, // Now correctly defined in the header
             FIRMWARE_VERSION_PATCH);
             
    return buffer;
}
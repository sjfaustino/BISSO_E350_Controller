#include "firmware_version.h"
#include <stdio.h> 

// FIX: Removed unused static buffer version_buffer
// static char version_buffer[FIRMWARE_VERSION_STRING_LEN];

const char* firmwareGetVersionString(char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < FIRMWARE_VERSION_STRING_LEN) {
        return "VERSION_ERR";
    }

    snprintf(buffer, buffer_size, 
             "%s v%u.%u.%u", 
             FIRMWARE_CODENAME, 
             FIRMWARE_VERSION_MAJOR, 
             FIRMWARE_VERSION_MINOR, 
             FIRMWARE_VERSION_PATCH);
             
    return buffer;
}
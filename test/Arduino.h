#ifndef ARDUINO_H_MOCK
#define ARDUINO_H_MOCK

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// Basic Arduino types
typedef uint8_t byte;

// Basic Arduino functions used in headers
static inline uint32_t millis() { return 0; }
static inline void delay(uint32_t ms) { (void)ms; }

// Logging functions used in project
void logError(const char* format, ...);
void logWarning(const char* format, ...);
void logInfo(const char* format, ...);
void logDebug(const char* format, ...);
void logVerbose(const char* format, ...);
void logPrintf(const char* format, ...);
void logPrintln(const char* format, ...);

#endif // ARDUINO_H_MOCK

#include "logging_mock.h"
#include <stdio.h>
#include <stdarg.h>

static int warning_count = 0;
static int error_count = 0;
static int fault_count = 0;

void mockLoggingReset() {
    warning_count = 0;
    error_count = 0;
    fault_count = 0;
}

int mockLoggingGetWarningCount() { return warning_count; }
int mockLoggingGetErrorCount() { return error_count; }
int mockLoggingGetFaultCount() { return fault_count; }

// --- Serial Logger Mocks ---

void logWarning(const char* format, ...) {
    warning_count++;
    va_list args; va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void logError(const char* format, ...) {
    error_count++;
    va_list args; va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void logInfo(const char* format, ...) {
    va_list args; va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void logPrintf(const char* format, ...) {
    va_list args; va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void logPrintln(const char* format, ...) {
    va_list args; va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void logDirectPrintf(const char* format, ...) {
    va_list args; va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void logDirectPrintln(const char* format, ...) {
    va_list args; va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

bool serialLoggerLock() { return true; }
void serialLoggerUnlock() {}

// --- Fault Logging Mocks ---

void faultLogWarning(fault_code_t code, const char* message) {
    fault_count++;
    printf("[MOCK_FAULT] WARN: Code %d - %s\n", (int)code, message);
}

void faultLogError(fault_code_t code, const char* message) {
    fault_count++;
    printf("[MOCK_FAULT] ERROR: Code %d - %s\n", (int)code, message);
}

const char* faultSeverityToString(fault_severity_t severity) {
    switch(severity) {
        case FAULT_NONE: return "NONE";
        case FAULT_WARNING: return "WARN";
        case FAULT_ERROR: return "ERROR";
        case FAULT_CRITICAL: return "CRITICAL";
        default: return "UNK";
    }
}

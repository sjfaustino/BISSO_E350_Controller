/**
 * @file src/native_main.cpp
 * @brief Minimal entry point for native (Windows/Linux) builds.
 * 
 * This file provides a dummy main() function to satisfy PlatformIO's 
 * build system when running 'pio run' on the native 'test' environment.
 * Actual unit tests should still be run via 'pio test'.
 */

#ifndef ARDUINO
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("BISSO E350 - Native Build (Compatibility Layer)\n");
    printf("Use 'pio test -e test' to run unit tests.\n");
    return 0;
}
#endif

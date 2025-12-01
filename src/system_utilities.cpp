#include "system_utilities.h"
#include <string.h>
#include <ctype.h>

char axisIndexToChar(uint8_t index) {
    switch(index) {
        case 0: return 'X';
        case 1: return 'Y';
        case 2: return 'Z';
        case 3: return 'A';
        default: return '?'; 
    }
}

uint8_t axisCharToIndex(char* arg) {
    if (arg == NULL || strlen(arg) != 1) return 255;
    char axis_letter = toupper(arg[0]);
    if (axis_letter == 'X') return 0;
    if (axis_letter == 'Y') return 1;
    if (axis_letter == 'Z') return 2;
    if (axis_letter == 'A') return 3;
    return 255;
}
#include "system_constants.h"

const char* resultToString(result_t result) {
  switch(result) {
    case RESULT_OK: return "OK";
    case RESULT_ERROR: return "ERROR";
    case RESULT_TIMEOUT: return "TIMEOUT";
    case RESULT_NACK: return "NACK";
    case RESULT_BUS_ERROR: return "BUS_ERROR";
    case RESULT_INVALID_PARAM: return "INVALID_PARAM";
    case RESULT_NOT_READY: return "NOT_READY";
    case RESULT_BUSY: return "BUSY";
    case RESULT_ERROR_HARDWARE: return "ERROR_HARDWARE";
    case RESULT_ERROR_MEMORY: return "ERROR_MEMORY";
    case RESULT_ERROR_STORAGE: return "ERROR_STORAGE";
    case RESULT_UNKNOWN: return "UNKNOWN";
    default: return "UNDEFINED";
  }
}

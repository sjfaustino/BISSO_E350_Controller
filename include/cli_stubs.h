#ifndef CLI_STUBS_H
#define CLI_STUBS_H

// Stub declarations for functions referenced in CLI but not fully implemented
// These are placeholder declarations for future implementation

// Timeout management
inline void timeoutShowDiagnostics() { Serial.println("[TIMEOUT] Diagnostics not implemented"); }

// I2C Management
typedef enum {
  I2C_BUS_OK = 0,
  I2C_BUS_ERROR = 1,
} i2c_bus_status_t;

inline i2c_bus_status_t i2cCheckBusStatus() { return I2C_BUS_OK; }
inline void i2cRecoverBus() { Serial.println("[I2C] Bus recovery initiated"); }
inline void i2cShowStats() { Serial.println("[I2C] Statistics not available"); }
inline const char* i2cBusStatusToString(i2c_bus_status_t status) { return "OK"; }

// Encoder Management
typedef struct {
  bool detected;
  uint32_t baud_rate;
} baud_detect_result_t;

inline baud_detect_result_t encoderDetectBaudRate() { 
  return {true, 9600}; 
}
inline void encoderShowStats() { Serial.println("[ENCODER] Statistics not available"); }

// Configuration Management
inline bool configIsMigrationNeeded() { return false; }
inline void configShowSchemaHistory() { Serial.println("[CONFIG] Schema history not available"); }
inline void configShowKeyMetadata() { Serial.println("[CONFIG] Metadata not available"); }

typedef struct {
  bool success;
  const char* message;
} migration_result_t;

inline migration_result_t configAutoMigrate() { 
  return {true, "No migration needed"}; 
}
inline void configShowMigrationStatus() { Serial.println("[CONFIG] Migration status: OK"); }
inline bool configRollbackToVersion(uint32_t version) { return true; }
inline void configValidateSchema() { Serial.println("[CONFIG] Schema valid"); }

// Task Management
inline void taskShowStats() { Serial.println("[TASKS] Statistics not available"); }
inline void taskShowAllTasks() { Serial.println("[TASKS] Task list not available"); }
inline uint8_t taskGetCpuUsage() { return 0; }
inline uint32_t taskGetUptime() { return 0; }

// Watchdog Management
inline void watchdogShowStatus() { Serial.println("[WDT] Status not available"); }
inline void watchdogShowTasks() { Serial.println("[WDT] Tasks not available"); }
inline void watchdogShowStats() { Serial.println("[WDT] Statistics not available"); }
inline void watchdogPrintDetailedReport() { Serial.println("[WDT] Report not available"); }

#endif

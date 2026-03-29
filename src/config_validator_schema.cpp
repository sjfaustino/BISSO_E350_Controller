/**
 * @file config_validator_schema.cpp
 * @brief Configuration Schema Implementation
 */

#include "config_validator_schema.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Configuration schema descriptors
// Central definition of all valid configuration parameters
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const config_descriptor_t config_schema[] = {
 // === NETWORK ===
 {
 .key = KEY_WIFI_SSID,
 .type = CONFIG_TYPE_STRING,
 .string_max_len = 32,
 .default_value = "BISSO-E350",
 .description = "WiFi network name (SSID)",
 .unit = NULL,
 .critical = false
 },
 {
 .key = KEY_WIFI_PASS,
 .type = CONFIG_TYPE_STRING,
 .string_max_len = 64,
 .default_value = "default_password",
 .description = "WiFi network password",
 .unit = NULL,
 .critical = false
 },
 {
 .key = KEY_WIFI_AP_EN,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "1",
 .description = "WiFi AP Mode Enable (1=on, 0=off)",
 .unit = "bool",
 .critical = false
 },
 {
 .key = KEY_WIFI_AP_SSID,
 .type = CONFIG_TYPE_STRING,
 .string_max_len = 32,
 .default_value = "BISSO-E350-Setup",
 .description = "WiFi AP network name",
 .unit = NULL,
 .critical = false
 },
 {
 .key = KEY_WIFI_AP_PASS,
 .type = CONFIG_TYPE_STRING,
 .string_max_len = 64,
 .default_value = "password",
 .description = "WiFi AP password",
 .unit = NULL,
 .critical = false
 },

 // === WEB SERVER ===
 {
 .key = KEY_WEB_USERNAME,
 .type = CONFIG_TYPE_STRING,
 .string_max_len = 32,
 .default_value = "admin",
 .description = "HTTP authentication username",
 .unit = NULL,
 .critical = false
 },
 {
 .key = KEY_WEB_PASSWORD,
 .type = CONFIG_TYPE_STRING,
 .string_max_len = 64,
 .default_value = "password",
 .description = "HTTP authentication password",
 .unit = NULL,
 .critical = false
 },
 {
 .key = KEY_WEB_PORT,
 .type = CONFIG_TYPE_INT,
 .int_min = 80,
 .int_max = 65535,
 .default_value = "80",
 .description = "HTTP server port",
 .unit = "port",
 .critical = false
 },

 // === MOTION (ENCODER RESOLUTION) ===
 {
 .key = KEY_PPM_X,
 .type = CONFIG_TYPE_INT, // Note: Code uses int32 for these
 .int_min = 10,
 .int_max = 200000,
 .default_value = "100",
 .description = "X-axis encoder pulses per millimeter",
 .unit = "pulses/mm",
 .critical = true
 },
 {
 .key = KEY_PPM_Y,
 .type = CONFIG_TYPE_INT,
 .int_min = 10,
 .int_max = 200000,
 .default_value = "100",
 .description = "Y-axis encoder pulses per millimeter",
 .unit = "pulses/mm",
 .critical = true
 },
 {
 .key = KEY_PPM_Z,
 .type = CONFIG_TYPE_INT,
 .int_min = 10,
 .int_max = 200000,
 .default_value = "100",
 .description = "Z-axis encoder pulses per millimeter",
 .unit = "pulses/mm",
 .critical = true
 },
 {
 .key = KEY_PPM_A,
 .type = CONFIG_TYPE_INT,
 .int_min = 10,
 .int_max = 200000,
 .default_value = "100",
 .description = "A-axis encoder pulses per degree",
 .unit = "pulses/deg",
 .critical = true
 },
 
 // === MOTION BEHAVIOR ===
 {
 .key = KEY_DEFAULT_SPEED,
 .type = CONFIG_TYPE_FLOAT,
 .float_min = 1.0f,
 .float_max = 500.0f,
 .default_value = "100.0",
 .description = "Default motion speed",
 .unit = "mm/min",
 .critical = false
 },
 {
 .key = KEY_DEFAULT_ACCEL,
 .type = CONFIG_TYPE_FLOAT,
 .float_min = 1.0f,
 .float_max = 1000.0f,
 .default_value = "50.0",
 .description = "Default motion acceleration",
 .unit = "mm/sec²",
 .critical = false
 },
 {
 .key = KEY_MOTION_STRICT_LIMITS,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "1",
 .description = "Strict soft limits (1=stop immediately)",
 .unit = "bool",
 .critical = true
 },

 // === VFD (ALTIVAR31) ===
 {
 .key = KEY_VFD_EN,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "1",
 .description = "Enable VFD Communication (1=on, 0=off)",
 .unit = "bool",
 .critical = true
 },
 {
 .key = KEY_VFD_ADDR,
 .type = CONFIG_TYPE_INT,
 .int_min = 1,
 .int_max = 247,
 .default_value = "2",
 .description = "VFD Modbus Address",
 .unit = "addr",
 .critical = true
 },

 // === SPINDLE MONITOR (JXK-10) ===
 {
 .key = KEY_JXK10_ENABLED,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "1",
 .description = "Enable JXK-10 Current Sensor (1=on, 0=off)",
 .unit = "bool",
 .critical = true
 },
 {
 .key = KEY_JXK10_ADDR,
 .type = CONFIG_TYPE_INT,
 .int_min = 1,
 .int_max = 247,
 .default_value = "1",
 .description = "JXK-10 Modbus Address",
 .unit = "addr",
 .critical = true
 },
 {
 .key = KEY_SPINDLE_THRESHOLD,
 .type = CONFIG_TYPE_INT, // Code uses int
 .int_min = 1,
 .int_max = 50,
 .default_value = "30",
 .description = "Spindle overcurrent alarm threshold",
 .unit = "A",
 .critical = true
 },

 // === TACHOMETER (YH-TC05) ===
 {
 .key = KEY_YHTC05_ENABLED,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "0", // Default disabled for now to avoid bus spam
 .description = "Enable Tachometer (1=on, 0=off)",
 .unit = "bool",
 .critical = true
 },
 {
 .key = KEY_YHTC05_ADDR,
 .type = CONFIG_TYPE_INT,
 .int_min = 1,
 .int_max = 247,
 .default_value = "3",
 .description = "Tachometer Modbus Address",
 .unit = "addr",
 .critical = true
 },

 // === SAFETY ===
 {
 .key = KEY_STALL_TIMEOUT,
 .type = CONFIG_TYPE_INT,
 .int_min = 100,
 .int_max = 10000,
 .default_value = "2000",
 .description = "Stall detection timeout",
 .unit = "ms",
 .critical = false
 },
 {
 .key = KEY_ENC_DEV_TIMEOUT,
 .type = CONFIG_TYPE_INT,
 .int_min = 100,
 .int_max = 10000,
 .default_value = "5000",
 .description = "Encoder deviation timeout",
 .unit = "ms",
 .critical = false
 },

 // === LIMITS ===
 {
 .key = KEY_X_LIMIT_MIN,
 .type = CONFIG_TYPE_INT, 
 .int_min = -1000000,
 .int_max = 0,
 .default_value = "-500000",
 .description = "X-axis min limit",
 .unit = "int",
 .critical = true
 },
 {
 .key = KEY_X_LIMIT_MAX,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1000000,
 .default_value = "500000",
 .description = "X-axis max limit",
 .unit = "int",
 .critical = true
 },
 // Y Axis
 {
 .key = KEY_Y_LIMIT_MIN, .type = CONFIG_TYPE_INT, .int_min = -1000000, .int_max = 0, .default_value = "-500000", .description = "Y min limit", .unit = "int", .critical = true
 },
 {
 .key = KEY_Y_LIMIT_MAX, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 1000000, .default_value = "500000", .description = "Y max limit", .unit = "int", .critical = true
 },
 // Z Axis
 {
 .key = KEY_Z_LIMIT_MIN, .type = CONFIG_TYPE_INT, .int_min = -1000000, .int_max = 0, .default_value = "-100000", .description = "Z min limit", .unit = "int", .critical = true
 },
 {
 .key = KEY_Z_LIMIT_MAX, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 1000000, .default_value = "100000", .description = "Z max limit", .unit = "int", .critical = true
 },
 
 // === SYSTEM ===
 {
 .key = "system_log_level",
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 5,
 .default_value = "2",
 .description = "System logging level (0=error, 5=debug)",
 .unit = "level",
 .critical = false
 },
 {
 .key = KEY_STATUS_LIGHT_EN,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "0",
 .description = "Enable status light (1=on, 0=off)",
 .unit = "bool",
 .critical = false
 },
 {
 .key = KEY_BUZZER_EN,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "0",
 .description = "Enable alarm buzzer (1=on, 0=off)",
 .unit = "bool",
 .critical = false
 },
 {
 .key = KEY_LCD_EN,
 .type = CONFIG_TYPE_INT,
 .int_min = 0,
 .int_max = 1,
 .default_value = "1",
 .description = "Enable LCD display (1=on, 0=off)",
 .unit = "bool",
 .critical = false
 },
 
 // Status Pins
 { .key = KEY_STATUS_LIGHT_GREEN, .type = CONFIG_TYPE_INT, .int_min = 1, .int_max = 48, .default_value = "13", .description = "Green light pin", .unit = "pin", .critical = false },
 { .key = KEY_STATUS_LIGHT_YELLOW, .type = CONFIG_TYPE_INT, .int_min = 1, .int_max = 48, .default_value = "14", .description = "Yellow light pin", .unit = "pin", .critical = false },
 { .key = KEY_STATUS_LIGHT_RED, .type = CONFIG_TYPE_INT, .int_min = 1, .int_max = 48, .default_value = "15", .description = "Red light pin", .unit = "pin", .critical = false },
 { .key = KEY_BUZZER_PIN, .type = CONFIG_TYPE_INT, .int_min = 1, .int_max = 48, .default_value = "16", .description = "Buzzer pin", .unit = "pin", .critical = false },
 { .key = KEY_ALARM_PIN, .type = CONFIG_TYPE_INT, .int_min = 1, .int_max = 48, .default_value = "2", .description = "Alarm input pin", .unit = "pin", .critical = false },

 {
 .key = "system_watchdog_timeout_s",
 .type = CONFIG_TYPE_INT,
 .int_min = 5,
 .int_max = 300,
 .default_value = "30",
 .description = "Watchdog timeout",
 .unit = "seconds",
 .critical = false
 },
 
 // === HOME PROFILES ===
 { .key = KEY_HOME_PROFILE_FAST, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 2, .default_value = "2", .description="Fast Homing Profile", .unit="idx", .critical=false},
 { .key = KEY_HOME_PROFILE_SLOW, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 2, .default_value = "0", .description="Slow Homing Profile", .unit="idx", .critical=false},

 // === RECOVERY ===
 { .key = KEY_RECOV_EN, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 1, .default_value = "1", .description="Job Recovery Enable", .unit="bool", .critical=false},
 
 // === BOOTLOG ===
 { .key = KEY_BOOTLOG_EN, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 1, .default_value = "1", .description="Boot Log Enable", .unit="bool", .critical=false},

 // === MISC ===
 { .key = KEY_CLI_ECHO, .type = CONFIG_TYPE_INT, .int_min = 0, .int_max = 1, .default_value = "1", .description="CLI Echo", .unit="bool", .critical=false},
};
#pragma GCC diagnostic pop

static const int schema_count = sizeof(config_schema) / sizeof(config_descriptor_t);

void configSchemaInit() {
 logInfo("[CONFIG_SCHEMA] Initialized with %d parameters", schema_count);
}

const config_descriptor_t* configGetDescriptor(const char* key) {
 if (!key) return NULL;

 for (int i = 0; i < schema_count; i++) {
 if (strcmp(config_schema[i].key, key) == 0) {
 return &config_schema[i];
 }
 }
 return NULL;
}

const config_descriptor_t* configGetAllDescriptors(int* count) {
 if (count) *count = schema_count;
 return config_schema;
}

config_validation_result_t configValidateValue(const char* key, const char* value) {
 config_validation_result_t result = {
 .is_valid = false,
 .error_message = NULL,
 .was_clamped = false,
 .clamped_to = NULL
 };

 if (!key || !value) {
 result.is_valid = false;
 result.error_message = "Key or value is NULL";
 return result;
 }

 const config_descriptor_t* desc = configGetDescriptor(key);
 if (!desc) {
 result.is_valid = false;
 result.error_message = "Unknown configuration key";
 return result;
 }

 // Type-specific validation
 switch (desc->type) {
 case CONFIG_TYPE_INT: {
 char* endptr;
 long int_val = strtol(value, &endptr, 10);
 if (*endptr != '\0') {
 result.error_message = "Invalid integer format";
 result.is_valid = false;
 break;
 }
 if (int_val < desc->int_min || int_val > desc->int_max) {
 result.was_clamped = true;
 if (int_val < desc->int_min) {
 snprintf((char*)desc->default_value, 16, "%ld", (long)desc->int_min);
 result.clamped_to = "minimum";
 } else {
 snprintf((char*)desc->default_value, 16, "%ld", (long)desc->int_max);
 result.clamped_to = "maximum";
 }
 }
 result.is_valid = true;
 break;
 }

 case CONFIG_TYPE_FLOAT: {
 char* endptr;
 float float_val = strtof(value, &endptr);
 if (*endptr != '\0') {
 result.error_message = "Invalid float format";
 result.is_valid = false;
 break;
 }
 if (float_val < desc->float_min || float_val > desc->float_max) {
 result.was_clamped = true;
 if (float_val < desc->float_min) {
 result.clamped_to = "minimum";
 } else {
 result.clamped_to = "maximum";
 }
 }
 result.is_valid = true;
 break;
 }

 case CONFIG_TYPE_STRING: {
 if (strlen(value) > desc->string_max_len) {
 result.error_message = "String too long";
 result.is_valid = false;
 } else {
 result.is_valid = true;
 }
 break;
 }

 case CONFIG_TYPE_BOOL: {
 if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0 &&
 strcmp(value, "1") != 0 && strcmp(value, "0") != 0) {
 result.error_message = "Invalid boolean value (use: true|false|0|1)";
 result.is_valid = false;
 } else {
 result.is_valid = true;
 }
 break;
 }

 default:
 result.error_message = "Unknown config type";
 result.is_valid = false;
 }

 return result;
}

bool configValidateAll() {
 bool all_valid = true;

 for (int i = 0; i < schema_count; i++) {
 const config_descriptor_t* desc = &config_schema[i];

 // Get current value from NVS
 const char* current_value = NULL;
 if (desc->type == CONFIG_TYPE_STRING) {
 current_value = configGetString(desc->key, NULL);
 }
 // For numeric types, would need to read and convert

 if (!current_value && desc->type == CONFIG_TYPE_STRING) {
 logWarning("[CONFIG_SCHEMA] Missing key: %s (critical: %s)",
 desc->key, desc->critical ? "yes" : "no");
 if (desc->critical) {
 all_valid = false;
 }
 }
 }

 return all_valid;
}

size_t configExportSchemaJSON(char* buffer, size_t buffer_size) {
 if (!buffer || buffer_size < 512) return 0;

 size_t offset = 0;
 offset += snprintf(buffer + offset, buffer_size - offset, "{\"schema\":[");

 for (int i = 0; i < schema_count; i++) {
 const config_descriptor_t* desc = &config_schema[i];

 if (i > 0) {
 offset += snprintf(buffer + offset, buffer_size - offset, ",");
 }

 const char* type_str = "unknown";
 switch (desc->type) {
 case CONFIG_TYPE_INT: type_str = "int"; break;
 case CONFIG_TYPE_FLOAT: type_str = "float"; break;
 case CONFIG_TYPE_STRING: type_str = "string"; break;
 case CONFIG_TYPE_BOOL: type_str = "bool"; break;
 }

 offset += snprintf(buffer + offset, buffer_size - offset,
 "{\"key\":\"%s\",\"type\":\"%s\",\"description\":\"%s\",\"default\":\"%s\",\"critical\":%s}",
 desc->key, type_str, desc->description ? desc->description : "",
 desc->default_value ? desc->default_value : "", desc->critical ? "true" : "false");

 // Prevent buffer overrun
 if (offset >= buffer_size - 50) {
 offset = buffer_size - 50;
 break;
 }
 }

 offset += snprintf(buffer + offset, buffer_size - offset, "]}");
 return offset;
}

void configPrintSchema() {
 serialLoggerLock();
 logPrintln("\n[CONFIG SCHEMA] === Configuration Parameters ===");
 logPrintln("Key | Type | Min/Max | Unit | Critical");
 logPrintln("-----------------------------|--------|------------------|-----------|----------");

 for (int i = 0; i < schema_count; i++) {
 const config_descriptor_t* desc = &config_schema[i];

 char type_str[8];
 char range_str[20];

 switch (desc->type) {
 case CONFIG_TYPE_INT:
 snprintf(type_str, sizeof(type_str), "INT");
 snprintf(range_str, sizeof(range_str), "%ld..%ld",
 (long)desc->int_min, (long)desc->int_max);
 break;
 case CONFIG_TYPE_FLOAT:
 snprintf(type_str, sizeof(type_str), "FLOAT");
 snprintf(range_str, sizeof(range_str), "%.1f..%.1f",
 desc->float_min, desc->float_max);
 break;
 case CONFIG_TYPE_STRING:
 snprintf(type_str, sizeof(type_str), "STRING");
 snprintf(range_str, sizeof(range_str), "<%zu chars", desc->string_max_len);
 break;
 case CONFIG_TYPE_BOOL:
 snprintf(type_str, sizeof(type_str), "BOOL");
 snprintf(range_str, sizeof(range_str), "true/false");
 break;
 default:
 snprintf(type_str, sizeof(type_str), "?");
 snprintf(range_str, sizeof(range_str), "?");
 }

 logPrintf("%-35s | %-6s | %-16s | %-9s | %s\r\n",
 desc->key, type_str, range_str,
 desc->unit ? desc->unit : "-",
 desc->critical ? "YES" : "no");
 }

 logPrintf("\r\nTotal parameters: %d\r\n\r\n", schema_count);
 serialLoggerUnlock();
}

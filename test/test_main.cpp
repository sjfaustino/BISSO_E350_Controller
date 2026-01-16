/**
 * @file test/test_main.cpp
 * @brief Main entry point for native unit tests
 *
 * Provides setUp/tearDown hooks and main() for Unity test framework
 */

#include <unity.h>

// External test registration functions
extern void run_altivar31_vfd_tests(void);
extern void run_auth_manager_tests(void);
extern void run_axis_synchronization_tests(void);
extern void run_config_defaults_tests(void);
extern void run_cutting_analytics_tests(void);
extern void run_dashboard_metrics_tests(void);
extern void run_encoder_hal_tests(void);
extern void run_fault_logging_tests(void);
extern void run_i2c_recovery_tests(void);
extern void run_lcd_formatter_tests(void);
extern void run_modbus_rtu_tests(void);
extern void run_motion_planner_tests(void);
extern void run_rs485_registry_tests(void);
extern void run_hardware_optimization_tests(void);

// Unity setup/teardown hooks
void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

// Common test runner
void run_all_tests(void) {
    UNITY_BEGIN();
    
    run_altivar31_vfd_tests();
    run_auth_manager_tests();
    run_axis_synchronization_tests();
    run_config_defaults_tests();
    run_cutting_analytics_tests();
    run_dashboard_metrics_tests();
    run_encoder_hal_tests();
    run_fault_logging_tests();
    run_i2c_recovery_tests();
    run_lcd_formatter_tests();
    run_modbus_rtu_tests();
    run_motion_planner_tests();
    run_rs485_registry_tests();
    run_hardware_optimization_tests();
    
    UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    // Wait for serial to settle
    delay(2000);
    run_all_tests();
}
void loop() {
    // Nothing to do
    delay(100);
}
#else
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    run_all_tests();
    return 0;
}
#endif


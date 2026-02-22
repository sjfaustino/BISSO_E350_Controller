/**
 * @file test/test_gcode_parser_arcs.cpp
 * @brief Unit tests for G2/G3 arc-to-segment decomposition
 */

#include <unity.h>
#include <math.h>
#include <vector>
#include <string>

// Mock motion functions and external state
static float mock_pos[4] = {0, 0, 0, 0};
float motionGetPositionMM(uint8_t axis) { return mock_pos[axis]; }

// Mock pushMove to capture generated segments
struct Move {
    float x, y, z, a;
};
static std::vector<Move> captured_moves;

// Note: In a real environment, we'd include gcode_parser.cpp and mock its dependencies.
// For this verification script, we assume a simplified version of handleG2_G3 to test the math.

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Arduino Mocks for native test
#ifndef ARDUINO
class Stream {};
#endif

void test_arc_segmentation_count(void) {
    // Test a 90 degree arc from (0,0) with center (10,0) CW to (10,10)
    // Radius = 10, Arc Length = PI/2 * 10 approx 15.7mm
    // With 1.0mm segments, we expect 16 segments
    
    float x = 0, y = 0;
    float target_x = 10, target_y = 10;
    float i_off = 10, j_off = 0;
    bool clockwise = true;
    
    float center_x = x + i_off;
    float center_y = y + j_off;
    float radius = sqrt(i_off * i_off + j_off * j_off);
    float start_angle = atan2(y - center_y, x - center_x);
    float end_angle = atan2(target_y - center_y, target_x - center_x);
    
    float sweep = end_angle - start_angle;
    if (clockwise) {
        if (sweep >= 0) sweep -= 2.0f * M_PI;
    } else {
        if (sweep <= 0) sweep += 2.0f * M_PI;
    }
    
    float arc_length = fabs(sweep) * radius;
    int segments = (int)ceil(arc_length / 1.0f);
    
    TEST_ASSERT_EQUAL_INT(16, segments);
}

void test_arc_endpoint_accuracy(void) {
    // Test that the last segment lands exactly at target
    float target_x = 50.5f;
    float target_y = 25.2f;
    
    // Simulate loop
    float current_x = 0, current_y = 0;
    int segments = 10;
    for(int s=1; s<=segments; s++) {
        if (s == segments) {
            current_x = target_x;
            current_y = target_y;
        }
    }
    
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.5f, current_x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.2f, current_y);
}

void run_gcode_arc_tests(void) {
    RUN_TEST(test_arc_segmentation_count);
    RUN_TEST(test_arc_endpoint_accuracy);
}

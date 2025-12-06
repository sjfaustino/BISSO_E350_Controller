/**
 * @file motion_planner.h
 * @brief Trajectory Planning & Buffer Management
 * @project Gemini v3.0.0
 */

#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include "motion.h"
#include "motion_buffer.h"

class MotionPlanner {
public:
    MotionPlanner();
    void init();
    
    // Main Planning Cycle (Called from Control Loop)
    void update(motion_axis_t* axes, uint8_t& active_axis, int32_t& active_start_pos);

    // Feed Rate API
    void setFeedOverride(float factor);
    float getFeedOverride();

private:
    float feed_override;

    // Helpers
    bool checkBufferDrain(uint8_t& active_axis);
    bool checkLookAhead(motion_axis_t* axis, uint8_t active_axis, int32_t& active_start_pos);
    void applyDynamicApproach(motion_axis_t* axis, uint8_t active_axis, int32_t current_pos);
};

extern MotionPlanner motionPlanner;

#endif
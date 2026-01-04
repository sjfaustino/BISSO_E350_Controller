#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include "motion.h" // Provides 'Axis' class definition
#include "motion_buffer.h"

class MotionPlanner {
public:
    MotionPlanner();
    void init();
    
    // Main Planning Cycle
    void update(Axis* axes, uint8_t& active_axis, int32_t& active_start_pos);

    // Feed Rate API
    void setFeedOverride(float factor);
    float getFeedOverride();

private:
    float feed_override;

    // Helpers
    bool checkBufferDrain(uint8_t& active_axis);
    bool checkLookAhead(Axis* axis, uint8_t active_axis, int32_t& active_start_pos);
    void applyDynamicApproach(Axis* axis, uint8_t active_axis, int32_t current_pos);
};

extern MotionPlanner motionPlanner;

#endif

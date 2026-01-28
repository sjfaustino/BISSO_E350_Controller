#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <stdint.h>

/**
 * @brief ESP-NOW Telemetry Packet Structure
 * 
 * This structure MUST match exactly between the main controller
 * and any remote receiving devices (DRO boxes).
 */
struct TelemetryPacket {
    float x;        ///< Machine X position (mm)
    float y;        ///< Machine Y position (mm)
    float z;        ///< Machine Z position (mm)
    uint32_t status; ///< 0=READY, 1=MOVING, 2=ALARM, 3=E-STOP
    uint32_t uptime; ///< System uptime in seconds
};

#endif // TELEMETRY_PACKET_H

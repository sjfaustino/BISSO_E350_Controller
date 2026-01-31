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
    uint32_t signature;  ///< Protocol signature (0x42495353 = "BISS")
    uint8_t  channel;    ///< Controller's current radio channel
    uint8_t  pad[3];     ///< Alignment padding
    float x;             ///< Machine X position (mm)
    float y;             ///< Machine Y position (mm)
    float z;             ///< Machine Z position (mm)
    uint32_t status;     ///< 0=READY, 1=MOVING, 2=ALARM, 3=E-STOP
    uint32_t uptime;     ///< System uptime in seconds
};

#endif // TELEMETRY_PACKET_H

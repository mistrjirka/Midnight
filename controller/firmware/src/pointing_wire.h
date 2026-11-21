#pragma once
#include <stddef.h>
#include <stdint.h>
struct CameraPointing {
    uint32_t sequence, cameraUptimeMs, ra, ageAtReadMs;
    int32_t dec;
    uint16_t fovCdeg, matches;
    uint8_t source;
    bool valid;
};
bool parseCameraPointing(const uint8_t *payload, size_t size, CameraPointing &out);
/* The controller must not use a gallery or synthetic solution for closed-loop movement. */
bool usableLivePointing(const CameraPointing &p);

#pragma once
#include <stdint.h>
#include "pointing_wire.h"
void cameraHostStart();
bool cameraHostRunning();
const char *cameraHostState();
/* Returns a validated PTP packet and the local reception time, if one arrived. */
bool cameraHostLatest(CameraPointing &out, uint32_t &receivedAtMs);

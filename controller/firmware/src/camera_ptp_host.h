#pragma once
#include <stdint.h>
#include <Arduino.h>
#include "pointing_wire.h"
#include "mount_mailbox_wire.h"
#include "mount_coordinator.h"
void cameraHostStart();
bool cameraHostRunning();
const char *cameraHostState();
String cameraHostDiagnostics();
/* Returns a validated PTP packet and the local reception time, if one arrived. */
bool cameraHostLatest(CameraPointing &out, uint32_t &receivedAtMs);

enum class CameraPhotoState: uint8_t {
    IDLE=0, QUEUED, REQUESTED, COMPLETE, FAILED, TIMED_OUT
};
struct CameraMountView {
    CameraMountMailbox mailbox={};
    bool mailboxValid=false;
    MountNavState navigation=MountNavState::IDLE;
    bool targetActive=false;
    uint32_t targetRa=0,lastPhotoRa=0;
    int32_t targetDec=0,lastPhotoDec=0;
    uint32_t lastRequestId=0;
    CameraPhotoState photoState=CameraPhotoState::IDLE;
};
/* Nonblocking. USB work happens only on the dedicated PTP task.
 * The request is accepted only when a PTP camera session is open. */
bool cameraHostRequestMeasure(uint32_t &id);
bool cameraHostMountView(CameraMountView &view);
const char *cameraHostPhotoStatus(CameraPhotoState state);
const char *cameraHostNavigationStatus(MountNavState state);

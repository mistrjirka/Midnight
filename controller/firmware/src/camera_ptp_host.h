#pragma once
#include <stdint.h>
#include <Arduino.h>
#include "pointing_wire.h"
#include "mount_mailbox_wire.h"
#include "mount_coordinator.h"
#include "goto_loop.h"
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
    GoToPhase loopPhase=GoToPhase::IDLE;
    GoToStage loopStage=GoToStage::BASELINE;
    GoToFault loopFault=GoToFault::NONE;
    uint32_t loopErrorE4=0,loopCorrections=0;
    bool autoMotorReady=false,nextPulseValid=false;
    mount_pulse nextPulse={};
};
/* Nonblocking. USB work happens only on the dedicated PTP task.
 * The request is accepted only when a PTP camera session is open. */
bool cameraHostRequestMeasure(uint32_t &id);
bool cameraHostMountView(CameraMountView &view);
const char *cameraHostPhotoStatus(CameraPhotoState state);
const char *cameraHostNavigationStatus(MountNavState state);


/* All automatic pulses are refused until the motor interlock is installed.
 * This is not the serial manual-SLEW interface. */
const char *cameraHostLoopStatus(GoToPhase phase,bool motorReady);
const char *cameraHostLoopFault(GoToFault code);


/* Thread-safe requests from the serial/HTTP main loop. No USB access here. */
void cameraHostCancelGoTo();
void cameraHostGoToLinkLost();

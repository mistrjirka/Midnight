/* ESP32 owns destination, camera measurement requests, and navigation state.
 * It intentionally cannot drive either motor until the second driver and
 * camera USB electrical link are verified. GPL-2.0-or-later. */
#pragma once
#include <stdint.h>
#include "mount_mailbox_wire.h"
#include "pointing_wire.h"
extern "C" {
#include "../../../modules/astro/mount_model.h"
}
enum class MountNavState: uint8_t {
    IDLE=0, REQUEST_PHOTO, QUEUED, WAIT_PHOTO,
    WAIT_POINT, MOTORS_UNAVAILABLE, FAILED, STOPPED
};
class MountCoordinator {
public:
    void observeMailbox(const CameraMountMailbox &m);
    void observePoint(const CameraPointing &p);
    bool needsPhoto() const {return state_==MountNavState::REQUEST_PHOTO;}
    void photoQueued(uint32_t request);
    void photoSent(uint32_t request,bool accepted);
    void linkFailed();
    MountNavState state() const {return state_;}
    const char *status() const;
    uint32_t targetRa() const {return targetRa_;}
    int32_t targetDec() const {return targetDec_;}
    uint32_t lastPhotoRa() const {return lastRa_;}
    int32_t lastPhotoDec() const {return lastDec_;}
    uint32_t photoRequestId() const {return requestId_;}
    bool targetActive() const {return targetActive_;}
    /* Calibrate from measured before/after solves once each real motor exists. */
    bool calibrateAxis(int axis,int direction,uint32_t pulseMs,
                       mount_point before,mount_point after);
    /* Pure planning. No actuator calls in this class or at startup. */
    int planPulse(mount_pulse &out) const;
private:
    MountNavState state_=MountNavState::IDLE;
    uint32_t lastCommand_=0,targetRa_=0,requestId_=0,expectedSequence_=0;
    uint32_t lastRa_=0;
    int32_t targetDec_=0,lastDec_=0;
    bool targetActive_=false;
    mount_model geometry_={};
};

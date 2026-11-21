#include "mount_coordinator.h"
void MountCoordinator::observeMailbox(const CameraMountMailbox &m) {
    if(m.commandSequence && m.commandSequence!=lastCommand_){
        lastCommand_=m.commandSequence;
        expectedSequence_=0;
        requestId_=0;
        if(m.command==2) {
            targetActive_=false;
            state_=MountNavState::STOPPED;
        } else if(m.command==1 && m.targetValid) {
            targetRa_=m.targetRa;
            targetDec_=m.targetDec;
            targetActive_=true;
            state_=MountNavState::REQUEST_PHOTO;
        } else {
            targetActive_=false;
            state_=MountNavState::FAILED;
        }
    }
    if(state_==MountNavState::WAIT_PHOTO &&
       m.completedId==requestId_ && m.captureState==2) {
        expectedSequence_=m.pointSequence;
        state_=expectedSequence_?MountNavState::WAIT_POINT:MountNavState::FAILED;
    }
    if(state_==MountNavState::WAIT_PHOTO &&
       m.completedId==requestId_ && m.captureState==3)
        state_=MountNavState::FAILED;
}
void MountCoordinator::observePoint(const CameraPointing &p) {
    if(state_!=MountNavState::WAIT_POINT ||
       !p.valid || p.source!=5 || p.sequence!=expectedSequence_ ||
       !usableLivePointing(p))return;
    lastRa_=p.ra;lastDec_=p.dec;
    mount_point measured={p.ra,p.dec,0,1};
    if(!geometry_.current.valid)mount_sync(&geometry_,measured);
    else mount_set_current(&geometry_,measured);
    /* Requires independent motor calibration and hardware fail-safe.
     * We do not move motors based only on successful image acquisition. */
    state_=MountNavState::MOTORS_UNAVAILABLE;
}
void MountCoordinator::photoQueued(uint32_t id) {
    if(state_==MountNavState::REQUEST_PHOTO && id) {
        requestId_=id;
        state_=MountNavState::QUEUED;
    }
}
void MountCoordinator::photoSent(uint32_t id,bool accepted) {
    if(state_!=MountNavState::QUEUED || requestId_!=id)return;
    state_=accepted?MountNavState::WAIT_PHOTO:MountNavState::FAILED;
}
void MountCoordinator::linkFailed() {
    if(state_!=MountNavState::IDLE &&
       state_!=MountNavState::STOPPED &&
       state_!=MountNavState::MOTORS_UNAVAILABLE)
        state_=MountNavState::FAILED;
}
const char *MountCoordinator::status() const {
    switch(state_){
        case MountNavState::IDLE:return "IDLE";
        case MountNavState::REQUEST_PHOTO:return "REQUEST_PHOTO";
        case MountNavState::QUEUED:return "PHOTO_QUEUED";
        case MountNavState::WAIT_PHOTO:return "WAIT_PHOTO";
        case MountNavState::WAIT_POINT:return "WAIT_POINT";
        case MountNavState::MOTORS_UNAVAILABLE:return "MOTORS_UNAVAILABLE";
        case MountNavState::FAILED:return "FAILED";
        case MountNavState::STOPPED:return "STOPPED";
    }
    return "INVALID";
}

bool MountCoordinator::calibrateAxis(int axis,int direction,uint32_t ms,
                                    mount_point before,mount_point after){
    return mount_calibrate(&geometry_,axis,direction,ms,before,after)!=0;
}
int MountCoordinator::planPulse(mount_pulse &out) const {
    if(!targetActive_)return MOUNT_PLAN_ERROR;
    mount_point target={targetRa_,targetDec_,0,1};
    return mount_plan(&geometry_,target,&out);
}

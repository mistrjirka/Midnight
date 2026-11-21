#include "goto_hil.h"

/* This adapter runs the exact production GoToLoop and packet parsers.
 * Every motor request is returned to the host simulator as DATA ONLY.
 * Nothing here touches GPIO, Wi-Fi, USB OTG or motor drivers. */
bool GoToHil::mailbox(const uint8_t *packet){
    CameraMountMailbox message={};
    if(!parseMountMailbox(packet,32,message)){
        setError("BAD_MAILBOX");
        return false;
    }
    if(message.commandSequence==0 || message.commandSequence==commandSeq_){
        return true;
    }
    commandSeq_=message.commandSequence;
    awaitingPhoto_=0;
    if(message.command==2){
        loop_.cancel();
        setError("NONE");
        return true;
    }
    if(message.command==1 && message.targetValid){
        if(loop_.start(message.targetRa,message.targetDec,clock_)){
            setError("NONE");
            return true;
        }
    }
    setError("INVALID_TARGET");
    return false;
}
bool GoToHil::pointing(uint32_t id,const uint8_t *packet){
    CameraPointing p={};
    if(!parseCameraPointing(packet,32,p)){
        setError("BAD_POINT_PACKET");
        return false;
    }
    if(id!=awaitingPhoto_ || id==0){
        setError("PHOTO_ID_MISMATCH");
        return false;
    }
    if(!loop_.finishPhoto(id,p,clock_)){
        setError("POINT_REJECTED");
        return false;
    }
    awaitingPhoto_=0;
    setError("NONE");
    return true;
}
HilAction GoToHil::next(){
    loop_.tick(clock_);
    if(loop_.needsPhoto()){
        const uint32_t id=++requestSeq_;
        if(!loop_.beginPhoto(id,clock_)){
            setError("CAMERA_REQUEST_FAILED");
            return {};
        }
        awaitingPhoto_=id;
        return {HilActionType::CAMERA_SHOT,id,{}};
    }
    if(loop_.needsPulse()){
        mount_pulse pulse={};
        if(!loop_.nextPulse(pulse) || !loop_.pulseStarted(clock_)){
            setError("MOTOR_REQUEST_FAILED");
            return {};
        }
        HilAction action={};
        action.type=pulse.axis==0?HilActionType::RA_SLEW:HilActionType::AXIS2_SLEW;
        action.motor=pulse;
        return action;
    }
    return {};
}
bool GoToHil::stopAck(bool accepted){
    if(!loop_.pulseInProgress()){
        setError("NO_MOTOR_ACTIVE");
        return false;
    }
    loop_.pulseStopped(accepted,clock_);
    if(!accepted){
        setError("STOP_UNCONFIRMED");
        return false;
    }
    setError("NONE");
    return true;
}
void GoToHil::advance(uint32_t ms){
    clock_+=ms;
    loop_.tick(clock_);
}
void GoToHil::cancel(){
    loop_.cancel();
    awaitingPhoto_=0;
}
void GoToHil::reset(){
    loop_=GoToLoop{};
    clock_=1000;
    commandSeq_=0;
    requestSeq_=1000;
    awaitingPhoto_=0;
    expectedPointSequence_=0;
    error_="NONE";
}

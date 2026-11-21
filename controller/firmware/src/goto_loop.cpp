#include "goto_loop.h"
extern "C" {
#include "../../../modules/astro/core/fixed_math.h"
}
namespace {
bool good(mount_point p){
    return p.valid && p.ra<3600000u && p.dec>=-900000 && p.dec<=900000;
}
}
bool GoToLoop::start(uint32_t ra,int32_t dec,uint32_t now) {
    if(ra>=3600000u || dec<-900000 || dec>900000) {
        fault(GoToFault::INVALID_TARGET);
        return false;
    }
    model_={};
    current_={};
    before_={};
    target_={ra,dec,0,1};
    pulse_=secondPulse_={};
    secondPending_=secondInProgress_=pairMeasurement_=false;
    stage_=GoToStage::BASELINE;
    phase_=GoToPhase::NEED_PHOTO;
    fault_=GoToFault::NONE;
    startedAt_=now;
    photoAt_=motorAt_=settleAt_=photoId_=lastSequence_=errorE4_=corrections_=0;
    noProgress_=wrongDirection_=0;
    driftEastQ16_=driftNorthQ16_=0;
    driftValid_=false;
    driftRefresh_=false;
    driftRefreshCount_=0;
    return true;
}
void GoToLoop::fault(GoToFault why){
    fault_=why;
    phase_=GoToPhase::FAULT;
}
void GoToLoop::cancel(){
    phase_=GoToPhase::STOPPED;
    fault_=GoToFault::NONE;
}
uint32_t GoToLoop::magnitude(mount_point a,mount_point b) const {
    int32_t x=0,y=0;
    uint32_t angular=0;
    if(!mount_target_bearing(a,b,&x,&y,&angular))return UINT32_MAX;
    return angular;
}
void GoToLoop::tick(uint32_t now) {
    if(phase_==GoToPhase::IDLE || phase_==GoToPhase::STOPPED ||
       phase_==GoToPhase::ARRIVED || phase_==GoToPhase::FAULT)return;
    if(uint32_t(now-startedAt_)>TOTAL_TIMEOUT_MS){
        fault(GoToFault::TOTAL_TIMEOUT);
        return;
    }
    if(phase_==GoToPhase::WAIT_PHOTO &&
       uint32_t(now-photoAt_)>PHOTO_TIMEOUT_MS)
        fault(GoToFault::PHOTO_TIMEOUT);
    if(phase_==GoToPhase::MOVING &&
       uint32_t(now-motorAt_)>pulse_.duration_ms+3000)
        fault(GoToFault::MOTOR_TIMEOUT);
    if(phase_==GoToPhase::SETTLING &&
       int32_t(now-settleAt_)>=0)
        phase_=GoToPhase::NEED_PHOTO;
}
bool GoToLoop::beginPhoto(uint32_t id,uint32_t now) {
    if(phase_!=GoToPhase::NEED_PHOTO || !id)return false;
    photoId_=id;
    photoAt_=now;
    phase_=GoToPhase::WAIT_PHOTO;
    return true;
}
bool GoToLoop::finishPhoto(uint32_t id,const CameraPointing &p,uint32_t now) {
    if(phase_!=GoToPhase::WAIT_PHOTO || !id || id!=photoId_)return false;
    if(!p.valid || p.source!=5 || !p.sequence || p.sequence==lastSequence_ ||
       p.matches<8 || p.fovCdeg==0 || !usableLivePointing(p) ||
       p.ra>=3600000 || p.dec<-900000 || p.dec>900000) {
        fault(GoToFault::BAD_PHOTO);
        return false;
    }
    mount_point measured={p.ra,p.dec,now-p.ageAtReadMs,1};
    if(!good(measured)){
        fault(GoToFault::INVALID_POSITION);
        return false;
    }
    lastSequence_=p.sequence;
    const GoToStage measuredStage=stage_;
    if(measuredStage==GoToStage::BASELINE){
        if(!mount_sync(&model_,measured)){
            fault(GoToFault::INVALID_POSITION);return false;
        }
        current_=measured;
        errorE4_=magnitude(current_,target_);
        if(errorE4_==UINT32_MAX){
            fault(GoToFault::INVALID_POSITION);return false;
        }
        if(errorE4_<=350){
            phase_=GoToPhase::ARRIVED;return true;
        }
        // Estimate sky motion with both motors stopped before commanding
        // either motor. This captures tracking-off sidereal drift as well as
        // small drift from polar misalignment when tracking is on.
        stage_=GoToStage::DRIFT_SAMPLE;
        phase_=GoToPhase::NEED_PHOTO;
        return true;
    }
    if(measuredStage==GoToStage::DRIFT_SAMPLE){
        const uint32_t dt=uint32_t(measured.timestamp_ms-current_.timestamp_ms);
        int32_t east=0,north=0;
        if(dt<1000 || dt>90000 ||
           !mount_displacement(current_,measured,&east,&north)){
            fault(GoToFault::DRIFT_SAMPLE);
            return false;
        }
        const int64_t ve=(int64_t)east*65536/dt;
        const int64_t vn=(int64_t)north*65536/dt;
        // Reject movement larger than 0.5e-4 deg/ms (0.05 deg/s).
        // A human moving the tripod during drift calibration cannot become
        // a plausible tracking model.
        if(ve<-32768 || ve>32768 || vn<-32768 || vn>32768){
            fault(GoToFault::DRIFT_SAMPLE);
            return false;
        }
        driftEastQ16_=(int32_t)ve;
        driftNorthQ16_=(int32_t)vn;
        driftValid_=true;
        current_=measured;
        mount_set_current(&model_,measured);
        errorE4_=magnitude(current_,target_);
        if(driftRefresh_){
            driftRefresh_=false;
            noProgress_=wrongDirection_=0;
            stage_=GoToStage::CORRECT;
        }else{
            stage_=GoToStage::RA_FORWARD;
        }
        phase_=GoToPhase::NEED_MOTOR;
        return prepareNext();
    }
    const uint32_t dt=uint32_t(measured.timestamp_ms-before_.timestamp_ms);
    if(!driftValid_ || dt<100 || dt>120000){
        fault(GoToFault::DRIFT_SAMPLE);
        return false;
    }
    int32_t rawEast=0,rawNorth=0;
    if(!mount_displacement(before_,measured,&rawEast,&rawNorth)){
        fault(GoToFault::INVALID_POSITION);
        return false;
    }
    // Subtract empirically measured tracking drift over the *entire*
    // time between photo captures, not merely over motor-on time.
    // The photos can take seconds, while a pulse may last <1 second.
    int64_t east=(int64_t)rawEast-((int64_t)driftEastQ16_*dt/65536);
    int64_t north=(int64_t)rawNorth-((int64_t)driftNorthQ16_*dt/65536);
    if(east<-30000 || east>30000 || north<-30000 || north>30000){
        fault(GoToFault::INVALID_POSITION);
        return false;
    }
    const uint64_t norm=(uint64_t)(east*east)+(uint64_t)(north*north);
    const uint32_t moved=astro_isqrt64(norm);
    if(moved==UINT32_MAX || moved>30000) {
        fault(GoToFault::INVALID_POSITION);
        return false;
    }
    if(measuredStage!=GoToStage::CORRECT){
        if(!mount_calibrate_vector(&model_,pulse_.axis,pulse_.direction,
                            pulse_.duration_ms,(int32_t)east,(int32_t)north,measured)){
            fault(GoToFault::CALIBRATION);return false;
        }
    }else{
        if(moved<60 && pulse_.duration_ms>=150){
            fault(GoToFault::NO_PROGRESS);return false;
        }
        if(!pairMeasurement_ && moved>=60 &&
           !mount_calibrate_vector(&model_,pulse_.axis,pulse_.direction,
                                   pulse_.duration_ms,(int32_t)east,(int32_t)north,measured)){
            fault(GoToFault::CALIBRATION);return false;
        }
        // A paired move contains two unknown motor contributions, so one
        // combined photo cannot uniquely identify separate axis speeds.
        // Keep the prior calibrated vectors and use the measured error to
        // correct the next pair. Future isolated pulses can refine the fit.
    }
    if(!mount_set_current(&model_,measured)){
        fault(GoToFault::INVALID_POSITION);return false;
    }
    current_=measured;
    uint32_t nextError=magnitude(current_,target_);
    if(nextError==UINT32_MAX){
        fault(GoToFault::INVALID_POSITION);return false;
    }
    if(measuredStage==GoToStage::CORRECT){
        if(nextError>errorE4_+100 && nextError>errorE4_+errorE4_/10)
            wrongDirection_++;
        else wrongDirection_=0;
        if(nextError+60>=errorE4_)noProgress_++;
        else noProgress_=0;
        if(wrongDirection_>=2 || noProgress_>=4){
            // Track-off drift and sky geometry can change after several
            // short moves. Re-measure stationary sky motion instead of
            // instantly aborting on a non-monotonic single-axis step.
            if(++driftRefreshCount_>8){
                fault(GoToFault::NO_PROGRESS);return false;
            }
            driftRefresh_=true;
            noProgress_=wrongDirection_=0;
        }
    }
    errorE4_=nextError;
    if(measuredStage==GoToStage::CORRECT && corrections_ &&
       corrections_%24==0){
        // A long slew changes the local sky frame. A fresh stationary pair
        // keeps the drift calibration representative of current pointing.
        driftRefresh_=true;
    }
    if(driftRefresh_){
        stage_=GoToStage::DRIFT_SAMPLE;
        phase_=GoToPhase::NEED_PHOTO;
        return true;
    }
    if(measuredStage==GoToStage::RA_FORWARD)
        stage_=GoToStage::RA_BACKWARD;
    else if(measuredStage==GoToStage::RA_BACKWARD)
        stage_=GoToStage::SECOND_FORWARD;
    else if(measuredStage==GoToStage::SECOND_FORWARD)
        stage_=GoToStage::SECOND_BACKWARD;
    else if(measuredStage==GoToStage::SECOND_BACKWARD)
        stage_=GoToStage::CORRECT;
    if(errorE4_<=350 && stage_==GoToStage::CORRECT){
        phase_=GoToPhase::ARRIVED;return true;
    }
    phase_=GoToPhase::NEED_MOTOR;
    return prepareNext();
}
bool GoToLoop::prepareNext() {
    if(phase_!=GoToPhase::NEED_MOTOR)return false;
    switch(stage_){
        case GoToStage::RA_FORWARD:pulse_={0,1,CALIBRATION_MS,0};return true;
        case GoToStage::RA_BACKWARD:pulse_={0,-1,CALIBRATION_MS,0};return true;
        case GoToStage::SECOND_FORWARD:pulse_={1,1,CALIBRATION_MS,0};return true;
        case GoToStage::SECOND_BACKWARD:pulse_={1,-1,CALIBRATION_MS,0};return true;
        case GoToStage::CORRECT:{
            if(corrections_>=MAX_CORRECTIONS){
                fault(GoToFault::MAX_STEPS);return false;
            }
            // The camera returns this solve ~1.8s after exposure midpoint.
            // A new movement, 1.2s settling and next 2.2s exposure midpoint
            // puts next verification ~5.2s later, plus motor duration.
            // Estimate 5.7s for planning, correcting sidereal drift when
            // tracking is off (otherwise it can oscillate near target).
            secondPulse_={};
            int result=mount_plan_pair(&model_,target_,
                                       driftEastQ16_,driftNorthQ16_,
                                       5200,&pulse_,&secondPulse_);
            if(result==MOUNT_PLAN_DONE){
                phase_=GoToPhase::ARRIVED;return true;
            }
            if(result!=MOUNT_PLAN_PULSE ||
               pulse_.duration_ms<50 || pulse_.duration_ms>1000 ||
               secondPulse_.duration_ms>1000){
                fault(GoToFault::BAD_AXES);return false;
            }
            pairMeasurement_=secondPulse_.duration_ms>0;
            secondPending_=pairMeasurement_;
            secondInProgress_=false;
            return true;
        }
        default:break;
    }
    return false;
}
bool GoToLoop::nextPulse(mount_pulse &out) const {
    if(phase_!=GoToPhase::NEED_MOTOR)return false;
    out=pulse_;return true;
}
bool GoToLoop::pulseStarted(uint32_t now) {
    if(phase_!=GoToPhase::NEED_MOTOR)return false;
    before_=current_;
    motorAt_=now;
    // Count one correction per paired movement, not per physical pulse.
    if(stage_==GoToStage::CORRECT && !secondInProgress_)corrections_++;
    phase_=GoToPhase::MOVING;
    return true;
}
void GoToLoop::pulseStopped(bool ack,uint32_t now) {
    if(phase_!=GoToPhase::MOVING)return;
    if(!ack){fault(GoToFault::MOTOR_STOP);return;}
    if(secondPending_){
        // An individual motor must stop and acknowledge before the
        // next motor can receive a pulse. Take ONE photo after the pair.
        secondPending_=false;
        secondInProgress_=true;
        pulse_=secondPulse_;
        phase_=GoToPhase::NEED_MOTOR;
        return;
    }
    secondInProgress_=false;
    settleAt_=now+SETTLE_MS;
    phase_=GoToPhase::SETTLING;
}
const char *GoToLoop::status(bool motorReady) const {
    switch(phase_){
        case GoToPhase::IDLE:return "IDLE";
        case GoToPhase::NEED_PHOTO:return "NEED_PHOTO";
        case GoToPhase::WAIT_PHOTO:return "WAIT_PHOTO";
        case GoToPhase::NEED_MOTOR:return motorReady?"NEED_PULSE":"WAIT_MOTORS";
        case GoToPhase::MOVING:return "MOVING";
        case GoToPhase::SETTLING:return "SETTLING";
        case GoToPhase::ARRIVED:return "ARRIVED";
        case GoToPhase::STOPPED:return "STOPPED";
        case GoToPhase::FAULT:return "FAULT";
    }
    return "INVALID";
}

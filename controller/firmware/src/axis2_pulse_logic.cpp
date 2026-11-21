#include "axis2_pulse_logic.h"
bool Axis2PulseLogic::start(int dir,uint32_t ms,uint32_t now){
    if(active_ || (dir!=1 && dir!=-1) || ms<50 || ms>1000 ||
       (hasStopped_ && uint32_t(now-lastStopAt_)<COAST_DEADTIME_MS))
        return false;
    deadline_=now+ms;
    drive_=dir==1?Axis2Drive::FORWARD:Axis2Drive::REVERSE;
    active_=true;
    return true;
}
void Axis2PulseLogic::tick(uint32_t now){
    if(active_ && int32_t(now-deadline_)>=0)stop(now);
}
void Axis2PulseLogic::stop(uint32_t now){
    if(active_){
        lastStopAt_=now;
        hasStopped_=true;
    }
    active_=false;
    drive_=Axis2Drive::COAST;
}

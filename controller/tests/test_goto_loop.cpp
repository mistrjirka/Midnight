#include "../firmware/src/goto_loop.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

struct SimCamera {
    uint32_t seq=0;
    mount_point position={1000000,100000,0,1};
    mount_point target={};
    uint32_t pulseCount=0;
    bool reverseBlocked=false;
    CameraPointing capture(){
        CameraPointing p={};
        p.sequence=++seq;p.ra=position.ra;p.dec=position.dec;
        p.source=5;p.valid=true;p.matches=18;p.fovCdeg=700;
        p.ageAtReadMs=2500;
        return p;
    }
    void pulse(const mount_pulse &action) {
        assert(action.duration_ms>=50 && action.duration_ms<=1000);
        assert(action.axis==0 || action.axis==1);
        assert(action.direction==1 || action.direction==-1);
        int d=action.direction*(int)action.duration_ms;
        if(action.axis==0){
            position.ra=(uint32_t)((int)position.ra+d*4);
        }else{
            // Second axis is diagonal (east + north), not perpendicular to RA.
            position.ra=(uint32_t)((int)position.ra+d*2);
            position.dec+=d*3;
        }
        pulseCount++;
    }
};
static uint32_t now=1000;
static void runPulse(GoToLoop &loop,SimCamera &cam,uint32_t &id) {
    loop.tick(now);
    assert(loop.needsPulse());
    for(int axisPulse=0;axisPulse<2;axisPulse++){
        mount_pulse pulse={};
        assert(loop.nextPulse(pulse));
        assert(loop.pulseStarted(now));
        cam.pulse(pulse);
        now+=pulse.duration_ms;
        loop.pulseStopped(true,now);
        if(loop.phase()==GoToPhase::SETTLING)break;
        assert(loop.needsPulse() && axisPulse==0);
    }
    assert(loop.phase()==GoToPhase::SETTLING);
    loop.tick(now+GoToLoop::SETTLE_MS-1);
    assert(!loop.needsPhoto());
    now+=GoToLoop::SETTLE_MS;
    loop.tick(now);
    assert(loop.needsPhoto());
    assert(loop.beginPhoto(++id,now));
    now+=3000;
    assert(loop.finishPhoto(id,cam.capture(),now));
}
static void successScenario(){
    now=1000;
    GoToLoop loop;
    SimCamera cam;
    uint32_t id=100;
    assert(loop.start(1045000,112500,now));
    assert(loop.needsPhoto());
    assert(loop.beginPhoto(++id,now));
    now+=3000;
    assert(loop.finishPhoto(id,cam.capture(),now));
    assert(loop.stage()==GoToStage::DRIFT_SAMPLE);
    assert(loop.needsPhoto());
    assert(loop.beginPhoto(++id,now));
    now+=3000;
    assert(loop.finishPhoto(id,cam.capture(),now));
    assert(loop.stage()==GoToStage::RA_FORWARD);
    assert(loop.driftMeasured());
    for(int i=0;i<4;i++) runPulse(loop,cam,id);
    assert(loop.stage()==GoToStage::CORRECT);
    assert(loop.calibrated());
    int corrections=0;
    while(loop.phase()!=GoToPhase::ARRIVED && corrections<39){
        runPulse(loop,cam,id);
        ++corrections;
    }
    assert(loop.phase()==GoToPhase::ARRIVED);
    assert(loop.errorE4()<=350);
    assert(loop.steps()==(uint32_t)corrections);
    printf("PASS converged skewed 2-axis mount in %d corrections, %u simulated pulses\n",
           corrections,cam.pulseCount);
}
static void stoppedAndStale(){
    now=1000;
    GoToLoop loop;
    SimCamera cam;
    uint32_t id=2;
    assert(loop.start(1020000,110000,now));
    assert(loop.beginPhoto(id,now));
    CameraPointing old=cam.capture();
    old.source=2;
    assert(!loop.finishPhoto(id,old,now+1000));
    assert(loop.failure()==GoToFault::BAD_PHOTO);
    assert(loop.start(1020000,110000,now));
    assert(loop.beginPhoto(++id,now));
    assert(!loop.finishPhoto(id+1,cam.capture(),now));
    assert(loop.phase()==GoToPhase::WAIT_PHOTO);
    loop.cancel();
    assert(loop.phase()==GoToPhase::STOPPED);
    assert(!loop.beginPhoto(99,now));
    assert(!loop.pulseStarted(now));
    assert(loop.start(1020000,110000,now));
    assert(loop.beginPhoto(++id,now));
    loop.tick(now+GoToLoop::PHOTO_TIMEOUT_MS+1);
    assert(loop.failure()==GoToFault::PHOTO_TIMEOUT);
    assert(loop.start(1020000,110000,now));
    assert(loop.beginPhoto(++id,now));
    assert(loop.finishPhoto(id,cam.capture(),now+1000));
    assert(loop.beginPhoto(++id,now+1000));
    assert(loop.finishPhoto(id,cam.capture(),now+4000));
    assert(loop.pulseStarted(now+5000));
    loop.tick(now+5000+GoToLoop::CALIBRATION_MS+3001);
    assert(loop.failure()==GoToFault::MOTOR_TIMEOUT);
    puts("PASS stale/gallery rejected, mismatched request ignored, STOP, photo and motor timeouts");
}
static void failures(){
    now=1000;
    GoToLoop loop;
    SimCamera cam;
    assert(!loop.start(3600000,0,now));
    assert(loop.failure()==GoToFault::INVALID_TARGET);
    assert(loop.start(3400000,0,now));
    assert(loop.beginPhoto(1,now));
    assert(loop.finishPhoto(1,cam.capture(),now+1000));
    assert(loop.stage()==GoToStage::DRIFT_SAMPLE); // distant targets now permitted
    loop.cancel();
    assert(loop.start(1000000,100000,now));
    assert(loop.beginPhoto(2,now));
    assert(loop.finishPhoto(2,cam.capture(),now+1000));
    assert(loop.phase()==GoToPhase::ARRIVED);
    assert(loop.start(1010000,110000,now));
    assert(loop.beginPhoto(3,now));
    assert(loop.finishPhoto(3,cam.capture(),now+1000));
    assert(loop.beginPhoto(4,now+1000));
    assert(loop.finishPhoto(4,cam.capture(),now+4000));
    assert(loop.pulseStarted(now+5000));
    loop.pulseStopped(false,now+5400);
    assert(loop.failure()==GoToFault::MOTOR_STOP);
    puts("PASS invalid target, unsafe large move, already-arrived, unconfirmed motor stop");
}
int main() {
    successScenario();
    stoppedAndStale();
    failures();
}

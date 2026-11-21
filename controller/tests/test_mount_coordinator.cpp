#include "../firmware/src/mount_coordinator.h"
#include <cassert>
#include <cstdio>
int main() {
    MountCoordinator nav;
    CameraMountMailbox m;
    m.commandSequence=1;m.command=1;m.targetValid=true;
    m.targetRa=1020000;m.targetDec=-3400;
    nav.observeMailbox(m);
    assert(nav.needsPhoto());
    assert(nav.targetRa()==1020000 && nav.targetDec()==-3400);
    nav.photoQueued(123);
    assert(nav.state()==MountNavState::QUEUED);
    nav.photoSent(123,true);
    assert(nav.state()==MountNavState::WAIT_PHOTO);
    nav.observeMailbox(m); // repeated poll cannot reissue
    assert(nav.state()==MountNavState::WAIT_PHOTO);
    m.requestedId=123;m.completedId=123;m.captureState=2;m.pointSequence=9;
    nav.observeMailbox(m);
    assert(nav.state()==MountNavState::WAIT_POINT);
    CameraPointing p={};
    p.valid=true;p.source=2;p.sequence=9;p.matches=15;
    nav.observePoint(p);
    assert(nav.state()==MountNavState::WAIT_POINT); // gallery is not a measurement
    p.source=5;p.sequence=8;
    nav.observePoint(p);
    assert(nav.state()==MountNavState::WAIT_POINT); // old solve
    p.sequence=9;p.ra=1000000;p.dec=10000;
    p.ageAtReadMs=3000;
    nav.observePoint(p);
    assert(nav.state()==MountNavState::MOTORS_UNAVAILABLE);
    assert(nav.lastPhotoRa()==1000000);
    mount_pulse next={};
    assert(nav.planPulse(next)==MOUNT_PLAN_ERROR); // no calibrated motors
    const mount_point a={1000000,10000,0,1};
    const mount_point ra={1002000,10000,0,1};
    const mount_point skew={1001000,11700,0,1};
    assert(nav.calibrateAxis(0,+1,500,a,ra));
    assert(nav.calibrateAxis(1,+1,500,a,skew));
    assert(nav.planPulse(next)==MOUNT_PLAN_PULSE);
    assert(next.duration_ms>=50 && next.duration_ms<=1000);
    // New target command leads to new measurement only after explicit camera action.
    m.commandSequence=2;m.command=1;m.targetRa=1500000;
    nav.observeMailbox(m);
    assert(nav.needsPhoto() && nav.targetRa()==1500000);
    nav.photoQueued(456);nav.photoSent(456,true);
    m.requestedId=456;m.completedId=456;m.captureState=3;
    nav.observeMailbox(m);
    assert(nav.state()==MountNavState::FAILED);
    m.commandSequence=3;m.command=2;
    nav.observeMailbox(m);
    assert(nav.state()==MountNavState::STOPPED && !nav.needsPhoto());
    m.commandSequence=4;m.command=1;
    nav.observeMailbox(m);
    assert(nav.needsPhoto());
    nav.photoQueued(789);
    nav.photoSent(789,false);
    assert(nav.state()==MountNavState::FAILED);
    puts("PASS ESP32 target -> fresh-photo request -> measured point, retries blocked, stop");
}

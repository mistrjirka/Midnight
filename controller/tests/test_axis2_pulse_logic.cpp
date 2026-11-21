#include "../firmware/src/axis2_pulse_logic.h"
#include <cassert>
#include <cstdio>
int main(){
    Axis2PulseLogic h;
    assert(!h.active() && !h.in1() && !h.in2());
    assert(!h.start(0,800,1234));
    assert(!h.start(1,0,1234));
    assert(!h.start(1,1001,1234));
    assert(h.start(+1,800,1234));
    assert(h.in1() && !h.in2());
    assert(!h.start(-1,500,1235)); // no direction changes while moving
    h.tick(2033);assert(h.active());
    h.tick(2034);assert(!h.active() && !h.in1() && !h.in2());
    assert(!h.start(-1,200,2035)); // 20 ms coast before reversing
    assert(h.start(-1,200,2500));
    assert(!h.in1() && h.in2());
    h.stop(2700);
    assert(!h.active() && !h.in1() && !h.in2());
    // millis() wrap-around must not hold the motor forever.
    assert(h.start(+1,900,0xfffffff0u));
    h.tick(883);assert(h.active());
    h.tick(884);assert(!h.active());
    puts("PASS axis2 H-bridge both directions, no short, deadline and emergency coast");
}

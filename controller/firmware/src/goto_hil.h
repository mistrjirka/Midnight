/* Hardware-in-the-loop GoTo tester. Compiled only with MIDNIGHT_HIL_SIMULATION.
 * Consumes actual MNC1/MNT1 camera packets but cannot touch motor I/O.
 * GPL-2.0-or-later.
 */
#pragma once
#include <stdint.h>
#include "goto_loop.h"
#include "mount_mailbox_wire.h"
enum class HilActionType : uint8_t {NONE, CAMERA_SHOT, RA_SLEW, AXIS2_SLEW};
struct HilAction {
    HilActionType type=HilActionType::NONE;
    uint32_t requestId=0;
    mount_pulse motor={};
};
class GoToHil {
public:
    bool mailbox(const uint8_t *packet);
    bool pointing(uint32_t id,const uint8_t *packet);
    HilAction next();
    bool stopAck(bool ok);
    void advance(uint32_t ms);
    void cancel();
    void reset();
    const GoToLoop &loop() const {return loop_;}
    uint32_t clock() const {return clock_;}
    uint32_t expectedPointSequence() const {return expectedPointSequence_;}
    uint32_t waitingRequest() const {return awaitingPhoto_;}
    const char *lastError() const {return error_;}
private:
    void setError(const char *s){error_=s;}
    GoToLoop loop_;
    uint32_t clock_=1000;
    uint32_t commandSeq_=0,requestSeq_=1000,awaitingPhoto_=0;
    uint32_t expectedPointSequence_=0;
    const char *error_="NONE";
};
/* DC motor H-bridge truth table and bounded pulse watchdog.
 * No board-specific dependencies: the same policy is unit-tested on Linux.
 * GPL-2.0-or-later.
 */
#pragma once
#include <stdint.h>
enum class Axis2Drive : uint8_t {COAST=0,FORWARD,REVERSE};
class Axis2PulseLogic {
public:
    bool start(int direction,uint32_t durationMs,uint32_t now);
    void tick(uint32_t now);
    void stop(uint32_t now=0);
    bool active() const {return active_;}
    Axis2Drive drive() const {return drive_;}
    bool in1() const {return drive_==Axis2Drive::FORWARD;}
    bool in2() const {return drive_==Axis2Drive::REVERSE;}
    uint32_t deadline() const {return deadline_;}
private:
    Axis2Drive drive_=Axis2Drive::COAST;
    uint32_t deadline_=0;
    bool active_=false;
    bool hasStopped_=false;
    uint32_t lastStopAt_=0;
    static constexpr uint32_t COAST_DEADTIME_MS=20;
};

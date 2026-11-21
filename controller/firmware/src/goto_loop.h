/* Closed-loop two-axis GoTo. Pure C++ without hardware dependencies.
 * Motor power, stop watchdog and actual I/O belong to the caller.
 * GPL-2.0-or-later. */
#pragma once
#include <stdint.h>
#include "pointing_wire.h"
extern "C" {
#include "../../../modules/astro/mount_model.h"
}
enum class GoToPhase : uint8_t {
    IDLE, NEED_PHOTO, WAIT_PHOTO, NEED_MOTOR, MOVING, SETTLING,
    ARRIVED, STOPPED, FAULT
};
enum class GoToStage : uint8_t {
    BASELINE, DRIFT_SAMPLE, RA_FORWARD, RA_BACKWARD, SECOND_FORWARD,
    SECOND_BACKWARD, CORRECT
};
enum class GoToFault : uint8_t {
    NONE, INVALID_TARGET, INVALID_POSITION, BAD_PHOTO, DRIFT_SAMPLE, PHOTO_TIMEOUT,
    MOTOR_START, MOTOR_STOP, MOTOR_TIMEOUT, CALIBRATION, BAD_AXES,
    TOO_FAR, NO_PROGRESS, MAX_STEPS, TOTAL_TIMEOUT, CONNECTION
};
class GoToLoop {
public:
    static constexpr uint32_t CALIBRATION_MS=800;
    static constexpr uint32_t SETTLE_MS=1200;
    static constexpr uint32_t PHOTO_TIMEOUT_MS=90000;
    static constexpr uint32_t TOTAL_TIMEOUT_MS=3600000;
    static constexpr uint32_t MAX_CORRECTIONS=400;
    bool start(uint32_t targetRa,int32_t targetDec,uint32_t now);
    void cancel();
    void fault(GoToFault why);
    void tick(uint32_t now);
    bool needsPhoto() const {return phase_==GoToPhase::NEED_PHOTO;}
    bool beginPhoto(uint32_t requestId,uint32_t now);
    bool finishPhoto(uint32_t requestId,const CameraPointing &point,uint32_t now);
    bool needsPulse() const {return phase_==GoToPhase::NEED_MOTOR;}
    bool nextPulse(mount_pulse &out) const;
    bool pulseStarted(uint32_t now);
    void pulseStopped(bool confirmed,uint32_t now);
    bool pulseInProgress() const {return phase_==GoToPhase::MOVING;}
    GoToPhase phase() const {return phase_;}
    GoToStage stage() const {return stage_;}
    GoToFault failure() const {return fault_;}
    uint32_t errorE4() const {return errorE4_;}
    uint32_t steps() const {return corrections_;}
    uint32_t photoId() const {return photoId_;}
    uint32_t targetRa() const {return target_.ra;}
    int32_t targetDec() const {return target_.dec;}
    uint32_t lastRa() const {return current_.ra;}
    int32_t lastDec() const {return current_.dec;}
    bool calibrated() const {return model_.axis[0].valid && model_.axis[1].valid;}
    bool driftMeasured() const {return driftValid_;}
    int32_t driftEastQ16() const {return driftEastQ16_;}
    int32_t driftNorthQ16() const {return driftNorthQ16_;}
    const char *status(bool motorsAvailable) const;
private:
    bool prepareNext();
    uint32_t magnitude(mount_point from,mount_point to) const;
    mount_model model_={};
    mount_point current_={};
    mount_point before_={};
    mount_point target_={};
    mount_pulse pulse_={},secondPulse_={};
    bool secondPending_=false,secondInProgress_=false,pairMeasurement_=false;
    GoToPhase phase_=GoToPhase::IDLE;
    GoToStage stage_=GoToStage::BASELINE;
    GoToFault fault_=GoToFault::NONE;
    uint32_t startedAt_=0,photoAt_=0,motorAt_=0,settleAt_=0;
    uint32_t photoId_=0,lastSequence_=0,errorE4_=0,corrections_=0;
    int32_t driftEastQ16_=0,driftNorthQ16_=0;
    bool driftValid_=false,driftRefresh_=false;
    uint8_t driftRefreshCount_=0;
    uint8_t noProgress_=0,wrongDirection_=0;
};

/* Empirical two-axis pointing controller, pure fixed-point C. GPL-2.0-or-later. */
#ifndef ASTRO_MOUNT_MODEL_H
#define ASTRO_MOUNT_MODEL_H
#include <stdint.h>
typedef struct {uint32_t ra; int32_t dec; uint32_t timestamp_ms; int valid;} mount_point;
typedef struct {
    int32_t east_q16, north_q16; /* sky displacement: 1e-4 degree / motor ms, Q16 */
    int valid;
    uint8_t samples;
} mount_axis;
typedef struct {
    mount_point current;
    mount_axis axis[2];
    unsigned consecutive_failures;
} mount_model;
typedef struct {
    int axis; /* 0 = RA, 1 = independent second axis */
    int direction; /* +1 or -1 */
    uint32_t duration_ms; /* bounded pulse. Re-solve after every pulse. */
    uint32_t remaining_e4; /* approximate angular error in 1e-4 degree */
} mount_pulse;
enum {
    MOUNT_PLAN_ERROR=-1, MOUNT_PLAN_DONE=0, MOUNT_PLAN_PULSE=1
};
void mount_reset(mount_model *model);
int mount_sync(mount_model *model, mount_point point);
int mount_set_current(mount_model *model, mount_point point);
int mount_calibrate(mount_model *model, int axis, int direction,
                    uint32_t commanded_ms, mount_point before, mount_point after);
/* Calibrate using a displacement with empirically measured sky drift removed.
 * The vector is in 1e-4 degree of east/north movement caused by the motor
 * alone. Signed movement detects reversed motor polarity automatically. */
int mount_calibrate_vector(mount_model *model,int axis,int direction,
                           uint32_t commanded_ms,int32_t east_e4,int32_t north_e4,
                           mount_point after);
int mount_displacement(mount_point from, mount_point to,
                       int32_t *east_e4, int32_t *north_e4);
/* Great-circle distance and local east/north bearing. Never assumes a
 * home position, encoder, motor axis direction, or perpendicular axes.
 * The vector is capped at 5 degrees so the LOCAL motor calibration remains
 * a reasonable approximation even for a distant target. */
int mount_target_bearing(mount_point current,mount_point target,
                         int32_t *east_e4,int32_t *north_e4,
                         uint32_t *distance_e4);
int mount_plan(const mount_model *model, mount_point target, mount_pulse *out);
/* Compensate expected sky drift until the NEXT photo, not just now.
 * The drift is measured in local tangent-plane east/north Q16 e4deg/ms.
 * This avoids dithering around target when tracking is off. */
int mount_plan_drift(const mount_model *model,mount_point target,
                     int32_t driftEastQ16,int32_t driftNorthQ16,
                     uint32_t nextExposureMs,mount_pulse *out);
/* Solve the non-orthogonal 2x2 system for both axes, then scale the
 * pair to <=1000 ms each. Execute both with a STOP acknowledgement after
 * each, followed by one new plate solve. second.duration_ms==0 when unused. */
int mount_plan_pair(const mount_model *model,mount_point target,
                    int32_t driftEastQ16,int32_t driftNorthQ16,
                    uint32_t nextExposureMs,
                    mount_pulse *first,mount_pulse *second);
#endif

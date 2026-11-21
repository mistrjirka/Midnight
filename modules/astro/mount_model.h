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
int mount_displacement(mount_point from, mount_point to,
                       int32_t *east_e4, int32_t *north_e4);
int mount_plan(const mount_model *model, mount_point target, mount_pulse *out);
#endif

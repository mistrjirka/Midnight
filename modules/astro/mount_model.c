/* Displacements are measured empirically. No assumption that axes are square.
 * Use the same J2000 coordinates and capture-time epoch for each solve.
 * Measurements already include tracking/drift, so do not subtract arbitrary
 * sidereal motion when the primary axis is tracking.
 */
#include "mount_model.h"
#include "core/fixed_math.h"
#include <string.h>
#define MIN_PULSE_MS 50
#define MAX_PULSE_MS 1000
#define MIN_CALIBRATION_E4 60 /* 0.006 deg, avoid fitting plate-solver noise */
#define MAX_CALIBRATION_E4 30000 /* 3 degrees per measured pulse */
#define MAX_SLEW_ERROR_E4 200000 /* 20 degrees: recalibrate for bigger moves */
#define DONE_E4 350 /* ~2.1 arcmin: conservative first-pass stopping tolerance */

static int64_t absolute(int64_t v) {return v < 0 ? -v : v;}
static int valid_point(mount_point p) {
    return p.valid && p.ra<3600000u && p.dec>=-900000 && p.dec<=900000;
}
void mount_reset(mount_model *m) {if(m)memset(m,0,sizeof(*m));}
int mount_sync(mount_model *m,mount_point p) {
    if(!m || !valid_point(p))return 0;
    mount_reset(m);m->current=p;return 1;
}
int mount_set_current(mount_model *m,mount_point p) {
    if(!m || !valid_point(p))return 0;
    m->current=p;return 1;
}
int mount_displacement(mount_point a,mount_point b,int32_t *east,int32_t *north) {
    if(!valid_point(a)||!valid_point(b)||!east||!north)return 0;
    const int64_t avg_dec=((int64_t)a.dec+b.dec)/2;
    int32_t sine=0,cosine=0;
    astro_sincos((int32_t)avg_dec,&sine,&cosine);
    if(cosine < (ASTRO_Q30_ONE/10))return 0; /* too close to celestial pole */
    int32_t dra=(int32_t)b.ra-(int32_t)a.ra;
    if(dra>1800000)dra-=3600000;
    if(dra<-1800000)dra+=3600000;
    *east=(int32_t)((int64_t)dra*cosine/ASTRO_Q30_ONE);
    *north=b.dec-a.dec;
    return 1;
}
int mount_calibrate(mount_model *m,int axis,int direction,uint32_t ms,
                    mount_point before,mount_point after) {
    int32_t east=0,north=0;
    if(!m || axis<0 || axis>1 || (direction!=1 && direction!=-1) ||
       ms<MIN_PULSE_MS || ms>MAX_PULSE_MS ||
       !mount_displacement(before,after,&east,&north))return 0;
    const uint64_t norm=(uint64_t)(int64_t)east*east+(uint64_t)(int64_t)north*north;
    if(norm < (uint64_t)MIN_CALIBRATION_E4*MIN_CALIBRATION_E4 ||
       norm > (uint64_t)MAX_CALIBRATION_E4*MAX_CALIBRATION_E4)return 0;
    int32_t sx=(int32_t)(((int64_t)east*65536*direction)/ms);
    int32_t sy=(int32_t)(((int64_t)north*65536*direction)/ms);
    mount_axis *a=&m->axis[axis];
    if(a->valid) {
        /* Exclude strong reversals/outliers instead of corrupting calibration. */
        int64_t dot=(int64_t)a->east_q16*sx+(int64_t)a->north_q16*sy;
        if(dot<=0)return 0;
        sx=(int32_t)(((int64_t)a->east_q16*3+sx)/4);
        sy=(int32_t)(((int64_t)a->north_q16*3+sy)/4);
    }
    a->east_q16=sx;a->north_q16=sy;a->valid=1;
    if(a->samples<255)a->samples++;
    m->current=after;
    m->consecutive_failures=0;
    return 1;
}
int mount_plan(const mount_model *m,mount_point target,mount_pulse *out) {
    if(!m || !out || !valid_point(target) || !valid_point(m->current))return MOUNT_PLAN_ERROR;
    int32_t dx=0,dy=0;
    if(!mount_displacement(m->current,target,&dx,&dy))return MOUNT_PLAN_ERROR;
    uint64_t e2=(uint64_t)(int64_t)dx*dx+(uint64_t)(int64_t)dy*dy;
    out->remaining_e4=astro_isqrt64(e2);
    if(out->remaining_e4 <= DONE_E4)return MOUNT_PLAN_DONE;
    if(out->remaining_e4>MAX_SLEW_ERROR_E4 || !m->axis[0].valid || !m->axis[1].valid)
        return MOUNT_PLAN_ERROR;
    int64_t a=m->axis[0].east_q16,b=m->axis[1].east_q16;
    int64_t c=m->axis[0].north_q16,d=m->axis[1].north_q16;
    int64_t det=a*d-b*c;
    uint32_t len0=astro_isqrt64((uint64_t)(a*a+c*c));
    uint32_t len1=astro_isqrt64((uint64_t)(b*b+d*d));
    /* Angle check based on sin(theta), independent of axis orientation.
     * The previous determinant-to-product test missed nearly collinear
     * east/west axes when one vector was almost horizontal. */
    if(!det || !len0 || !len1 ||
       absolute(det)*20 < (int64_t)len0*len1)return MOUNT_PLAN_ERROR;
    int64_t t0=((int64_t)dx*d-(int64_t)dy*b)*65536/det;
    int64_t t1=((int64_t)dy*a-(int64_t)dx*c)*65536/det;
    int axis=absolute(t0)>=absolute(t1)?0:1;
    int64_t t=axis? t1:t0;
    if(!t)return MOUNT_PLAN_ERROR;
    uint32_t ms=(uint32_t)(absolute(t)>MAX_PULSE_MS?MAX_PULSE_MS:absolute(t));
    if(ms<MIN_PULSE_MS)ms=MIN_PULSE_MS; /* must still measure real displacement */
    out->axis=axis;out->direction=t>0?1:-1;out->duration_ms=ms;
    return MOUNT_PLAN_PULSE;
}

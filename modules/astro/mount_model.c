/* Displacements are measured empirically. No assumption that axes are square.
 * Use the same J2000 coordinates and capture-time epoch for each solve.
 * The GoTo controller estimates actual tracking drift from TWO stationary
 * exposures and subtracts it before fitting motor-only movement vectors.
 */
#include "mount_model.h"
#include "core/fixed_math.h"
#include <string.h>
#define MIN_PULSE_MS 50
#define MAX_PULSE_MS 1000
#define MIN_CALIBRATION_E4 60 /* 0.006 deg, avoid fitting plate-solver noise */
#define MAX_CALIBRATION_E4 30000 /* 3 degrees per measured pulse */
#define MAX_WAYPOINT_E4 50000  /* cap guidance to a 5 degree local step */
#define DONE_E4 350 /* ~2.1 arcmin: conservative first-pass stopping tolerance */

static int64_t absolute(int64_t v) {return v < 0 ? -v : v;}
/* Positive result means this individual motor pulse gets closer to target.
 * This matters with heavily skewed axes: a large coefficient from the 2x2
 * inverse can move AWAY from the target if issued before the other axis. */
static int64_t reduction(int32_t dx,int32_t dy,int64_t ve,int64_t vn,
                         int direction,uint32_t ms)
{
    const int64_t delta_e=ve*direction*(int64_t)ms/65536;
    const int64_t delta_n=vn*direction*(int64_t)ms/65536;
    const int64_t previous=(int64_t)dx*dx+(int64_t)dy*dy;
    const int64_t remaining_e=(int64_t)dx-delta_e;
    const int64_t remaining_n=(int64_t)dy-delta_n;
    return previous-(remaining_e*remaining_e+remaining_n*remaining_n);
}

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
/* Spherical great-circle bearing from arbitrary solved J2000 RA/Dec.
 * xi is east and eta is north in the tangent plane of CURRENT pointing.
 * A target many tens of degrees away still yields the correct direction.
 * Integer Q30 arithmetic avoids ARMv5 soft-float and 180-degree RA wrap.
 * Exactly antipodal points have no unique bearing and are rejected. */
int mount_target_bearing(mount_point from,mount_point to,int32_t *east,int32_t *north,
                         uint32_t *distance)
{
    if(!valid_point(from)||!valid_point(to)||!east||!north||!distance)return 0;
    int32_t s0,c0,s1,c1,sd,cd;
    astro_sincos(from.dec,&s0,&c0);
    astro_sincos(to.dec,&s1,&c1);
    int32_t dra=(int32_t)to.ra-(int32_t)from.ra;
    astro_sincos(dra,&sd,&cd);
    const int64_t c1cd=(int64_t)c1*cd/ASTRO_Q30_ONE;
    const int64_t xi=(int64_t)c1*sd/ASTRO_Q30_ONE;
    const int64_t eta=(int64_t)c0*s1/ASTRO_Q30_ONE -
                       (int64_t)s0*c1cd/ASTRO_Q30_ONE;
    const int64_t dot=(int64_t)s0*s1/ASTRO_Q30_ONE +
                       (int64_t)c0*c1cd/ASTRO_Q30_ONE;
    const uint64_t r2=(uint64_t)(xi*xi)+(uint64_t)(eta*eta);
    const uint32_t radial=astro_isqrt64(r2);
    int32_t angle=astro_atan2((int64_t)radial,dot);
    if(angle<0)angle=-angle;
    if(angle>1800000)angle=1800000;
    *distance=(uint32_t)angle;
    if(*distance<=DONE_E4){
        *east=*north=0;
        return 1;
    }
    // At or near the antipode, sensor noise can flip the "shortest"
    // direction discontinuously. Require an unambiguous bearing.
    if(radial<10000u)return 0;
    const uint32_t step=*distance>MAX_WAYPOINT_E4?MAX_WAYPOINT_E4:*distance;
    *east=(int32_t)((xi*(int64_t)step)/radial);
    *north=(int32_t)((eta*(int64_t)step)/radial);
    return 1;
}

int mount_calibrate_vector(mount_model *m,int axis,int direction,uint32_t ms,
                           int32_t east,int32_t north,mount_point after) {
    if(!m || !valid_point(after) || axis<0 || axis>1 ||
       (direction!=1 && direction!=-1) || ms<MIN_PULSE_MS ||
       ms>MAX_PULSE_MS)return 0;
    const uint64_t norm=(uint64_t)(int64_t)east*east+(uint64_t)(int64_t)north*north;
    if(norm < (uint64_t)MIN_CALIBRATION_E4*MIN_CALIBRATION_E4 ||
       norm > (uint64_t)MAX_CALIBRATION_E4*MAX_CALIBRATION_E4)return 0;
    /* Both positive and negative basis vectors are legal. Swapped H-bridge
     * motor terminals invert the sign, but do not prevent calibration. */
    int32_t sx=(int32_t)(((int64_t)east*65536*direction)/ms);
    int32_t sy=(int32_t)(((int64_t)north*65536*direction)/ms);
    mount_axis *a=&m->axis[axis];
    if(a->valid){
        /* A genuine polarity reversal between calibration probes signals
         * loose wiring or a broken motor. Do not silently change the model. */
        const int64_t dot=(int64_t)a->east_q16*sx+(int64_t)a->north_q16*sy;
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
int mount_calibrate(mount_model *m,int axis,int direction,uint32_t ms,
                    mount_point before,mount_point after) {
    int32_t east=0,north=0;
    if(!mount_displacement(before,after,&east,&north))return 0;
    return mount_calibrate_vector(m,axis,direction,ms,east,north,after);
}
static int mount_plan_vector(const mount_model *m,int32_t dx,int32_t dy,
                             mount_pulse *out) {
    if(!m || !out || !m->axis[0].valid || !m->axis[1].valid)
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
    const int64_t t[2]={
        ((int64_t)dx*d-(int64_t)dy*b)*65536/det,
        ((int64_t)dy*a-(int64_t)dx*c)*65536/det
    };
    const int64_t ve[2]={a,b},vn[2]={c,d};
    int best_axis=-1,best_dir=0;
    uint32_t best_ms=0;
    int64_t best_improvement=0;
    for(int axis=0;axis<2;axis++) {
        /* First evaluate the signed displacement from full 2x2 inversion. */
        for(int mode=0;mode<2;mode++){
            int64_t signed_duration=t[axis];
            if(mode==1){
                /* Greedy fallback. With non-perpendicular axes, a full
                 * inverse step can increase target error until the other
                 * axis moves. Projection yields a monotonic short pulse. */
                const int64_t dot=(int64_t)dx*ve[axis]+
                                  (int64_t)dy*vn[axis];
                const int64_t norm=ve[axis]*ve[axis]+vn[axis]*vn[axis];
                if(!norm)continue;
                signed_duration=dot*65536/norm;
            }
            if(!signed_duration)continue;
            const int direction=signed_duration>0?1:-1;
            const uint32_t ms=(uint32_t)(
                absolute(signed_duration)>MAX_PULSE_MS
                ? MAX_PULSE_MS
                : absolute(signed_duration)<MIN_PULSE_MS
                  ? MIN_PULSE_MS : absolute(signed_duration));
            const int64_t score=reduction(dx,dy,ve[axis],vn[axis],
                                          direction,ms);
            if(score>best_improvement){
                best_improvement=score;
                best_axis=axis;
                best_dir=direction;
                best_ms=ms;
            }
        }
    }
    if(best_axis<0)return MOUNT_PLAN_ERROR;
    out->axis=best_axis;
    out->direction=best_dir;
    out->duration_ms=best_ms;
    return MOUNT_PLAN_PULSE;
}

int mount_plan_drift(const mount_model *m,mount_point target,
                     int32_t driftE,int32_t driftN,
                     uint32_t nextPhotoMs,mount_pulse *out) {
    if(!m || !out || !valid_point(target) || !valid_point(m->current) ||
       nextPhotoMs>20000)return MOUNT_PLAN_ERROR;
    int32_t dx=0,dy=0;
    if(!mount_target_bearing(m->current,target,&dx,&dy,&out->remaining_e4))
        return MOUNT_PLAN_ERROR;
    if(out->remaining_e4<=DONE_E4)return MOUNT_PLAN_DONE;
    // Subtract the sky drift until the next photo's exposure midpoint.
    // Over a 5-degree waypoint this is only a small offset, but near
    // the target it can be larger than the requested final correction.
    const int64_t shifted_e=(int64_t)dx-(int64_t)driftE*nextPhotoMs/65536;
    const int64_t shifted_n=(int64_t)dy-(int64_t)driftN*nextPhotoMs/65536;
    if(shifted_e<-200000 || shifted_e>200000 ||
       shifted_n<-200000 || shifted_n>200000)return MOUNT_PLAN_ERROR;
    return mount_plan_vector(m,(int32_t)shifted_e,(int32_t)shifted_n,out);
}
int mount_plan(const mount_model *m,mount_point target,mount_pulse *out) {
    return mount_plan_drift(m,target,0,0,0,out);
}

int mount_plan_pair(const mount_model *m,mount_point target,
                    int32_t driftE,int32_t driftN,
                    uint32_t nextExposureMs,mount_pulse *first,mount_pulse *second)
{
    if(!m || !first || !second || !valid_point(target) ||
       !valid_point(m->current) || nextExposureMs>20000)return MOUNT_PLAN_ERROR;
    memset(first,0,sizeof(*first));
    memset(second,0,sizeof(*second));
    int32_t dx=0,dy=0;
    uint32_t remaining=0;
    if(!mount_target_bearing(m->current,target,&dx,&dy,&remaining))
        return MOUNT_PLAN_ERROR;
    first->remaining_e4=second->remaining_e4=remaining;
    if(remaining<=DONE_E4)return MOUNT_PLAN_DONE;
    if(!m->axis[0].valid || !m->axis[1].valid)return MOUNT_PLAN_ERROR;
    const int64_t a=m->axis[0].east_q16,b=m->axis[1].east_q16;
    const int64_t c=m->axis[0].north_q16,d=m->axis[1].north_q16;
    const int64_t det=a*d-b*c;
    const int64_t scale=absolute(a*d)+absolute(b*c);
    const uint32_t len0=astro_isqrt64((uint64_t)(a*a+c*c));
    const uint32_t len1=astro_isqrt64((uint64_t)(b*b+d*d));
    if(!det || !scale || !len0 || !len1 ||
       absolute(det)*20<(int64_t)len0*len1)return MOUNT_PLAN_ERROR;

    int64_t durations[2]={};
    uint32_t estimateMs=nextExposureMs;
    for(int iteration=0;iteration<3;iteration++){
        const int64_t east=(int64_t)dx-(int64_t)driftE*estimateMs/65536;
        const int64_t north=(int64_t)dy-(int64_t)driftN*estimateMs/65536;
        durations[0]=(east*d-north*b)*65536/det;
        durations[1]=(north*a-east*c)*65536/det;
        const int64_t maximum=absolute(durations[0])>absolute(durations[1])
            ?absolute(durations[0]):absolute(durations[1]);
        // Preserve the relative motor contributions when either would
        // exceed the maximum safe pulse duration.
        const int64_t div=maximum>MAX_PULSE_MS?maximum:MAX_PULSE_MS;
        uint32_t next=nextExposureMs;
        for(int axis=0;axis<2;axis++){
            const int64_t ms=absolute(durations[axis])*MAX_PULSE_MS/div;
            if(ms>=MIN_PULSE_MS)next+=(uint32_t)ms;
        }
        estimateMs=next;
    }
    const int64_t maximum=absolute(durations[0])>absolute(durations[1])
        ?absolute(durations[0]):absolute(durations[1]);
    const int64_t div=maximum>MAX_PULSE_MS?maximum:MAX_PULSE_MS;
    mount_pulse proposed[2]={};
    for(int axis=0;axis<2;axis++){
        const int64_t t=durations[axis];
        const int64_t ms=absolute(t)*MAX_PULSE_MS/div;
        if(ms<MIN_PULSE_MS)continue; /* omit unnecessary tiny moves */
        proposed[axis].axis=axis;
        proposed[axis].direction=t>0?1:-1;
        proposed[axis].duration_ms=(uint32_t)ms;
        proposed[axis].remaining_e4=remaining;
    }
    if(!proposed[0].duration_ms && !proposed[1].duration_ms){
        // Motor minimum pulse exceeds the requested fine correction.
        // Treat an error below 3 arcminutes (0.05 degrees) as reached
        // instead of endlessly dithering or faulting near the target.
        if(remaining<=500u)return MOUNT_PLAN_DONE;
        return MOUNT_PLAN_ERROR;
    }
    *first=proposed[0].duration_ms?proposed[0]:proposed[1];
    if(proposed[0].duration_ms && proposed[1].duration_ms)*second=proposed[1];
    return MOUNT_PLAN_PULSE;
}

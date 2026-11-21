#include "pointing_wire.h"
#include <string.h>
namespace {
uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
}
bool parseCameraPointing(const uint8_t *p, size_t n, CameraPointing &out) {
    if (!p || n!=32 || memcmp(p,"MNT1",4)!=0 || p[25]>1 ||
        p[26] || p[27]) return false;
    CameraPointing next={};
    next.sequence=rd32(p+4);
    next.cameraUptimeMs=rd32(p+8);
    next.ageAtReadMs=rd32(p+28)-next.cameraUptimeMs;
    next.ra=rd32(p+12);
    next.dec=(int32_t)rd32(p+16);
    next.fovCdeg=rd16(p+20);
    next.matches=rd16(p+22);
    next.source=p[24];
    next.valid=p[25]==1;
    if (next.valid && (next.sequence==0 || next.ra>=3600000 ||
        next.dec < -900000 || next.dec > 900000 ||
        next.fovCdeg==0 || next.fovCdeg>18000 ||
        next.matches<8 || next.matches>96 ||
        next.source<1 || next.source>5)) return false;
    if (!next.valid && (next.sequence!=0 || next.matches!=0 ||
        next.source!=0)) return false;
    out=next;
    return true;
}
bool usableLivePointing(const CameraPointing &p) {
    /* Normal still-photo solves may need longer card I/O and star matching.
     * Allow up to 65s for slow Canon still-photo solving; Live View stays 10s. */
    const uint32_t limit=p.source==5 ? 65000 : 10000;
    return p.valid && (p.source==1 || p.source==4 || p.source==5) &&
           p.matches>=8 && p.ageAtReadMs<limit;
}

/* Lightweight stable read-only camera telemetry. GPL-2.0-or-later. */
#include "astro_telemetry.h"
#include <string.h>
typedef struct {
    uint32_t sequence, uptime_ms, ra;
    int32_t dec;
    uint16_t fov, matches;
    uint8_t source, valid;
} pointing;
static volatile uint32_t revision=0;
static pointing last={0};
static void le16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void le32(uint8_t *p, uint32_t v) {
    for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));
}
void astro_telemetry_publish(uint32_t uptime_ms, uint32_t ra, int32_t dec,
                             uint16_t fov_cdeg, uint16_t matches, uint8_t source)
{
    if(ra>=3600000U || dec < -900000 || dec > 900000 ||
       fov_cdeg==0 || fov_cdeg>18000 ||
       matches<8 || matches>96 || source<1 || source>5)return;
    revision++; /* odd while a writer updates */
    last.sequence++;
    last.uptime_ms=uptime_ms;
    last.ra=ra;
    last.dec=dec;
    last.fov=fov_cdeg;
    last.matches=matches;
    last.source=source;
    last.valid=1;
    revision++; /* even after complete update */
}
void astro_telemetry_encode(uint8_t out[ASTRO_TELEMETRY_BYTES],uint32_t read_ms)
{
    pointing p;
    uint32_t before,after;
    memset(out,0,ASTRO_TELEMETRY_BYTES);
    le32(out+28,read_ms);
    /* Fixed-length copy with a bounded sequence check avoids a torn PTP
     * frame if the plate solver publishes a new result at the same time. */
    for(int attempt=0;attempt<4;attempt++) {
        before=revision;
        if(before&1U)continue;
        memcpy(&p,&last,sizeof(p));
        after=revision;
        if(before==after && !(after&1U)) {
            memcpy(out,"MNT1",4);
            le32(out+4,p.sequence);
            le32(out+8,p.uptime_ms);
            le32(out+12,p.ra);
            le32(out+16,(uint32_t)p.dec);
            le16(out+20,p.fov);
            le16(out+22,p.matches);
            out[24]=p.source;
            out[25]=p.valid;
            return;
        }
    }
    /* Retry later if writer is active; never invent a position. */
    memcpy(out,"MNT1",4);
}

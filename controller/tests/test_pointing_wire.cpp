#include "../firmware/src/pointing_wire.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static void put16(unsigned char *p, uint16_t v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p, uint32_t v) {for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
int main(){
    uint8_t frame[32]={};std::memcpy(frame,"MNT1",4);
    CameraPointing result={};
    assert(parseCameraPointing(frame,sizeof(frame),result));
    assert(!result.valid);
    put32(frame+4,1);put32(frame+8,1000);put32(frame+12,1234567);
    put32(frame+16,(uint32_t)-450123);put16(frame+20,250);put16(frame+22,12);
    frame[24]=1;frame[25]=1;put32(frame+28,1500);
    assert(parseCameraPointing(frame,32,result));
    assert(result.ra==1234567 && result.dec==-450123 && result.fovCdeg==250 && result.ageAtReadMs==500);
    assert(usableLivePointing(result));
    frame[24]=2;assert(parseCameraPointing(frame,32,result));assert(!usableLivePointing(result));
    frame[24]=3;assert(parseCameraPointing(frame,32,result));assert(!usableLivePointing(result));
    frame[24]=4;assert(parseCameraPointing(frame,32,result));assert(usableLivePointing(result));
    frame[24]=5;assert(parseCameraPointing(frame,32,result));assert(usableLivePointing(result));
    frame[25]=2;assert(!parseCameraPointing(frame,32,result));
    frame[25]=1;put32(frame+12,4000000);assert(!parseCameraPointing(frame,32,result));
    put32(frame+12,1234567);frame[26]=1;assert(!parseCameraPointing(frame,32,result));
    frame[26]=0; assert(!parseCameraPointing(frame,31,result));
    put32(frame+28,12000); // stale for Live View, still valid for a long still exposure
    frame[24]=4;
    assert(parseCameraPointing(frame,32,result));
    assert(!usableLivePointing(result));
    frame[24]=5;
    assert(parseCameraPointing(frame,32,result));
    assert(usableLivePointing(result));
    put32(frame+28,75000); // stale even for a saved-photo measurement
    assert(parseCameraPointing(frame,32,result));
    assert(!usableLivePointing(result));
    puts("PASS: camera telemetry decoding, bounds, sources, malformed frames");
}

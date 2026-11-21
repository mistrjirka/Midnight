#include "mount_mailbox_wire.h"
#include <cstring>
namespace {
uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
}
bool parseMountMailbox(const uint8_t *p,size_t n,CameraMountMailbox &out) {
    if(!p || n!=32 || std::memcmp(p,"MNC1",4)!=0 ||
       p[25] || p[24]>3 || p[26]>2 || p[27]>1)return false;
    CameraMountMailbox next;
    next.commandSequence=read32(p+4);
    next.targetRa=read32(p+8);
    next.targetDec=(int32_t)read32(p+12);
    next.requestedId=read32(p+16);
    next.completedId=read32(p+20);
    next.captureState=p[24];
    next.command=p[26];
    next.targetValid=p[27]==1;
    next.pointSequence=read32(p+28);
    if(next.targetValid && (next.targetRa>=3600000 ||
                           next.targetDec<-900000 || next.targetDec>900000))return false;
    if(next.commandSequence==0 && next.command!=0)return false;
    if(next.command==1 && !next.targetValid)return false;
    if(next.captureState==1 && !next.requestedId)return false;
    if((next.captureState==2 || next.captureState==3) &&
       (!next.completedId || next.completedId!=next.requestedId))return false;
    out=next;
    return true;
}

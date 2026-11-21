#include "ptp_wire.h"
#include <string.h>

namespace {
uint16_t read16(const uint8_t *v) {return (uint16_t)v[0] | ((uint16_t)v[1]<<8);}
uint32_t read32(const uint8_t *v) {
    return (uint32_t)v[0] | ((uint32_t)v[1]<<8) |
           ((uint32_t)v[2]<<16) | ((uint32_t)v[3]<<24);
}
}
void PtpWireReader::reset() {filled_=0;}
bool PtpWireReader::feed(const uint8_t *bytes, size_t n) {
    if((!bytes && n) || n>MAX_FRAME-filled_) {
        reset();
        return false;
    }
    if(n)memcpy(buffer_+filled_,bytes,n);
    filled_+=n;
    return true;
}
PtpWireReader::Result PtpWireReader::next(PtpContainer &out) {
    if(filled_<12)return NEED_MORE;
    const uint32_t frameLength=read32(buffer_);
    const uint16_t type=read16(buffer_+4);
    if(frameLength<12 || frameLength>MAX_FRAME || type<1 || type>4) {
        reset();
        return BAD_FRAME;
    }
    if(filled_<frameLength)return NEED_MORE;
    PtpContainer found;
    found.type=type;
    found.code=read16(buffer_+6);
    found.transaction=read32(buffer_+8);
    found.payloadSize=frameLength-12;
    if(found.payloadSize)memcpy(found.payload,buffer_+12,found.payloadSize);
    const size_t trailing=filled_-frameLength;
    if(trailing)memmove(buffer_,buffer_+frameLength,trailing);
    filled_=trailing;
    out=found;
    return FRAME_READY;
}

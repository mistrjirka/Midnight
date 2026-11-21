#include "../firmware/src/ptp_wire.h"
#include "../firmware/src/pointing_wire.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static void put16(uint8_t *b,uint16_t x){b[0]=x&255;b[1]=x>>8;}
static void put32(uint8_t *b,uint32_t x){for(int i=0;i<4;i++)b[i]=(uint8_t)(x>>(8*i));}
static std::vector<uint8_t> pkt(uint16_t type,uint16_t code,uint32_t txn,
                                 const std::vector<uint8_t> &content={}) {
    std::vector<uint8_t> v(12+content.size());
    put32(v.data(),(uint32_t)v.size());
    put16(v.data()+4,type);put16(v.data()+6,code);put32(v.data()+8,txn);
    if(!content.empty())memcpy(v.data()+12,content.data(),content.size());
    return v;
}
static std::vector<uint8_t> sample() {
    std::vector<uint8_t> bytes(32);
    memcpy(bytes.data(),"MNT1",4);
    put32(bytes.data()+4,1);
    put32(bytes.data()+8,500);
    put32(bytes.data()+12,2350021);
    put32(bytes.data()+16,(uint32_t)-145123);
    put16(bytes.data()+20,340);
    put16(bytes.data()+22,12);
    bytes[24]=1;bytes[25]=1;put32(bytes.data()+28,650);
    return bytes;
}
int main(){
    const auto data=pkt(2,0x9B32,7,sample());
    const auto ack=pkt(3,0x2001,7);
    PtpWireReader reader;PtpContainer frame={};
    assert(reader.feed(data.data(),5));
    assert(reader.next(frame)==PtpWireReader::NEED_MORE);
    assert(reader.feed(data.data()+5,18));
    assert(reader.next(frame)==PtpWireReader::NEED_MORE);
    assert(reader.feed(data.data()+23,data.size()-23));
    assert(reader.next(frame)==PtpWireReader::FRAME_READY);
    assert(frame.type==2 && frame.code==0x9B32 && frame.transaction==7 && frame.payloadSize==32);
    CameraPointing decoded={};
    assert(parseCameraPointing(frame.payload,frame.payloadSize,decoded));
    assert(decoded.ra==2350021 && decoded.dec==-145123 && decoded.ageAtReadMs==150);
    assert(usableLivePointing(decoded));
    assert(reader.feed(ack.data(),ack.size()));
    assert(reader.next(frame)==PtpWireReader::FRAME_READY);
    assert(frame.type==3 && frame.code==0x2001 && frame.transaction==7);
    assert(reader.next(frame)==PtpWireReader::NEED_MORE);

    std::vector<uint8_t> together=data;
    together.insert(together.end(),ack.begin(),ack.end());
    reader.reset();
    assert(reader.feed(together.data(),together.size()));
    assert(reader.next(frame)==PtpWireReader::FRAME_READY && frame.type==2);
    assert(reader.next(frame)==PtpWireReader::FRAME_READY && frame.type==3);
    assert(reader.buffered()==0);
    reader.reset();
    uint8_t invalid[12]={};put32(invalid,0xFFFFFFFFu);put16(invalid+4,2);
    assert(reader.feed(invalid,sizeof invalid));
    assert(reader.next(frame)==PtpWireReader::BAD_FRAME);
    assert(reader.buffered()==0);
    std::vector<uint8_t> huge(257);
    assert(!reader.feed(huge.data(),huge.size()));
    // Recovery: valid packet following a corrupt one.
    assert(reader.feed(data.data(),data.size()));
    assert(reader.next(frame)==PtpWireReader::FRAME_READY);
    puts("PASS: fragmented/coalesced PTP data+response, malformed and recovery");
}

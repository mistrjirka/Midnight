"""Exercise the actual Midnight PTP callback and ESP32 parser together.

Uses fake Canon transport callbacks, not a physical camera or USB hardware.
The camera publisher and ESP32 parser are compiled from production sources.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASTRO = ROOT / "modules" / "astro"
BRIDGE = ROOT / "controller" / "firmware" / "src"

PTP_HEADER = r"""
#pragma once
#include <stdint.h>
struct ptp_handle;
struct ptp_msg {
    uint32_t id, session, transaction, param_count, param[5];
};
struct ptp_context {
    struct ptp_handle *handle;
    int (*send_data)(struct ptp_handle*, void*, int,int,int,int,int);
    void *recv_data;
    int (*send_resp)(struct ptp_handle*,struct ptp_msg*);
    void *get_data_size;
};
#define PTP_RC_OK 0x2001
#define PTP_RC_GeneralError 0x2002
#define PTP_RC_ParameterNotSupported 0x2006
uint32_t ptp_register_handler(uint32_t id,
    int (*handler)(void*,struct ptp_context*,void*,void*),void* priv);
uint32_t get_ms_clock(void);
void DryosDebugMsg(int channel,int level,const char *fmt,...);
"""

CAMERA_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ptp.h"
#include "astro_telemetry.h"
#include "mount_mailbox.h"
int astro_ptp_register(void);
static int (*callback)(void*,struct ptp_context*,void*,void*)=NULL;
static uint32_t opcode=0;
static unsigned char output[96];
static size_t outsize=0;
static int registrations=0;
static uint32_t lastCaptureId=0;
int astro_mount_remote_enqueue(uint32_t id){
    lastCaptureId=id;
    return id==123?2:0;
}
static void put16(unsigned char *p,unsigned value) {p[0]=value&255;p[1]=(value>>8)&255;}
static void put32(unsigned char *p,uint32_t v) {for(int i=0;i<4;i++)p[i]=(v>>(i*8))&255;}
uint32_t get_ms_clock(void){return 175;}
void DryosDebugMsg(int channel,int level,const char *fmt,...){
    (void)channel;(void)level;(void)fmt;
}
uint32_t ptp_register_handler(uint32_t op,
        int (*handler)(void*,struct ptp_context*,void*,void*),void* priv){
    (void)priv;
    registrations++;
    if(op==0x9B32) {opcode=op;callback=handler;}
    return 0;
}
static int send_data(struct ptp_handle *h,void *buffer,int len,int total,
                     int a,int b,int c){
    (void)h;(void)total;(void)a;(void)b;(void)c;
    if(outsize || len!=32) return 1;
    put32(output,12+(uint32_t)len);
    put16(output+4,2);put16(output+6,opcode);put32(output+8,9);
    memcpy(output+12,buffer,(size_t)len);outsize=12+(size_t)len;
    return 0;
}
static int send_resp(struct ptp_handle *h,struct ptp_msg *m){
    (void)h;
    if(m->transaction!=9 || (m->id!=PTP_RC_OK && m->id!=0x2019) ||
       (outsize!=44 && outsize!=0))return 1;
    put32(output+outsize,12);put16(output+outsize+4,3);
    put16(output+outsize+6,m->id);put32(output+outsize+8,m->transaction);
    outsize+=12;
    return 0;
}
int main(void){
    astro_telemetry_publish(100,2350021,-145123,340,12,ASTRO_SOURCE_LIVE);
    if(astro_ptp_register()!=0 || !callback || opcode!=0x9B32 ||
       registrations!=4)return 2;
    union {
        int (*short_abi)(void*,struct ptp_context*,void*,void*);
        int (*actual)(void*,struct ptp_context*,uint32_t,uint32_t,uint32_t,
                      uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    } cb;
    cb.short_abi=callback;
    struct ptp_context context={0};
    context.send_data=send_data;context.send_resp=send_resp;
    if(cb.actual(NULL,&context,0x9B32,1,9,0,0,0,0,0)!=1)return 3;
    if(outsize!=56)return 4;
    fwrite(output,1,outsize,stdout);
    astro_mount_mailbox_target(1234500,-120300);
    outsize=0;opcode=0x9B34;
    if(cb.actual(NULL,&context,0x9B34,1,9,0,0,0,0,0)!=1 ||
       outsize!=56 || memcmp(output+12,"MNC1",4))return 5;
    outsize=0;opcode=0x9B33;
    if(cb.actual(NULL,&context,0x9B33,1,9,123,0,0,0,0)!=1 ||
       outsize!=12 || lastCaptureId!=123 || output[6]!=1 || output[7]!=0x20)return 6;
    outsize=0;
    if(cb.actual(NULL,&context,0x9B33,1,9,999,0,0,0,0)!=1 ||
       outsize!=12 || output[6]!=0x19 || output[7]!=0x20)return 7;
    return 0;
}
"""

ESP_HARNESS = r"""
#include "ptp_wire.h"
#include "pointing_wire.h"
#include <cassert>
#include <cstdio>
int main(){
    unsigned char bytes[100]={};
    const size_t size=fread(bytes,1,sizeof(bytes),stdin);
    if(size!=56)return 3;
    PtpWireReader wire;
    PtpContainer pkt;
    CameraPointing sample={};
    int received=0;
    // Test uneven USB chunk sizes to exercise true reassembly.
    for(size_t cursor=0;cursor<size;){
        size_t n=(cursor%7)+1;
        if(n>size-cursor)n=size-cursor;
        assert(wire.feed(bytes+cursor,n));
        cursor+=n;
        for(;;){
            auto result=wire.next(pkt);
            if(result==PtpWireReader::NEED_MORE)break;
            assert(result==PtpWireReader::FRAME_READY);
            if(++received==1){
                assert(pkt.type==2 && pkt.code==0x9B32 && pkt.transaction==9);
                assert(parseCameraPointing(pkt.payload,pkt.payloadSize,sample));
                assert(sample.ra==2350021 && sample.dec==-145123);
                assert(sample.matches==12 && sample.fovCdeg==340);
                assert(sample.ageAtReadMs==75 && usableLivePointing(sample));
            } else {
                assert(received==2 && pkt.type==3 && pkt.code==0x2001);
                assert(pkt.transaction==9 && pkt.payloadSize==0);
            }
        }
    }
    assert(received==2 && wire.buffered()==0);
    puts("PASS real camera callback to production ESP32 PTP decoder");
}
"""

def test_camera_ptp_callback_to_esp32_decoder():
    with tempfile.TemporaryDirectory(prefix="midnight-ptp-integration-") as tmp:
        folder = pathlib.Path(tmp)
        (folder / "ptp.h").write_text(PTP_HEADER)
        (folder / "mock.c").write_text(CAMERA_HARNESS)
        (folder / "esp.cpp").write_text(ESP_HARNESS)
        camera = folder / "camera_sim"
        receiver = folder / "esp_receiver"
        subprocess.run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            f"-I{folder}", f"-I{ASTRO}", str(folder / "mock.c"),
            str(ASTRO / "astro_ptp.c"), str(ASTRO / "astro_telemetry.c"),
            str(ASTRO / "mount_mailbox.c"),
            "-o", str(camera),
        ], check=True)
        subprocess.run([
            "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            f"-I{BRIDGE}", str(folder / "esp.cpp"),
            str(BRIDGE / "pointing_wire.cpp"),
            str(BRIDGE / "ptp_wire.cpp"), "-o", str(receiver),
        ], check=True)
        emitted = subprocess.run([str(camera)], check=True, capture_output=True)
        assert len(emitted.stdout) == 56
        decoded = subprocess.run(
            [str(receiver)], input=emitted.stdout, check=True, capture_output=True
        )
        assert b"PASS real camera callback" in decoded.stdout

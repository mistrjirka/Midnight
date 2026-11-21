"""Real PTP handler -> actual mount worker action -> telemetry + mailbox.

A simulated Canon shutter and solver replace physical camera I/O. No
synthetic serial POINT injection is used.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MODULE=ROOT/"modules/astro"

PTP=r"""
#pragma once
#include <stdint.h>
struct ptp_handle;
struct ptp_msg {uint32_t id,session,transaction,param_count,param[5];};
struct ptp_context {
 struct ptp_handle *handle;
 int (*send_data)(struct ptp_handle*,void*,int,int,int,int,int);
 void *recv_data;
 int (*send_resp)(struct ptp_handle*,struct ptp_msg*);
 void *get_data_size;
};
#define PTP_RC_OK 0x2001
#define PTP_RC_GeneralError 0x2002
#define PTP_RC_ParameterNotSupported 0x2006
uint32_t ptp_register_handler(uint32_t,
    int (*handler)(void*,struct ptp_context*,void*,void*),void*);
uint32_t get_ms_clock(void);
void DryosDebugMsg(int,int,const char*,...);
"""
STUBS={
"ptp.h":PTP,
"dryos.h":"#pragma once\n#include <stdint.h>\nextern int lv;\nuint32_t get_ms_clock(void);\n",
"notify_box.h":"#pragma once\nvoid NotifyBox(int,const char*,...);\n",
"gui-common.h":"#pragma once\nint gui_menu_shown(void);\nint is_play_or_qr_mode(void);\nint is_movie_mode(void);\n",
"propvalues.h":"#pragma once\nextern int shooting_mode;\n",
"property.h":"#pragma once\n#define SHOOTMODE_M 3\n",
"shoot.h":"#pragma once\n",
}
MOCK=r"""
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "ptp.h"
#include "mount_control.h"
#include "mount_mailbox.h"
#include "astro_telemetry.h"
#include "solve.h"
#include "worker.h"

int lv=0,shooting_mode=3,capture_fail=0;
static int photo_calls=0,queue_calls=0;
static uint8_t out[32];
static int data_calls=0;
static uint32_t response=0;
static int (*handler[3])(void*,struct ptp_context*,void*,void*)={0};
uint32_t get_ms_clock(void){return 1150;}
void DryosDebugMsg(int ch,int lvl,const char*fmt,...){
 (void)ch;(void)lvl;(void)fmt;
}
void NotifyBox(int ms,const char*fmt,...){(void)ms;(void)fmt;}
void astro_set_status(const char *fmt,...){(void)fmt;}
const char *astro_status(void){return "Mount completed";}
int astro_cancel_requested(void){return 0;}
void astro_cancel(void){}
astro_action astro_running_action(void){return ASTRO_ACTION_NONE;}
int gui_menu_shown(void){return 0;}
int is_play_or_qr_mode(void){return 0;}
int is_movie_mode(void){return 0;}
uint8_t *astro_image(void){static uint8_t image[360*240];return image;}
int astro_mount_capture_photo(uint8_t *image,uint32_t *captured){
 photo_calls++; if(capture_fail)return 0;
 image[0]=7;*captured=1000;return 1;
}
astro_solve_status astro_solve_frame(uint8_t *image,uint32_t budget,astro_solve_result *r){
 assert(image[0]==7 && budget>=1000);
 memset(r,0,sizeof(*r));r->valid=1;
 r->sky.ra=1250000;r->sky.dec=-450000;
 r->sky.fov_cdeg=230;r->sky.matched=16;return ASTRO_SOLVE_OK;
}
void astro_solve_release(void){}
const char *astro_solve_status_text(astro_solve_status x){(void)x;return "FAILED";}
void astro_remember_solution(const astro_solve_result *r){(void)r;}
int astro_request(astro_action a){
 assert(a==ASTRO_ACTION_MOUNT_REMOTE_MEASURE);
 queue_calls++;return 1;
}
uint32_t ptp_register_handler(uint32_t op,
      int (*callback)(void*,struct ptp_context*,void*,void*),void *priv) {
 (void)priv;
 int index=op==0x9B32?0:op==0x9B33?1:op==0x9B34?2:-1;
 if(index<0)return 1;
 handler[index]=callback;return 0;
}
static int send_data(struct ptp_handle *h,void*data,int n,int total,
                     int a,int b,int c) {
 (void)h;(void)total;(void)a;(void)b;(void)c;
 assert(n==32);memcpy(out,data,32);data_calls++;return 0;
}
static int send_resp(struct ptp_handle*h,struct ptp_msg*reply) {
 (void)h;response=reply->id;return 0;
}
int astro_ptp_register(void);
static void call(int i,uint32_t id) {
 union {
   int (*short_abi)(void*,struct ptp_context*,void*,void*);
   int (*full)(void*,struct ptp_context*,uint32_t,uint32_t,uint32_t,
               uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
 } cb;
 assert(handler[i]);cb.short_abi=handler[i];
 struct ptp_context c={0};c.send_data=send_data;c.send_resp=send_resp;
 response=0;
 assert(cb.full(NULL,&c,0x9B32+(uint32_t)i,1,7,id,0,0,0,0)==1);
}
static uint32_t value32(const uint8_t*p){
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int main(void){
 assert(astro_ptp_register()==0);
 assert(astro_mount_send_selected_target(1234567,-420000));
 call(2,0);
 assert(response==PTP_RC_OK && !memcmp(out,"MNC1",4));
 assert(value32(out+8)==1234567 && (int32_t)value32(out+12)==-420000);
 call(1,17);
 assert(response==PTP_RC_OK && queue_calls==1 && photo_calls==0);
 call(1,17);
 assert(response==PTP_RC_OK && queue_calls==1);
 call(1,18);
 assert(response==0x2019 && queue_calls==1);
 astro_mount_remote_measure();
 assert(photo_calls==1);
 call(0,0);
 assert(response==PTP_RC_OK && !memcmp(out,"MNT1",4));
 assert(out[24]==ASTRO_SOURCE_STILL && out[25]==1);
 assert(value32(out+12)==1250000);
 call(2,0);
 assert(response==PTP_RC_OK && value32(out+20)==17 && out[24]==2);
 assert(value32(out+28)==1);
 call(1,17);assert(response==PTP_RC_OK && queue_calls==1);
 capture_fail=1;
 call(1,18);
 assert(response==PTP_RC_OK && queue_calls==2);
 astro_mount_remote_measure();
 assert(photo_calls==2);
 call(2,0);
 assert(response==PTP_RC_OK && value32(out+20)==18 && out[24]==3);
 assert(value32(out+28)==0);
 astro_mount_abort_target();
 call(2,0);
 assert(out[26]==ASTRO_MOUNT_CMD_STOP);
 puts("PASS real PTP -> worker -> photo/solve -> mailbox, busy, duplicate, failure, stop");
 return 0;
}
"""

def test_camera_ptp_remote_photo_integration():
 with tempfile.TemporaryDirectory(prefix="midnight-ptp-camera-") as tmp:
  dest=Path(tmp)
  for name,source in STUBS.items():
   (dest/name).write_text(source)
  (dest/"test.c").write_text(MOCK)
  out=dest/"harness"
  sources=["astro_ptp.c","mount_mailbox.c","astro_telemetry.c",
           "mount_control.c","mount_model.c","core/fixed_math.c"]
  subprocess.run([
    "gcc","-std=gnu99","-O2","-Wall","-Wextra","-Werror",
    "-I",str(dest),"-I",str(MODULE),str(dest/"test.c"),
    *[str(MODULE/name) for name in sources],"-o",str(out)
  ],check=True)
  result=subprocess.run([str(out)],capture_output=True,check=True)
  assert b"PASS real PTP" in result.stdout

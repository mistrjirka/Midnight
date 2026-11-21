"""Production camera mailbox and ESP32 decoder, including duplicate capture IDs."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
CAM=ROOT/"modules/astro"
ESP=ROOT/"controller/firmware/src"
HOST=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mount_mailbox.h"
static uint32_t u32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void){
 uint8_t data[32];astro_mount_mailbox_encode(data,0);
 assert(!memcmp(data,"MNC1",4));assert(u32(data+4)==0);
 assert(!astro_mount_mailbox_target(3600000,0));
 assert(!astro_mount_mailbox_target(1,900001));
 assert(astro_mount_mailbox_target(1020304,-450005));
 assert(astro_mount_mailbox_begin(111)==2);
 assert(astro_mount_mailbox_begin(111)==1);
 assert(!astro_mount_mailbox_begin(112));
 astro_mount_mailbox_encode(data,0);
 assert(u32(data+4)==1 && u32(data+8)==1020304);
 assert((int32_t)u32(data+12)==-450005 && data[24]==1 && data[26]==1);
 assert(u32(data+16)==111 && !u32(data+20));
 astro_mount_mailbox_complete(100,1,11);
 astro_mount_mailbox_complete(111,1,7);
 astro_mount_mailbox_encode(data,0);
 assert(data[24]==2 && u32(data+20)==111 && u32(data+28)==7);
 assert(astro_mount_mailbox_begin(111)==1);
 assert(astro_mount_mailbox_begin(112)==2);
 astro_mount_mailbox_complete(112,0,500);
 astro_mount_mailbox_stop();
 astro_mount_mailbox_encode(data,0);
 assert(data[24]==3 && u32(data+20)==112 && u32(data+28)==0);
 assert(data[26]==2 && u32(data+4)==2);
 astro_mount_progress feedback={0};
 assert(!astro_mount_mailbox_progress(1,3,990,2,1));
 assert(!astro_mount_mailbox_progress(2,9,990,2,1));
 assert(astro_mount_mailbox_progress(2,3,12345,8,4));
 astro_mount_mailbox_read_progress(&feedback);
 assert(feedback.valid && feedback.phase==3 && feedback.error_e4==12345);
 assert(feedback.steps==8 && feedback.stage==4);
 assert(astro_mount_mailbox_progress(2,3,1555,18,6));
 astro_mount_mailbox_read_progress(&feedback);
 assert(feedback.stage==6);
 assert(!astro_mount_mailbox_progress(2,3,1555,18,7));
 assert(astro_mount_mailbox_progress(2,0x103,12345,8,4));
 astro_mount_mailbox_read_progress(&feedback);
 assert(feedback.motor_ready==1);

 fwrite(data,1,32,stdout);
}
"""
DEVICE=r"""
#include "mount_mailbox_wire.h"
#include <cstdio>
#include <cassert>
int main(){
 unsigned char data[32]={};
 assert(fread(data,1,32,stdin)==32);
 CameraMountMailbox out;
 assert(parseMountMailbox(data,32,out));
 assert(out.commandSequence==2 && out.command==2);
 assert(out.targetRa==1020304 && out.targetDec==-450005);
 assert(out.requestedId==112 && out.completedId==112);
 assert(out.captureState==3 && out.pointSequence==0);
 data[24]=9;assert(!parseMountMailbox(data,32,out));
 puts("PASS mount mailbox end-to-end, duplicate requests, busy, success/failure, abort");
}
"""
def test_mailbox_wire_roundtrip():
 with tempfile.TemporaryDirectory(prefix="midnight-mailbox-") as temp:
  tmp=Path(temp)
  (tmp/"cam.c").write_text(HOST)
  (tmp/"esp.cpp").write_text(DEVICE)
  subprocess.run(["gcc","-std=c99","-O2","-Wall","-Wextra","-Werror",
      "-I",str(CAM),str(tmp/"cam.c"),str(CAM/"mount_mailbox.c"),
      "-o",str(tmp/"cam")],check=True)
  subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
      "-I",str(ESP),str(tmp/"esp.cpp"),str(ESP/"mount_mailbox_wire.cpp"),
      "-o",str(tmp/"esp")],check=True)
  created=subprocess.run([str(tmp/"cam")],capture_output=True,check=True)
  assert len(created.stdout)==32
  done=subprocess.run([str(tmp/"esp")],input=created.stdout,capture_output=True,check=True)
  assert b"PASS" in done.stdout

"""Production embedded two-axis pointing and non-orthogonal movement model."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
MO=ROOT/"modules/astro"
SOURCE=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "mount_model.h"
static mount_point p(int ra,int dec){mount_point x={(uint32_t)ra,dec,0,1};return x;}
int main(void){
 mount_model m;mount_reset(&m);
 mount_point start=p(800000,350000);
 assert(mount_sync(&m,start));
 /* RA wraps across 359->1. */
 int32_t ex=0,ey=0;
 assert(mount_displacement(p(3599000,0),p(1000,0),&ex,&ey));
 assert(ex>=1995 && ex<=2005 && ey==0);
 /* One axis goes east, second runs 30 deg NE instead of perpendicular. */
 mount_point ra=p(802000,350000);
 mount_point second=p(801000,351700);
 assert(mount_calibrate(&m,0,+1,500,start,ra));
 assert(mount_calibrate(&m,1,+1,500,start,second));
 mount_pulse cmd;
 mount_point target=p(810000,357000);
 assert(mount_plan(&m,target,&cmd)==MOUNT_PLAN_PULSE);
 assert(cmd.axis==0 || cmd.axis==1);
 assert(cmd.duration_ms>=50 && cmd.duration_ms<=1000);
 assert(cmd.direction==1);
 /* Reverse axis directions are supported. */
 assert(mount_plan(&m,p(797000,347000),&cmd)==MOUNT_PLAN_PULSE);
 assert(cmd.direction==-1);
 assert(mount_plan(&m,ra,&cmd)==MOUNT_PLAN_PULSE);
 assert(mount_set_current(&m,ra));
 assert(mount_plan(&m,ra,&cmd)==MOUNT_PLAN_DONE);
 /* Invalid calibration measurement does not corrupt the axis. */
 int32_t old=m.axis[0].east_q16;
 assert(!mount_calibrate(&m,0,+1,500,ra,ra));
 assert(m.axis[0].east_q16==old);
 /* Full-sky bearings: the camera may point anywhere before a blind solve.
  * A target across RA=0 and a target 100 degrees away must remain valid. */
 uint32_t angular=0;
 assert(mount_target_bearing(p(3590000,350000),p(10000,350000),
                             &ex,&ey,&angular));
 assert(ex>0 && angular>10000 && angular<20000);
 assert(mount_target_bearing(p(800000,350000),p(1800000,500000),
                             &ex,&ey,&angular));
 assert(angular>500000 && angular<1200000);
 assert(ex!=0 || ey!=0);
 /* Exactly antipodal means the bearing is ambiguous: abort safely. */
 assert(!mount_target_bearing(p(0,0),p(1800000,0),&ex,&ey,&angular));
 /* Arbitrary axis polarity is learned from measured movement. */
 mount_reset(&m);
 assert(mount_sync(&m,start));
 assert(mount_calibrate_vector(&m,0,+1,500,-1300,-300,ra));
 assert(m.axis[0].east_q16<0 && m.axis[0].north_q16<0);
 assert(mount_calibrate_vector(&m,0,-1,500,+1300,+300,ra));
 assert(m.axis[0].samples==2);
 /* Nearly parallel axes: refuse movement. */
 m.axis[1]=m.axis[0];
 assert(mount_plan(&m,target,&cmd)==MOUNT_PLAN_ERROR);
 /* A near-parallel second axis still fails if first axis points east.
  * det/(a*d) alone would incorrectly accept it. */
 m.axis[0].north_q16=0;
 m.axis[1].east_q16=m.axis[0].east_q16;
 m.axis[1].north_q16=m.axis[0].east_q16/10000;
 assert(mount_plan(&m,target,&cmd)==MOUNT_PLAN_ERROR);
 mount_reset(&m);
 assert(mount_plan(&m,target,&cmd)==MOUNT_PLAN_ERROR);
 /* Near pole: the RA coordinate is not a stable tangent-plane basis. */
 assert(!mount_displacement(p(0,899999),p(5000,899999),&ex,&ey));
 /* Simulated skewed two-axis mount: calibrate each axis independently.
  * Re-observe after every bounded movement and iteratively correct. */
 mount_reset(&m);
 start=p(800000,0);
 assert(mount_sync(&m,start));
 assert(mount_calibrate(&m,0,+1,500,start,p(802000,0)));
 assert(mount_calibrate(&m,1,+1,500,start,p(801000,1800)));
 mount_set_current(&m,start);
 int steps=0,reached=0;
 mount_point goal=p(816000,12700),location=start;
 for(steps=0;steps<60;steps++){
   int plan=mount_plan(&m,goal,&cmd);
   if(plan==MOUNT_PLAN_DONE){reached=1;break;}
   assert(plan==MOUNT_PLAN_PULSE);
   int sign=cmd.direction;
   if(cmd.axis==0){
       location.ra=(uint32_t)((int)location.ra+sign*(int)cmd.duration_ms*4);
   }else{
       location.ra=(uint32_t)((int)location.ra+sign*(int)cmd.duration_ms*2);
       location.dec+=sign*(int)cmd.duration_ms*36/10;
   }
   assert(mount_set_current(&m,location));
 }
 assert(reached && steps<60);
 puts("PASS RA wrap, skew axes, calibration, negative direction, singularity, convergence");
}
"""
def test_mount_model():
 with tempfile.TemporaryDirectory(prefix="mount-model-") as temp:
  src=Path(temp)/"test.c"
  src.write_text(SOURCE)
  out=Path(temp)/"mount-test"
  subprocess.run(["gcc","-std=c99","-O2","-Wall","-Wextra","-Werror",
                  "-I",str(MO),str(src),str(MO/"mount_model.c"),
                  str(MO/"core/fixed_math.c"),"-o",str(out)],check=True)
  subprocess.run([str(out)],check=True,capture_output=True)

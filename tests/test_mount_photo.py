"""Camera still-shot measurement must not use Live View and must restore settings."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "modules" / "astro"
STUBS = {
    "dryos.h": """
#pragma once
#include <stdint.h>
extern int lv;
uint32_t get_ms_clock(void);
void msleep(int ms);
""",
    "lens.h": """
#pragma once
struct lens_info_t {
 int raw_iso, raw_shutter, raw_aperture, lens_exists,raw_aperture_min;
};
extern struct lens_info_t lens_info;
struct card_info{int file_number;};
struct card_info *get_shooting_card(void);
#define SHUTTER_1s 56
#define SHUTTER_2s 48
#define AF_DISABLE 0
int get_max_analog_iso(void);
int lens_set_rawiso(int);
int lens_set_rawshutter(int);
int lens_set_rawaperture(int);
int lens_take_picture(int,int);
""",
    "property.h": """
#pragma once
#define SHOOTMODE_M 3
""",
    "propvalues.h": """
#pragma once
extern int shooting_mode;
""",
    "gui-common.h": """
#pragma once
int is_play_or_qr_mode(void);
int is_movie_mode(void);
int is_pure_play_photo_mode(void);
void enter_play_mode(void);
void exit_play_qr_mode(void);
""",
    "shoot.h": "#pragma once\n",
}
C_MAIN = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "lens.h"
#include "gui-common.h"
#include "mount_photo.h"
#include "state.h"
int lv=0,shooting_mode=3,astro_cfg_mount_exposure_sec=2;
struct lens_info_t lens_info={80,80,45,1,30};
static struct card_info card={11};
static int shot=0,fail_shot=0;
static int setiso=0,setshutter=0,setaperture=0;
static int restored=0,opened=0,closed=0;
static int cancel=0,playmode=0,advance_clock=0;
uint32_t get_ms_clock(void){return 1200+advance_clock;}
void msleep(int ms){advance_clock+=ms;}
struct card_info *get_shooting_card(void){return &card;}
int get_max_analog_iso(void){return 112;}
int lens_set_rawiso(int v){lens_info.raw_iso=v;setiso++;return 1;}
int lens_set_rawshutter(int v){lens_info.raw_shutter=v;setshutter++;return 1;}
int lens_set_rawaperture(int v){lens_info.raw_aperture=v;setaperture++;return 1;}
int lens_take_picture(int wait,int af){
 assert(wait==1 && af==0);
 assert(lens_info.raw_iso==112);
 assert(lens_info.raw_aperture==30);
 assert(lens_info.raw_shutter==(astro_cfg_mount_exposure_sec==2?48:56));
 shot++;
 if(!fail_shot)card.file_number++;
 return 0;
}
int astro_cancel_requested(void){return cancel;}
int is_play_or_qr_mode(void){return playmode;}
int is_movie_mode(void){return 0;}
int is_pure_play_photo_mode(void){return playmode;}
void enter_play_mode(void){opened++;playmode=1;}
void exit_play_qr_mode(void){closed++;playmode=0;}
uint32_t astro_playback_signature(void){return playmode?9234:0;}
int astro_capture_display(uint8_t *image){if(!playmode)return 0;image[0]=42;return 1;}
void astro_set_status(const char *fmt,...){(void)fmt;restored++;}
int main(void){
 uint8_t frame[86400]={};uint32_t at=0;
 assert(astro_mount_capture_photo(frame,&at)==1);
 assert(frame[0]==42 && at>1200);
 assert(shot==1 && opened==1 && closed==1);
 assert(lens_info.raw_iso==80 && lens_info.raw_shutter==80);
 assert(lens_info.raw_aperture==45);
 assert(setiso==2 && setshutter==2 && setaperture==2);
 lv=1;
 assert(!astro_mount_capture_photo(frame,&at));
 assert(shot==1);lv=0;
 fail_shot=1;
 assert(!astro_mount_capture_photo(frame,&at));
 assert(shot==2 && opened==1 && closed==1);
 assert(lens_info.raw_iso==80 && lens_info.raw_shutter==80);
 assert(lens_info.raw_aperture==45);
 puts("PASS: still photo and latest-playback capture, restores camera settings, no LV");
}
"""


def test_still_photo_setting_restoration_and_no_live_view():
    with tempfile.TemporaryDirectory(prefix="mount-photo-") as folder:
        root = Path(folder)
        for name, source in STUBS.items():
            (root / name).write_text(source)
        (root / "test.c").write_text(C_MAIN)
        binary = root / "mount_photo_test"
        subprocess.run([
            "gcc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(root), "-I", str(MODULE),
            str(root / "test.c"), str(MODULE / "mount_photo.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], capture_output=True, check=True)

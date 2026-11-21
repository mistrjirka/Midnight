/* The camera is the pointing sensor. The mount geometry is learned from real
 * plate-solved photos, not from assumed perpendicular motor axes.
 */
#include <dryos.h>
#include <notify_box.h>
#include "mount_control.h"
#include "mount_photo.h"
#include "mount_model.h"
#include "astro_telemetry.h"
#include "solve.h"
#include "solve_actions.h"
#include "state.h"
#include "worker.h"

#define PHOTO_BUDGET_MS 45000
static mount_model calibrated;
static mount_point previous;
static int has_reference=0;
static int has_delta=0;
static int32_t last_east=0,last_north=0;
static void result_to_screen(void) {
    NotifyBox(5500,"Mount: %s",astro_status());
}

static int measure(mount_point *position)
{
    uint32_t captured_at=0;
    astro_solve_result solved={0};
    if(!astro_mount_capture_photo(astro_image(),&captured_at))return 0;
    astro_set_status("Mount: solving saved photo");
    astro_solve_status status=astro_solve_frame(astro_image(),PHOTO_BUDGET_MS,&solved);
    astro_solve_release();
    if(status!=ASTRO_SOLVE_OK || solved.sky.matched<8){
        astro_set_status("Mount: %s",astro_solve_status_text(status));
        return 0;
    }
    if(astro_cancel_requested())return 0;
    position->ra=solved.sky.ra;
    position->dec=solved.sky.dec;
    position->timestamp_ms=captured_at;
    position->valid=1;
    astro_remember_solution(&solved);
    astro_telemetry_publish(captured_at,position->ra,position->dec,
         (uint16_t)solved.sky.fov_cdeg,(uint16_t)solved.sky.matched,ASTRO_SOURCE_STILL);
    return 1;
}
void astro_mount_sync_photo(void)
{
    mount_point p={0};
    if(!measure(&p)){result_to_screen();return;}
    if(!mount_sync(&calibrated,p))return;
    previous=p;has_reference=1;has_delta=0;
    astro_set_status("Mount sync RA %lu.%04lu Dec %c%lu.%04lu",
       (unsigned long)(p.ra/10000),(unsigned long)(p.ra%10000),
       p.dec<0?'-':'+',
       (unsigned long)((p.dec<0?-p.dec:p.dec)/10000),
       (unsigned long)((p.dec<0?-p.dec:p.dec)%10000));
    result_to_screen();
}
void astro_mount_measure_photo(void)
{
    mount_point p={0};
    if(!measure(&p)){result_to_screen();return;}
    if(has_reference){
        int32_t x,y;
        if(mount_displacement(previous,p,&x,&y)){
            has_delta=1;last_east=x;last_north=y;
            /* Measured east and north, not assumed RA/DEC motor axes. */
            astro_set_status("Moved E%c%lu.%04lu N%c%lu.%04lu",
                x<0?'-':'+',(unsigned long)((x<0?-x:x)/10000),
                (unsigned long)((x<0?-x:x)%10000),
                y<0?'-':'+',(unsigned long)((y<0?-y:y)/10000),
                (unsigned long)((y<0?-y:y)%10000));
        } else astro_set_status("Mount: near pole, measure invalid");
    } else astro_set_status("Mount: position measured, not synced");
    previous=p;has_reference=1;
    mount_set_current(&calibrated,p);
    result_to_screen();
}
void astro_mount_status_menu(char *out,int size)
{
    if(!has_reference){snprintf(out,size,"Not synced");return;}
    snprintf(out,size,"RA %lu.%02lu Dec %c%lu.%02lu",
      (unsigned long)(previous.ra/10000),
      (unsigned long)(previous.ra%10000/100),
      previous.dec<0?'-':'+',
      (unsigned long)((previous.dec<0?-previous.dec:previous.dec)/10000),
      (unsigned long)((previous.dec<0?-previous.dec:previous.dec)%10000/100));
}
void astro_mount_delta_menu(char *out,int size)
{
    if(!has_delta){snprintf(out,size,"--");return;}
    int32_t x=last_east,y=last_north;
    snprintf(out,size,"E%c%lu.%02lu N%c%lu.%02lu",
       x<0?'-':'+',(unsigned long)((x<0?-x:x)/10000),
       (unsigned long)((x<0?-x:x)%10000/100),
       y<0?'-':'+',(unsigned long)((y<0?-y:y)/10000),
       (unsigned long)((y<0?-y:y)%10000/100));
}
void astro_mount_goto_unavailable(void)
{
    astro_set_status("GoTo disabled: motors not calibrated");
    NotifyBox(4500,"Mount: connect/calibrate both motors first");
}

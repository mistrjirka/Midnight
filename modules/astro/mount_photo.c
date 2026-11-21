/* Real still exposure, manual mode, auto ISO/aperture restoration.
 * Intentionally does NOT capture or amplify Live View.
 */
#include <dryos.h>
#include <lens.h>
#include <property.h>
#include <propvalues.h>
#include <gui-common.h>
#include <shoot.h>
#include "capture.h"
#include "state.h"
#include "worker.h"
#include "mount_photo.h"

#define PLAYBACK_WAIT_MS 3500
#define PLAYBACK_STEP_MS 150

int astro_mount_capture_photo(uint8_t *image,uint32_t *captured_ms)
{
    int old_iso,old_shutter,old_aperture,has_lens;
    int configured=0,shot_ok=0,result=0;
    uint32_t before_ms;
    int file_before;
    struct card_info *card;

    if(!image || !captured_ms)return 0;
    if(lv || is_play_or_qr_mode() || is_movie_mode()){
        astro_set_status("Mount: close Live View/playback first");
        return 0;
    }
    if(shooting_mode!=SHOOTMODE_M){
        astro_set_status("Mount: use M photo mode");
        return 0;
    }
    card=get_shooting_card();
    if(!card){
        astro_set_status("Mount: no shooting card");
        return 0;
    }
    old_iso=lens_info.raw_iso;
    old_shutter=lens_info.raw_shutter;
    old_aperture=lens_info.raw_aperture;
    has_lens=lens_info.lens_exists && lens_info.raw_aperture_min;
    file_before=card->file_number;
    if(astro_cancel_requested())return 0;

    /* Use the highest analogue ISO, not extreme digital amplification.
     * With manual lenses, only the user's physical aperture ring can open it. */
    astro_set_status("Mount: setting %ds still",astro_cfg_mount_exposure_sec);
    configured=1;
    if(!lens_set_rawiso(get_max_analog_iso()) ||
       !lens_set_rawshutter(astro_cfg_mount_exposure_sec==1?SHUTTER_1s:SHUTTER_2s) ||
       (has_lens && !lens_set_rawaperture(lens_info.raw_aperture_min))){
        astro_set_status("Mount: camera rejected exposure settings");
        goto restore;
    }
    if(astro_cancel_requested())goto restore;
    before_ms=(uint32_t)get_ms_clock();
    astro_set_status("Mount: exposing %ds",astro_cfg_mount_exposure_sec);
    /* Blocking until the photo finishes; no AF refocus. */
    (void)lens_take_picture(1,AF_DISABLE);
    /* A successful trigger must have actually saved a new file. */
    for(int elapsed=0;elapsed<3000 && card->file_number==file_before;elapsed+=100)
        msleep(100);
    shot_ok=(card->file_number!=file_before);
    if(!shot_ok){
        astro_set_status("Mount: no new saved photo");
        goto restore;
    }
    *captured_ms=before_ms+(uint32_t)astro_cfg_mount_exposure_sec*500u;
restore:
    /* Always undo temporary exposure settings, even after failures/Q. */
    if(configured){
        if(has_lens)lens_set_rawaperture(old_aperture);
        lens_set_rawshutter(old_shutter);
        lens_set_rawiso(old_iso);
    }
    if(!shot_ok || astro_cancel_requested())return 0;
    astro_set_status("Mount: loading the saved photo");
    enter_play_mode();
    if(!is_pure_play_photo_mode()){
        astro_set_status("Mount: playback did not open");
        goto leave_playback;
    }
    /* Canon may need to load the JPEG/CR2 preview into its YUV buffer. */
    for(int elapsed=0;elapsed<PLAYBACK_WAIT_MS;elapsed+=PLAYBACK_STEP_MS){
        if(astro_cancel_requested())break;
        if(is_pure_play_photo_mode()){
            uint32_t signature=astro_playback_signature();
            if(signature && astro_capture_display(image)){
                msleep(PLAYBACK_STEP_MS);
                if(signature==astro_playback_signature()){
                    result=1;
                    break;
                }
            }
        }
        msleep(PLAYBACK_STEP_MS);
    }
    if(!result)astro_set_status("Mount: no stable saved-photo preview");
leave_playback:
    exit_play_qr_mode();
    return result && !astro_cancel_requested();
}

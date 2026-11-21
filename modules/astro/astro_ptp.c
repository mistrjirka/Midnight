/* Midnight Canon USB PTP mailbox and shutter request transport.
 * 0x9B32: read-only verified pointing
 * 0x9B33: enqueue a new saved-photo measurement (never block PTP)
 * 0x9B34: read-only target, capture request status and sequence
 * No camera opcode can move the mount. GPL-2.0-or-later.
 */
#include <stdint.h>
#include <ptp.h>
#include "astro_telemetry.h"
#include "mount_mailbox.h"
#include "mount_control.h"
#include "astro_ptp.h"

#define PTP_RC_DEVICE_BUSY 0x2019u

static int camera_mount_ptp(
    void *priv,struct ptp_context *context,
    uint32_t opcode,uint32_t session,uint32_t transaction,
    uint32_t p1,uint32_t p2,uint32_t p3,uint32_t p4,uint32_t p5)
{
    (void)priv;(void)p2;(void)p3;(void)p4;(void)p5;
    uint8_t frame[ASTRO_TELEMETRY_BYTES];
    struct ptp_msg resp={
        .id=PTP_RC_OK,
        .session=session,
        .transaction=transaction,
        .param_count=0
    };
    if(opcode==ASTRO_MOUNT_CAPTURE_OPCODE){
        /* Dispatch an ordinary worker action, not a shutter call from PTP.
         * Retries of the same request ID are idempotent. */
        const int queued=astro_mount_remote_enqueue(p1);
        if(!queued)resp.id=PTP_RC_DEVICE_BUSY;
        else DryosDebugMsg(0,15,"ASTRO_PTP capture request=%u %s",
                           p1,queued==2?"queued":"duplicate");
    } else if(opcode==ASTRO_TELEMETRY_PTP_OPCODE ||
              opcode==ASTRO_MOUNT_STATUS_OPCODE){
        const uint32_t now=(uint32_t)get_ms_clock();
        if(opcode==ASTRO_TELEMETRY_PTP_OPCODE)
            astro_telemetry_encode(frame,now);
        else
            astro_mount_mailbox_encode(frame,astro_telemetry_current_sequence());

        static uint32_t last_log=0,last_sequence=0;
        if(opcode==ASTRO_TELEMETRY_PTP_OPCODE){
            const uint32_t seq=(uint32_t)frame[4] |
                ((uint32_t)frame[5]<<8) | ((uint32_t)frame[6]<<16) |
                ((uint32_t)frame[7]<<24);
            if(seq!=last_sequence || (uint32_t)(now-last_log)>10000){
                DryosDebugMsg(0,15,
                    "ASTRO_PTP point seq=%u valid=%u src=%u matches=%u",
                    seq,(unsigned)frame[25],(unsigned)frame[24],
                    (unsigned)((uint16_t)frame[22] | ((uint16_t)frame[23]<<8)));
                last_sequence=seq;
                last_log=now;
            }
        }
        if(context->send_data(context->handle,frame,sizeof(frame),
                              sizeof(frame),0,0,0)){
            DryosDebugMsg(0,15,"ASTRO_PTP data send failed op=0x%04X",opcode);
            resp.id=PTP_RC_GeneralError;
        }
    } else {
        resp.id=PTP_RC_ParameterNotSupported;
    }
    context->send_resp(context->handle,&resp);
    return 1;
}

int astro_ptp_register(void)
{
    /* DryOS calls 10-argument PTP handlers; the published registration
     * prototype describes only 4 arguments. Use the ABI-compatible union. */
    union {
        int (*registration)(void*,struct ptp_context*,void*,void*);
        int (*canon_abi)(void*,struct ptp_context*,uint32_t,uint32_t,uint32_t,
                         uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    } cb;
    cb.canon_abi=camera_mount_ptp;
    const int point=(int)ptp_register_handler(
        ASTRO_TELEMETRY_PTP_OPCODE,cb.registration,0);
    const int capture=(int)ptp_register_handler(
        ASTRO_MOUNT_CAPTURE_OPCODE,cb.registration,0);
    const int mailbox=(int)ptp_register_handler(
        ASTRO_MOUNT_STATUS_OPCODE,cb.registration,0);
    DryosDebugMsg(0,15,"ASTRO_PTP register point=%d capture=%d mailbox=%d",
                  point,capture,mailbox);
    return point|capture|mailbox;
}

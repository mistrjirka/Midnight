/* Vendor operation 0x9B32 for read-only Midnight pointing.
 * No camera actions or motor control occur in the USB callback.
 * GPL-2.0-or-later. */
#include <stdint.h>
#include <ptp.h>
#include "astro_telemetry.h"
#include "astro_ptp.h"
static int camera_telemetry_ptp(
    void *priv, struct ptp_context *context,
    uint32_t opcode, uint32_t session, uint32_t transaction,
    uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5)
{
    (void)priv;(void)opcode;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;
    uint8_t frame[ASTRO_TELEMETRY_BYTES];
    const uint32_t now=(uint32_t)get_ms_clock();
    astro_telemetry_encode(frame,now);
    static uint32_t last_log=0,last_sequence=0;
    const uint32_t sequence=(uint32_t)frame[4] |
        ((uint32_t)frame[5]<<8) | ((uint32_t)frame[6]<<16) |
        ((uint32_t)frame[7]<<24);
    if(sequence!=last_sequence || (uint32_t)(now-last_log)>10000) {
        DryosDebugMsg(0,15,"ASTRO_PTP read opcode=0x%04X seq=%u valid=%u src=%u matches=%u",
            (unsigned)ASTRO_TELEMETRY_PTP_OPCODE,(unsigned)sequence,
            (unsigned)frame[25],(unsigned)frame[24],
            (unsigned)((uint16_t)frame[22] | ((uint16_t)frame[23]<<8)));
        last_log=now;last_sequence=sequence;
    }
    struct ptp_msg resp = {
        .id=PTP_RC_OK,
        .session=session,
        .transaction=transaction,
        .param_count=0
    };
    if(context->send_data(context->handle,frame,sizeof(frame),
                          sizeof(frame),0,0,0)) {
        DryosDebugMsg(0,15,"ASTRO_PTP USB data send failed");
        resp.id=PTP_RC_GeneralError;
    }
    context->send_resp(context->handle,&resp);
    return 1;
}
int astro_ptp_register(void)
{
    /* Same DryOS API used by Magic Lantern's built-in vendor PTP commands.
     * The handler remains registered until camera restart. */
    /* Magic Lantern PTP_HANDLER callbacks receive 10 arguments on DryOS.
     * The exported ptp_register_handler declaration incorrectly lists only
     * four. Preserve the working PTP_HANDLER ABI without an incompatible
     * direct function-pointer cast that breaks modern GCC -Werror. */
    union {
        int (*registration)(void*,struct ptp_context*,void*,void*);
        int (*canon_abi)(void*,struct ptp_context*,uint32_t,uint32_t,uint32_t,
                         uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    } cb;
    cb.canon_abi=camera_telemetry_ptp;
    const int result=(int)ptp_register_handler(
        ASTRO_TELEMETRY_PTP_OPCODE,cb.registration,0);
    DryosDebugMsg(0,15,"ASTRO_PTP registered opcode=0x%04X result=%d",
                  ASTRO_TELEMETRY_PTP_OPCODE,result);
    return result;
}

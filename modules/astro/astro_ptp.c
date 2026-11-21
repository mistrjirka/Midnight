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
    astro_telemetry_encode(frame,(uint32_t)get_ms_clock());
    struct ptp_msg resp = {
        .id=PTP_RC_OK,
        .session=session,
        .transaction=transaction,
        .param_count=0
    };
    if(context->send_data(context->handle,frame,sizeof(frame),
                          sizeof(frame),0,0,0))
        resp.id=PTP_RC_GeneralError;
    context->send_resp(context->handle,&resp);
    return 1;
}
int astro_ptp_register(void)
{
    /* Same DryOS API used by Magic Lantern's built-in vendor PTP commands.
     * The handler remains registered until camera restart. */
    return (int)ptp_register_handler(
        ASTRO_TELEMETRY_PTP_OPCODE,
        (int (*)(void*,struct ptp_context*,void*,void*))camera_telemetry_ptp,
        0);
}

/* Small bounded PTP mailbox that is safe to snapshot across DryOS tasks.
 * The sequence protects against partially updated 32-byte USB replies.
 * No cameras, motors or dynamic allocation used in this file. */
#include "mount_mailbox.h"
#include <string.h>

typedef struct {
    uint32_t command_sequence,target_ra;
    int32_t target_dec;
    uint32_t requested_id,completed_id,completed_point_sequence;
    uint8_t capture_state,command,target_valid;
    astro_mount_progress report;
} mount_mailbox_state;
static volatile unsigned revision;
static volatile unsigned writer_lock;
static mount_mailbox_state state;

/* Three DryOS contexts can write: menu, PTP and solver worker.
 * The revision counter alone cannot serialize multiple writers.
 * ARMv5 provides SWP; the host unit tests use GCC's atomic exchange. */
static void mailbox_lock(void)
{
#ifdef __arm__
    unsigned previous;
    do {
        const unsigned one=1;
        __asm__ __volatile__("swp %0, %1, [%2]" :
                             "=&r"(previous) : "r"(one),"r"(&writer_lock) : "memory");
    } while(previous);
#else
    while(__sync_lock_test_and_set(&writer_lock,1u)) {}
#endif
}
static void mailbox_unlock(void)
{
#ifdef __arm__
    __asm__ __volatile__("" ::: "memory");
    writer_lock=0;
#else
    __sync_lock_release(&writer_lock);
#endif
}
static void write_begin(void)
{
    mailbox_lock();
    revision++;
    __asm__ __volatile__("" ::: "memory");
}
static void write_end(void)
{
    __asm__ __volatile__("" ::: "memory");
    revision++;
    mailbox_unlock();
}
static void put32(uint8_t *p,uint32_t x) {
    for(int i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8));
}
static void put16(uint8_t *p,uint16_t x) {p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}

int astro_mount_mailbox_target(uint32_t ra,int32_t dec)
{
    if(ra>=3600000u || dec < -900000 || dec > 900000)return 0;
    write_begin();
    state.command_sequence++;
    if(!state.command_sequence)state.command_sequence++;
    state.target_ra=ra;state.target_dec=dec;
    state.target_valid=1;state.command=ASTRO_MOUNT_CMD_GOTO;
    memset(&state.report,0,sizeof(state.report));
    write_end();
    return 1;
}
void astro_mount_mailbox_stop(void)
{
    write_begin();
    state.command_sequence++;
    if(!state.command_sequence)state.command_sequence++;
    state.command=ASTRO_MOUNT_CMD_STOP;
    memset(&state.report,0,sizeof(state.report));
    write_end();
}
int astro_mount_mailbox_begin(uint32_t id)
{
    if(!id)return 0;
    write_begin();
    int result=0;
    if(state.requested_id==id)result=1;
    else if(state.capture_state!=ASTRO_MOUNT_CAPTURE_RUNNING){
        state.requested_id=id;
        state.capture_state=ASTRO_MOUNT_CAPTURE_RUNNING;
        state.completed_point_sequence=0;
        result=2;
    }
    write_end();
    return result;
}
void astro_mount_mailbox_complete(uint32_t id,int ok,uint32_t point_seq)
{
    if(!id)return;
    write_begin();
    if(id==state.requested_id &&
       state.capture_state==ASTRO_MOUNT_CAPTURE_RUNNING){
        state.completed_id=id;
        state.completed_point_sequence=ok?point_seq:0;
        state.capture_state=ok?ASTRO_MOUNT_CAPTURE_OK:ASTRO_MOUNT_CAPTURE_FAILED;
    }
    write_end();
}
void astro_mount_mailbox_encode(uint8_t out[ASTRO_MOUNT_MAILBOX_BYTES],uint32_t last_point)
{
    mount_mailbox_state snap;
    memset(out,0,ASTRO_MOUNT_MAILBOX_BYTES);
    for(int retry=0;retry<6;retry++){
        const unsigned before=revision;
        if(before&1u)continue;
        __asm__ __volatile__("" ::: "memory");
        memcpy(&snap,&state,sizeof(snap));
        __asm__ __volatile__("" ::: "memory");
        if(before!=revision || (revision&1u))continue;
        memcpy(out,"MNC1",4);
        put32(out+4,snap.command_sequence);
        put32(out+8,snap.target_ra);
        put32(out+12,(uint32_t)snap.target_dec);
        put32(out+16,snap.requested_id);
        put32(out+20,snap.completed_id);
        put16(out+24,snap.capture_state);
        out[26]=snap.command;
        out[27]=snap.target_valid;
        const uint32_t point=(snap.capture_state==ASTRO_MOUNT_CAPTURE_IDLE)
            ?last_point:snap.completed_point_sequence;
        put32(out+28,point);
        return;
    }
    /* Host must treat missing magic as invalid/retry rather than invent state. */
}

uint32_t astro_mount_mailbox_last_requested(void)
{
    return state.requested_id;
}

int astro_mount_mailbox_progress(uint32_t cmd_seq,uint32_t phase_and_arm,
                                uint32_t err,uint32_t steps,uint32_t stage)
{
    const uint32_t phase=phase_and_arm&255u;
    if(phase>8u || (phase_and_arm & ~0x1ffu) || err>3600000u ||
       steps>100u || stage>6u)return 0;
    write_begin();
    int accepted=0;
    if(cmd_seq!=0 && state.command_sequence==cmd_seq){
        state.report.command_seq=cmd_seq;
        state.report.error_e4=err;
        state.report.steps=steps;
        state.report.phase=(uint8_t)phase;
        state.report.stage=(uint8_t)stage;
        state.report.motor_ready=(phase_and_arm & 0x100u)!=0;
        state.report.valid=1;
        accepted=1;
    }
    write_end();
    return accepted;
}
void astro_mount_mailbox_read_progress(astro_mount_progress *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));
    for(int i=0;i<6;i++){
        const unsigned before=revision;
        if(before&1u)continue;
        __asm__ __volatile__("" ::: "memory");
        memcpy(out,&state.report,sizeof(*out));
        __asm__ __volatile__("" ::: "memory");
        if(before==revision && !(revision&1u))return;
    }
    memset(out,0,sizeof(*out));
}

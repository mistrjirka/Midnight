/* Mount-to-camera PTP mailbox. USB host requests a photo; the camera publishes
 * the selected target and capture result. No motor commands on the camera.
 * GPL-2.0-or-later. */
#ifndef ASTRO_MOUNT_MAILBOX_H
#define ASTRO_MOUNT_MAILBOX_H
#include <stdint.h>
#define ASTRO_MOUNT_MAILBOX_BYTES 32
#define ASTRO_MOUNT_CAPTURE_OPCODE 0x9B33u
#define ASTRO_MOUNT_STATUS_OPCODE 0x9B34u
#define ASTRO_MOUNT_PROGRESS_OPCODE 0x9B35u
#define ASTRO_MOUNT_CMD_NONE 0
#define ASTRO_MOUNT_CMD_GOTO 1
#define ASTRO_MOUNT_CMD_STOP 2
#define ASTRO_MOUNT_CAPTURE_IDLE 0
#define ASTRO_MOUNT_CAPTURE_RUNNING 1
#define ASTRO_MOUNT_CAPTURE_OK 2
#define ASTRO_MOUNT_CAPTURE_FAILED 3

/* Only explicit presses of GoTo / Stop change the command sequence. */
int astro_mount_mailbox_target(uint32_t ra,int32_t dec);
void astro_mount_mailbox_stop(void);

/* Return 2 for new job, 1 for duplicate ID, 0 for busy/invalid. */
int astro_mount_mailbox_begin(uint32_t request_id);
void astro_mount_mailbox_complete(uint32_t request_id,int success,uint32_t point_sequence);
uint32_t astro_mount_mailbox_last_requested(void);
/* ESP->Canon GoTo progress. PTP parameters: phase, error, iteration, stage,
 * camera command sequence. Camera rejects stale target sequence. */
typedef struct {
    uint32_t command_seq;
    uint32_t error_e4;
    uint32_t steps;
    uint8_t phase,stage,motor_ready,valid;
} astro_mount_progress;
int astro_mount_mailbox_progress(uint32_t command_seq,uint32_t phase_and_arm,
                                uint32_t error_e4,uint32_t steps,uint32_t stage);
void astro_mount_mailbox_read_progress(astro_mount_progress *out);
void astro_mount_mailbox_encode(uint8_t out[ASTRO_MOUNT_MAILBOX_BYTES],uint32_t latest_point_sequence);
#endif

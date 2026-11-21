/* Mount-to-camera PTP mailbox. USB host requests a photo; the camera publishes
 * the selected target and capture result. No motor commands on the camera.
 * GPL-2.0-or-later. */
#ifndef ASTRO_MOUNT_MAILBOX_H
#define ASTRO_MOUNT_MAILBOX_H
#include <stdint.h>
#define ASTRO_MOUNT_MAILBOX_BYTES 32
#define ASTRO_MOUNT_CAPTURE_OPCODE 0x9B33u
#define ASTRO_MOUNT_STATUS_OPCODE 0x9B34u
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
void astro_mount_mailbox_encode(uint8_t out[ASTRO_MOUNT_MAILBOX_BYTES],uint32_t latest_point_sequence);
#endif

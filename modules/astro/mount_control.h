/* User-facing still-photo mount sync and measured motion.
 * Motor actuation remains explicitly disabled until both drivers are verified.
 * GPL-2.0-or-later.
 */
#ifndef ASTRO_MOUNT_CONTROL_H
#define ASTRO_MOUNT_CONTROL_H
#include <stdint.h>
void astro_mount_sync_photo(void);
void astro_mount_measure_photo(void);
void astro_mount_status_menu(char *out,int size);
void astro_mount_delta_menu(char *out,int size);
/* The camera posts a target; the ESP32 polls it and owns navigation. */
int astro_mount_send_selected_target(uint32_t ra,int32_t dec);
void astro_mount_abort_target(void);
/* Called only from the PTP thread; enqueue, never expose the shutter in PTP. */
int astro_mount_remote_enqueue(uint32_t request_id);
void astro_mount_remote_measure(void);
#endif

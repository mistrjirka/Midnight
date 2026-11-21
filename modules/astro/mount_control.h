/* User-facing still-photo mount sync and measured motion.
 * Motor actuation remains explicitly disabled until both drivers are verified.
 * GPL-2.0-or-later.
 */
#ifndef ASTRO_MOUNT_CONTROL_H
#define ASTRO_MOUNT_CONTROL_H
void astro_mount_sync_photo(void);
void astro_mount_measure_photo(void);
void astro_mount_status_menu(char *out,int size);
void astro_mount_delta_menu(char *out,int size);
void astro_mount_goto_unavailable(void);
#endif

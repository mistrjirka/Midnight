/* Capture a new Canon still, then read its saved playback preview, not Live View.
 * GPL-2.0-or-later.
 */
#ifndef ASTRO_MOUNT_PHOTO_H
#define ASTRO_MOUNT_PHOTO_H
#include <stdint.h>
int astro_mount_capture_photo(uint8_t *image,uint32_t *captured_ms);
#endif

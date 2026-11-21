/* Camera-only read-only pointing snapshot. GPL-2.0-or-later. */
#ifndef ASTRO_TELEMETRY_H
#define ASTRO_TELEMETRY_H
#include <stdint.h>
#define ASTRO_TELEMETRY_BYTES 32
#define ASTRO_TELEMETRY_PTP_OPCODE 0x9B32
#define ASTRO_SOURCE_LIVE 1
#define ASTRO_SOURCE_GALLERY 2
#define ASTRO_SOURCE_TEST 3
#define ASTRO_SOURCE_GUIDE 4
#define ASTRO_SOURCE_STILL 5 /* Canon still exposure, verified solved saved preview */
/* RA and Dec in 1e-4 degrees; FOV in centidegrees. Validated plate solves only. */
void astro_telemetry_publish(uint32_t uptime_ms, uint32_t ra, int32_t dec,
                             uint16_t fov_cdeg, uint16_t matches, uint8_t source);
/* Zero validity before the first verified solution; no fabricated pointing. */
void astro_telemetry_encode(uint8_t out[ASTRO_TELEMETRY_BYTES], uint32_t read_ms);
#endif

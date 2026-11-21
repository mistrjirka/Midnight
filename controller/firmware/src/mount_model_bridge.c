/* Compile the exact same two-axis calibration and fixed-point sky geometry
 * used in Midnight into the ESP32 firmware. This keeps trajectory decisions
 * on the controller without duplicate calibration algorithms.
 * GPL-2.0-or-later.
 */
#include "../../../modules/astro/core/fixed_math.c"
#include "../../../modules/astro/mount_model.c"

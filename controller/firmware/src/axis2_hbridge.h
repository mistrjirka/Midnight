/* ESP32-S3 second-axis DC motor driver with independent 5 ms local watchdog.
 * GPIO pins intentionally UNCONFIGURED until wiring is confirmed.
 * GPL-2.0-or-later.
 */
#pragma once
#include <stdint.h>
bool axis2HBridgeBegin();
bool axis2HBridgeConfigured();
bool axis2HBridgePulse(int direction,uint32_t durationMs);
bool axis2HBridgeMoving();
void axis2HBridgeStop();

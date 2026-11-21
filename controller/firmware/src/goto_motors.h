/* Two-axis pulse driver contract. The production implementation remains
 * fail-closed until the second-axis driver, power and STOP watchdog exist.
 * GPL-2.0-or-later.
 */
#pragma once
#include <stdint.h>
extern "C" {
#include "../../../modules/astro/mount_model.h"
}
enum class GoToMotorStatus : uint8_t {UNAVAILABLE, ACTIVE, STOP_CONFIRMED, FAILED};
class GoToMotorPort {
public:
    /* Both motors must have confirmed stop acknowledgement and local
     * timeout enforcement BEFORE ready() can be changed to true. */
    bool ready() const;
    bool pulse(const mount_pulse &command,uint32_t now);
    GoToMotorStatus poll(uint32_t now);
    void emergencyStop();
};

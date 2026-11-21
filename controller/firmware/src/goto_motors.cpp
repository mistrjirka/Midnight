#include "goto_motors.h"
#if defined(ARDUINO)
#include "axis2_hbridge.h"
#endif
/* RA is available over OG Star Tracker Wi-Fi, but its remote stop is not
 * guaranteed when the link drops. The second axis is not wired.
 * Never claim the hardware interlock is ready in this firmware. */
bool GoToMotorPort::ready() const {return false;}
bool GoToMotorPort::pulse(const mount_pulse &,uint32_t) {return false;}
GoToMotorStatus GoToMotorPort::poll(uint32_t) {return GoToMotorStatus::UNAVAILABLE;}
void GoToMotorPort::emergencyStop() {
#if defined(ARDUINO)
    // Independently stop the local DC H-bridge, if fitted.
    axis2HBridgeStop();
#endif
    /* The RA manual stop/retry loop remains in main.cpp. The automatic
     * controller is never armed while the RA stop is network-dependent. */
}

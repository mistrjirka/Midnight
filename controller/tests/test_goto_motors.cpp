/* Production motor port refuses automatic movement until BOTH axes and a
 * reliable stop watchdog have passed hardware tests.
 */
#include "../firmware/src/goto_motors.h"
#include <cassert>
#include <cstdio>
int main(){
    GoToMotorPort motor;
    assert(!motor.ready());
    mount_pulse pulse={0,1,800,10000};
    assert(!motor.pulse(pulse,1234));
    pulse.axis=1;
    assert(!motor.pulse(pulse,1235));
    assert(motor.poll(1240)==GoToMotorStatus::UNAVAILABLE);
    motor.emergencyStop();
    puts("PASS production GoTo motor port is fail closed");
}

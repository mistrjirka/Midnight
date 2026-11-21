#include "axis2_hbridge.h"
#include "axis2_pulse_logic.h"
#include <Arduino.h>
#include <driver/gpio.h>
#if defined(MIDNIGHT_AXIS2_IN1) != defined(MIDNIGHT_AXIS2_IN2)
#error "Define both MIDNIGHT_AXIS2_IN1 and MIDNIGHT_AXIS2_IN2 or neither"
#endif
/* Set BOTH pin names as compile flags only once board wiring is verified:
 * -D MIDNIGHT_AXIS2_IN1=4 -D MIDNIGHT_AXIS2_IN2=5
 * Optional: -D MIDNIGHT_AXIS2_ENABLE=6, -D MIDNIGHT_AXIS2_ESTOP=7
 * Do not use USB GPIO19/20, flash/PSRAM, or ESP32 strapping pins.
 */
namespace {
#if defined(MIDNIGHT_AXIS2_IN1) && defined(MIDNIGHT_AXIS2_IN2)
constexpr int pinA=MIDNIGHT_AXIS2_IN1,pinB=MIDNIGHT_AXIS2_IN2;
#if defined(MIDNIGHT_AXIS2_ENABLE)
constexpr int pinEn=MIDNIGHT_AXIS2_ENABLE;
#else
constexpr int pinEn=-1;
#endif
#if defined(MIDNIGHT_AXIS2_ESTOP)
constexpr int pinStop=MIDNIGHT_AXIS2_ESTOP;
#else
constexpr int pinStop=-1;
#endif
#else
constexpr int pinA=-1,pinB=-1,pinEn=-1,pinStop=-1;
#endif
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static Axis2PulseLogic pulse;
static bool configured=false;
static bool watchdogRunning=false;
static bool validPin(int p) {
    // Explicit whitelist on S3, avoiding USB, strapping and PSRAM pins.
    return (p>=4 && p<=18) || p==21 || (p>=38 && p<=42);
}
static bool stopAsserted(){
    return pinStop>=0 && digitalRead(pinStop)==LOW;
}
static void outputs(Axis2Drive drive){
    if(!configured)return;
    // LOW/LOW means coast. Never HIGH/HIGH (electrical braking).
    gpio_set_level((gpio_num_t)pinA,drive==Axis2Drive::FORWARD);
    gpio_set_level((gpio_num_t)pinB,drive==Axis2Drive::REVERSE);
    if(pinEn>=0)
        gpio_set_level((gpio_num_t)pinEn,drive!=Axis2Drive::COAST);
}
static void watchdog(void*){
    for(;;){
        const uint32_t now=millis();
        const bool emergency=stopAsserted();
        portENTER_CRITICAL(&lock);
        if(emergency)pulse.stop(now);
        else pulse.tick(now);
        // Always drive the physical STOP outputs even if the USB/Wi-Fi
        // tasks are blocked. The watchdog task has a separate stack.
        outputs(pulse.drive());
        portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(5)>0?pdMS_TO_TICKS(5):1);
    }
}
}
bool axis2HBridgeBegin(){
    if(configured)return true;
    if(!validPin(pinA) || !validPin(pinB) || pinA==pinB ||
       (pinEn>=0 && (!validPin(pinEn) || pinEn==pinA || pinEn==pinB)) ||
       (pinStop>=0 && (!validPin(pinStop) || pinStop==pinA ||
                       pinStop==pinB || pinStop==pinEn)))
        return false;
    // Preload low outputs before switching the GPIO direction.
    gpio_set_level((gpio_num_t)pinA,0);
    gpio_set_level((gpio_num_t)pinB,0);
    if(pinEn>=0)gpio_set_level((gpio_num_t)pinEn,0);
    pinMode(pinA,OUTPUT);
    pinMode(pinB,OUTPUT);
    if(pinEn>=0)pinMode(pinEn,OUTPUT);
    if(pinStop>=0)pinMode(pinStop,INPUT_PULLUP);
    configured=true;
    pulse.stop(millis());
    outputs(Axis2Drive::COAST);
    if(!watchdogRunning){
        BaseType_t ok=xTaskCreatePinnedToCore(watchdog,"axis2_watchdog",
                    2048,nullptr,4,nullptr,1);
        if(ok!=pdPASS){
            configured=false;
            gpio_set_level((gpio_num_t)pinA,0);
            gpio_set_level((gpio_num_t)pinB,0);
            if(pinEn>=0)gpio_set_level((gpio_num_t)pinEn,0);
            return false;
        }
        watchdogRunning=true;
    }
    return true;
}
bool axis2HBridgeConfigured(){return configured && !stopAsserted();}
bool axis2HBridgePulse(int direction,uint32_t ms){
    if(!configured || stopAsserted())return false;
    const uint32_t now=millis();
    portENTER_CRITICAL(&lock);
    const bool ok=pulse.start(direction,ms,now);
    if(ok)outputs(pulse.drive());
    portEXIT_CRITICAL(&lock);
    return ok;
}
bool axis2HBridgeMoving(){
    portENTER_CRITICAL(&lock);
    const bool active=pulse.active();
    portEXIT_CRITICAL(&lock);
    return active;
}
void axis2HBridgeStop(){
    portENTER_CRITICAL(&lock);
    pulse.stop(millis());
    outputs(Axis2Drive::COAST);
    portEXIT_CRITICAL(&lock);
}

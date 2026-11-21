#include "../firmware/src/goto_loop.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>

constexpr double pi=3.14159265358979323846;
struct Sky {
    double ra,dec;
    double e[2],n[2];
    double dra,ddec;
    uint32_t now=1000;
    uint32_t seq=0;
    std::mt19937 rng;
    std::uniform_int_distribution<int> noise{-4,4};
    Sky(uint32_t seed,double start_ra,double start_dec,
        double e0,double n0,double e1,double n1,bool tracking):
        ra(start_ra),dec(start_dec),dra(tracking?0.0006:0.048),
        ddec(tracking?0.0004:0.002),
        rng(seed) {
        e[0]=e0;e[1]=e1;n[0]=n0;n[1]=n1;
    }
    void move(uint32_t ms,int axis=-1,int direction=0){
        if(axis>=0){
            const double north=n[axis]*direction*ms;
            const double east=e[axis]*direction*ms;
            const double cosine=cos(dec*pi/1800000.0);
            ra+=east/cosine;
            dec+=north;
        }
        ra+=dra*ms;
        dec+=ddec*ms;
        ra=fmod(ra+3600000,3600000.0);
        now+=ms;
    }
    CameraPointing capture(){
        move(2200);
        CameraPointing p={};
        p.valid=true;p.source=5;p.matches=18;p.fovCdeg=900;
        p.ra=(uint32_t)(fmod(ra+3600000,3600000.0)+0.5);
        p.dec=(int32_t)round(dec)+(int32_t)noise(rng);
        p.sequence=++seq;p.ageAtReadMs=1800;
        move(1800);
        return p;
    }
};
static bool one(uint32_t seed, bool tracking, bool distant, bool verbose=false){
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ra_dist(0,3600000);
    std::uniform_real_distribution<double> dec_dist(-570000,570000);
    std::uniform_real_distribution<double> angle_dist(0,2*pi);
    std::uniform_real_distribution<double> speed_dist(1.2,4.8);
    std::uniform_real_distribution<double> delta_dist(-1,1);
    const double ra=ra_dist(rng), dec=dec_dist(rng);
    const double angle0=angle_dist(rng);
    const double delta=(.42+fabs(delta_dist(rng)))*((seed%2)?1:-1);
    const double angle1=angle0+delta;
    const double speed0=speed_dist(rng), speed1=speed_dist(rng);
    const double sign0=seed%3?-1.0:1.0;
    const double sign1=seed%4?1.0:-1.0;
    Sky sky(seed,ra,dec,
            speed0*cos(angle0)*sign0,speed0*sin(angle0)*sign0,
            speed1*cos(angle1)*sign1,speed1*sin(angle1)*sign1,tracking);
    // Random start: RA can wrap around 0°, Dec has arbitrary sky orientation.
    // Generate an angular destination 2-5° away, and for distant trials 20-45°.
    double ddec=delta_dist(rng)*(distant?200000:30000);
    double east=delta_dist(rng)*(distant?250000:35000);
    double targetDec=dec+ddec;
    const double cosDec=cos(dec*pi/1800000.0);
    double targetRa=fmod(ra+east/cosDec+7200000,3600000.0);
    if(targetDec>=895000 || targetDec<=-895000) return true;
    GoToLoop loop;
    if(!loop.start((uint32_t)round(targetRa),(int32_t)round(targetDec),sky.now))
        return false;
    uint32_t id=1;
    for(int action=0;action<1900;action++){
        loop.tick(sky.now);
        if(loop.needsPhoto()){
            if(!loop.beginPhoto(id,sky.now)) return false;
            auto p=sky.capture();
            if(!loop.finishPhoto(id++,p,sky.now))break;
        }else if(loop.needsPulse()){
            mount_pulse pulse={};
            if(!loop.nextPulse(pulse) || !loop.pulseStarted(sky.now))
                return false;
            sky.move(pulse.duration_ms,pulse.axis,pulse.direction);
            loop.pulseStopped(true,sky.now);
            sky.move(GoToLoop::SETTLE_MS);
        }else{
            if(loop.phase()==GoToPhase::ARRIVED)return true;
            if(loop.phase()==GoToPhase::SETTLING){
                sky.move(GoToLoop::SETTLE_MS);
            }else break;
        }
    }
    if(verbose){
        printf("SEED %u tracking=%d distant=%d start=%.3f %.3f target=%.3f %.3f "
               "axis0=(%.3f,%.3f) axis1=(%.3f,%.3f) phase=%u fault=%u "
               "error=%u steps=%u now=%u\n",
            seed,tracking,distant,ra/10000,dec/10000,targetRa/10000,targetDec/10000,
            sky.e[0],sky.n[0],sky.e[1],sky.n[1],
            (unsigned)loop.phase(),(unsigned)loop.failure(),loop.errorE4(),
            loop.steps(),sky.now);
    }
    return false;
}
int main(int argc,char**argv){(void)argv;
    bool verbose=argc>1;
    int total=0,failures=0;
    for(uint32_t seed=1;seed<=240;seed++){
        bool tracking=seed%2;
        bool distant=seed%3==0;
        bool success=one(seed,tracking,distant,verbose);
        total++;
        if(!success){
            failures++;
            if(!verbose)one(seed,tracking,distant,true);
        }
    }
    printf("RANDOM GO-TO %d/%d PASS\n",total-failures,total);
    return failures?1:0;
}

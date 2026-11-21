#!/usr/bin/env python3
"""Run realistic PTP packets against a PHYSICAL ESP32-S3 HIL firmware.

All motor outputs are disabled in this firmware image. The host simulation
supplies 32-byte MNC1/MNT1 protocol frames and acts on printed MOTOR requests.
It models unknown skewed axes, reversed wiring, and tracking on/off drift.

Run: controller/.venv/bin/python controller/tests/hil_simulate.py
"""
import argparse
import math
import random
import re
import struct
import time
from collections import Counter

import serial

def mnc(command_seq:int,target_ra:int,target_dec:int,command:int=1)->bytes:
    return struct.pack("<4sIIiIIHBBI",b"MNC1",
                       command_seq,target_ra,target_dec,0,0,0,command,1,0)

def mnt(sequence:int,ra:int,dec:int,capture_age:int=1800,source:int=5)->bytes:
    return struct.pack("<4sIIIiHHBB2sI",b"MNT1",
                       sequence,100000,ra%3600000,dec,250,18,source,1,b"\x00\x00",
                       100000+capture_age)

class PhysicalESP:
    def __init__(self,port):
        self.ser=serial.Serial(port,115200,timeout=0.050,write_timeout=2)
        self.commands=0
        time.sleep(.5)
        self.ser.reset_input_buffer()
    def send(self,command):
        self.ser.write((command+"\n").encode())
        self.commands+=1
        lines=[]
        deadline=time.monotonic()+2.5
        while time.monotonic()<deadline:
            chunk=self.ser.readline()
            if chunk:
                val=chunk.decode(errors="replace").strip()
                lines.append(val)
                if any(val.startswith(prefix) for prefix in (
                    "SIM STATUS", "SIM PHOTO", "SIM MOTOR", "SIM RESULT",
                    "SIM IDLE", "SIM TIME", "SIM OK RESET", "SIM OK MAILBOX",
                    "SIM OK POINT", "SIM OK STOP_CONFIRMED", "SIM ERR",
                    "ERR HIL_MOTORS_HARD_LOCKED")):
                    # Structured commands with multiple lines are handled
                    # by the additional quick read below.
                    pass
            elif lines:
                break
        if not lines:
            raise RuntimeError("No ESP32 response for "+command[:40])
        return lines
    def command(self,text):
        lines=self.send(text)
        return next((l for l in lines if l.startswith("SIM ") or
                      l.startswith("ERR HIL")),lines[-1])
    def close(self):self.ser.close()

class Sky:
    """Positions are 1e-4 deg. Motor speed never given to the ESP32."""
    def __init__(self,tracking:bool,start_ra=1000000.0,start_dec=350000.0,
                 vectors=None):
        self.tracking=tracking
        self.ra=float(start_ra)
        self.dec=float(start_dec)
        self.random_axes=vectors is not None
        # Reversed wiring: '+' drives RA west and DEC south, not east/north.
        # Axis 1 also runs diagonally across RA: NOT perpendicular.
        self.motor_ra=[-0.95,-0.31]
        self.motor_dec=[+0.13,-0.88]
        if vectors is not None:
            self.motor_ra=[vectors[0][0],vectors[1][0]]
            self.motor_dec=[vectors[0][1],vectors[1][1]]
        # Tracking OFF makes the fixed camera drift east in J2000 coordinates.
        self.drift_ra=0.049 if not tracking else 0.0007
        self.drift_dec=0.004 if not tracking else 0.0005
        self.clock=1000
        self.motion=Counter()
    def advance(self,ms:int,axis=None,direction=0):
        self.ra=(self.ra+self.drift_ra*ms)%3600000
        self.dec+=self.drift_dec*ms
        if axis is not None:
            east=self.motor_ra[axis]*direction*ms
            # For randomized sky orientations, the motor vector is measured
            # in angular east/north movement. RA scales by 1/cos(Dec).
            ra_motion=east / max(0.2,math.cos(math.radians(self.dec/10000)))                       if self.random_axes else east
            self.ra=(self.ra+ra_motion)%3600000
            self.dec+=self.motor_dec[axis]*direction*ms
            self.motion[(axis,direction)]+=1
        self.clock+=ms

def parsekv(msg):
    # Keep the first key. The dry-run OG HTTP path includes its own
    # direction=0/1, which must not overwrite the signed logical direction.
    result={}
    for key,val in re.findall(r"([a-z0-9_]+)=(-?\d+)",msg):
        result.setdefault(key,int(val))
    return result

def capture(esp,sky,seq,id):
    # Still capture midpoint occurs 2.2 seconds after shutter request.
    # The camera sends its readout 1.8 seconds after the midpoint.
    sky.advance(2200)
    sample_ra=round(sky.ra)
    sample_dec=round(sky.dec)
    sky.advance(1800)
    esp.command("SIM TIME 4000")
    response=esp.command(f"SIM POINT {id} {mnt(seq,sample_ra,sample_dec).hex()}")
    if response!="SIM OK POINT":
        raise RuntimeError(f"Camera solve rejected: {response}")
    return (sample_ra,sample_dec)

def run_scenario(esp:PhysicalESP,tracking:bool,scene:int,
                 sky=None,target=None):
    if sky is None:sky=Sky(tracking)
    esp.command("SIM RESET")
    # Target east and north, while BOTH physical forward directions are
    # reversed. This also exercises skewed / non-orthogonal axis fitting.
    target_ra=round((sky.ra+11200)%3600000) if target is None else target[0]
    target_dec=round(sky.dec+6500) if target is None else target[1]
    response=esp.send("SIM MAILBOX "+mnc(scene,target_ra,target_dec).hex())
    if not any(l=="SIM OK MAILBOX" for l in response):
        raise RuntimeError("Target mailbox rejected: "+str(response))
    counts=Counter()
    records=[]
    sequence=0
    for iteration in range(650):
        action=esp.command("SIM NEXT")
        if action.startswith("SIM PHOTO"):
            photo_id=parsekv(action)["id"]
            sequence+=1
            shot=capture(esp,sky,sequence,photo_id)
            counts["photos"]+=1
            records.append((sky.clock,"PHOTO",photo_id,shot))
        elif action.startswith("SIM MOTOR"):
            args=parsekv(action)
            axis=args["axis"]
            direction=args["direction"]
            duration=args["duration_ms"]
            assert axis in (0,1) and direction in (-1,+1)
            assert 50<=duration<=1000
            # Validate the real OG HTTP command URL and H-bridge truth table,
            # not just an abstract motor direction. Neither is executed.
            if axis==0:
                assert "dryrun_http=/startslew?speed=400&direction="+(
                    "0" if direction>0 else "1") in action
                assert "dryrun_stop=/stopslew" in action
            else:
                assert args["in1"]==(1 if direction>0 else 0)
                assert args["in2"]==(1 if direction<0 else 0)
                assert args["stop_in1"]==0 and args["stop_in2"]==0
            sky.advance(duration,axis,direction)
            esp.command(f"SIM TIME {duration}")
            ack=esp.command("SIM ACK")
            if ack!="SIM OK STOP_CONFIRMED":
                raise RuntimeError("Stop not acknowledged: "+ack)
            # A correction may request a PAIR of bounded motor pulses.
            # Each gets a stop ACK, but only one new photograph is needed
            # after both have stopped.
            status=esp.command("SIM STATUS")
            if "phase=SETTLING" in status:
                sky.advance(1200)
                esp.command("SIM TIME 1200")
            elif "phase=NEED_PULSE" not in status:
                raise RuntimeError("Unexpected phase after motor STOP: "+status)
            counts[f"axis{axis}"]+=1
            counts[f"motor_direction_{axis}_{direction}"]+=1
            records.append((sky.clock,"MOTOR",axis,direction,duration))
        else:
            status=esp.command("SIM STATUS")
            if "phase=ARRIVED" in status:
                info=parsekv(status)
                assert info["error_e4"]<=500
                # Both axes, both directions must get calibrated despite
                # reversed polarity.
                assert counts["axis0"]>=2 and counts["axis1"]>=2
                for ax in (0,1):
                    for d in (-1,1):
                        assert sky.motion[(ax,d)]>0,sky.motion
                if not tracking:
                    assert info["drift_valid"]==1
                    assert abs(info["drift_e_q16"])>500
                else:
                    assert info["drift_valid"]==1
                return {
                    "tracking":tracking,"photos":counts["photos"],
                    "pulses":counts["axis0"]+counts["axis1"],
                    "ra_pulses":counts["axis0"],"axis2_pulses":counts["axis1"],
                    "corrections":info["corrections"],
                    "final_error_e4":info["error_e4"],
                    "drift_e_q16":info["drift_e_q16"],
                    "drift_n_q16":info["drift_n_q16"],
                    "virtual_ms":sky.clock,
                    "all_commands":esp.commands,
                    "trace":records,
                }
            raise RuntimeError(f"Loop stopped before convergence: {action} / {status}")
    raise RuntimeError("Simulation did not converge in 125 actions")

def random_scenario(esp,seed,tracking):
    r=random.Random(seed)
    # Camera approximately points upward, but RA is completely unknown.
    # At Czech latitudes zenith is around Dec +50, not a fixed celestial RA.
    ra=r.uniform(0,3600000)
    dec=r.uniform(300000,650000)
    angle=r.uniform(0,2*math.pi)
    second=angle+r.choice([-1,1])*r.uniform(.6,1.5)
    speed1=r.uniform(1.8,4.3)
    speed2=r.uniform(1.8,4.3)
    # Do not assume either logical forward direction maps to east or north.
    polarity1=r.choice([-1,1])
    polarity2=r.choice([-1,1])
    vectors=((polarity1*speed1*math.cos(angle),
              polarity1*speed1*math.sin(angle)),
             (polarity2*speed2*math.cos(second),
              polarity2*speed2*math.sin(second)))
    sky=Sky(tracking,ra,dec,vectors)
    target_e=r.uniform(-24000,24000)
    target_n=r.uniform(-24000,24000)
    if abs(target_e)+abs(target_n)<8000:target_n+=8000
    target=(round((ra+target_e/math.cos(math.radians(dec/10000)))%3600000),
            round(dec+target_n))
    result=run_scenario(esp,tracking,1000+seed,sky,target)
    result["seed"]=seed
    result["start_ra"]=round(ra)
    result["start_dec"]=round(dec)
    return result

def test_faults(esp):
    # Prove normal RA endpoints are unavailable in this HIL firmware.
    assert esp.command("SLEW 0 100 400")=="ERR HIL_MOTORS_HARD_LOCKED"
    assert esp.command("TRACK 1")=="ERR HIL_MOTORS_HARD_LOCKED"
    assert esp.command("STOP")=="ERR HIL_MOTORS_HARD_LOCKED"
    esp.command("SIM RESET")
    esp.command("SIM MAILBOX "+mnc(333,1010000,352000).hex())
    shot=esp.command("SIM NEXT")
    assert "SIM PHOTO id=" in shot
    id=parsekv(shot)["id"]
    assert esp.command("SIM POINT 999 "+mnt(1,1000000,350000).hex()).startswith("SIM ERR")
    esp.command("SIM TIME 4000")
    assert esp.command("SIM POINT "+str(id)+" "+
                       mnt(1,1000000,350000,source=2).hex()).startswith("SIM ERR")
    assert "phase=FAULT" in esp.command("SIM STATUS")
    esp.command("SIM RESET")
    esp.command("SIM MAILBOX "+mnc(334,1010000,352000).hex())
    assert esp.command("SIM NEXT").startswith("SIM PHOTO")
    esp.command("SIM CANCEL")
    assert "phase=STOPPED" in esp.command("SIM STATUS")
    return "PASS hardware locked, wrong photo ID, gallery rejection, stop"

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--port",default="/dev/ttyACM0")
    parser.add_argument("--scenario",choices=("all","off","on","random"),default="all")
    args=parser.parse_args()
    esp=PhysicalESP(args.port)
    try:
        print(test_faults(esp),flush=True)
        if args.scenario=="random":
            for i,(seed,tracking) in enumerate([
                    (11,False),(22,True),(33,True),(44,False),
                    (55,False),(66,True)]):
                result=random_scenario(esp,seed,tracking)
                print("PASS RANDOM",seed,"tracking",tracking,
                      "start_ra",result["start_ra"],
                      "start_dec",result["start_dec"],
                      "photos",result["photos"],
                      "pulses",result["pulses"],
                      "error_e4",result["final_error_e4"],flush=True)
            print("PASS 6/6 RANDOM ORIENTATIONS ON REAL ESP32",flush=True)
            return
        for tracking,scene in [(False,1),(True,2)]:
            if args.scenario=="off" and tracking:continue
            if args.scenario=="on" and not tracking:continue
            result=run_scenario(esp,tracking,scene)
            print("PASS",result["tracking"],"photos",result["photos"],
                  "pulses",result["pulses"],"RA",result["ra_pulses"],
                  "axis2",result["axis2_pulses"],"corrections",result["corrections"],
                  "error_e4",result["final_error_e4"],
                  "drift_e_q16",result["drift_e_q16"],flush=True)
            print("TRACE",result["trace"][-10:],flush=True)
        print("PASS Real ESP32-S3 HIL simulation completed",flush=True)
    finally:
        esp.close()
if __name__=="__main__":
    main()

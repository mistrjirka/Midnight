# ESP32-S3 GoTo: simulation and second-axis H-bridge

## Verified real ESP32 simulation

The **real ESP32-S3** was temporarily flashed with the `esp32s3-hil`
environment. The laptop/workstation test script sent 32-byte camera `MNC1`
(target and Stop) and `MNT1` (solved pointing) packets over its actual USB
serial connection. It parsed the ESP32's requested photos and commanded motor
pulses and simulated the resulting sky movement.

```sh
cd controller/firmware
../.venv/bin/pio run -e esp32s3-hil -t upload
cd ../..
controller/.venv/bin/python controller/tests/hil_simulate.py --scenario all
# When finished, RESTORE the normal, non-HIL production firmware:
cd controller/firmware
../.venv/bin/pio run -e esp32s3 -t upload
```

The HIL firmware locks out `SLEW`, `TRACK`, `STOP`, and `AXIS2` commands
that could affect a physical motor. `SIM NEXT` instead prints *hypothetical*
commands. It validates the OG tracker `/startslew?speed=400&direction=...`
and `/stopslew` paths and the H-bridge IN1/IN2 truth table, without sending
any electrical or Wi-Fi movement requests.

### Physical ESP32-S3 results (November 21, 2026)

| Scenario | Photos | Motor pulses | Corrections | Final sky error |
|---|---:|---:|---:|---:|
| Tracking OFF, sidereal drift | 18 | 16 (RA 6, axis 2 10) | 12 | 0.0065° (23.4 arcsec) |
| Tracking ON, small residual drift | 24 | 22 (RA 11, axis 2 11) | 18 | 0.0003° (1.08 arcsec) |

Both scenarios deliberately **reverse motor wiring polarity** from intuitive
RA/DEC directions. Axis 2 also moves in a skewed east/south direction, not
perpendicular to RA. The controller does not know the speed or direction
of either motor in advance.

The controller issues small forward/backward calibration pulses and measures
their actual effect from fresh plate-solved images. It fits two signed
east/north movement vectors, solves the resulting 2×2 linear system, and
iteratively corrects the target error. It rejects nearly parallel axes and
unexpected reversal of an already calibrated direction.

### Sidereal drift compensation

Before moving a motor, the ESP32 requests **two still photos while both
motors are stopped**. The change between verified J2000 sky positions gives
an empirical east/north drift vector, even if tracking is OFF.

It subtracts `drift rate × time between photo exposures` from all later
motor calibration measurements. The elapsed time includes exposure, solve,
and settling delays, **not just motor-on time**. A new measured sky position
is still used after every move to correct cumulative error.

Measured with tracking OFF: drift about 2,621 Q16 units in east sky motion,
or approximately 0.24° per minute, consistent with sidereal sky motion.

These are realistic **simulated sky images and motor mechanics** processed
on the physical ESP32. They do not validate the camera USB electrical
connection, Canon real shutter behavior, or physical motor movement.

## Second-axis motor: H-bridge DC drive

Implemented in `axis2_hbridge.cpp` with `axis2_pulse_logic.cpp`.

- Two GPIO outputs: IN1 and IN2
- Optional GPIO output: ENABLE
- Optional active-low ESTOP input
- Positive pulse: IN1 HIGH, IN2 LOW
- Negative pulse: IN1 LOW, IN2 HIGH
- Stop/coast: IN1 LOW, IN2 LOW, ENABLE LOW
- 50–1,000 ms pulses, 20 ms coast/deadtime between pulses
- A separate high-priority FreeRTOS task checks a **local** expiry
  every ~5 ms and turns motor outputs off, even during Wi-Fi blocking

Both pins must be configured explicitly with compile flags, for example:

```ini
build_flags =
    -D MIDNIGHT_AXIS2_IN1=<verified GPIO>
    -D MIDNIGHT_AXIS2_IN2=<verified GPIO>
    -D MIDNIGHT_AXIS2_ENABLE=<optional verified GPIO>
    -D MIDNIGHT_AXIS2_ESTOP=<optional verified GPIO>
```

The example contains placeholders, **not recommended pins**. The driver
refuses missing, duplicate, or unsafe pin selections. The current flash
has **no H-bridge pins configured**, so it cannot energize axis 2.

The motor must receive power from a properly rated H-bridge and external
supply. Share the ESP ground with the H-bridge control ground, and check
3.3 V input compatibility. Do not connect a DC motor directly to an ESP pin.
A physical power cutoff is required for unattended operation. A software
watchdog is not a substitute for a hardware emergency stop.

The existing OG Star Tracker RA interface lacks a guaranteed stop on Wi-Fi
loss. Therefore the production GoTo motor adapter **always reports not
ready** until RA and axis 2 both have verified, safe stop mechanisms.
Ordinary RA manual control remains separate.

## Protocol and GoTo state

1. Camera sends a target in the PTP mailbox (`0x9B34`).
2. ESP32 requests a still photo (`0x9B33`) and receives a new
   solved pointing packet (`0x9B32`).
3. ESP32 captures a second still while stationary to measure sky drift.
4. It plans RA and second-axis calibration pulses.
5. After each confirmed motor stop and settling period, it asks the camera
   for another still, updates its motor vectors, and selects the next pulse.
6. Once near the target, it stops and reports the remaining sky error.

The camera receives progress reports through PTP `0x9B35` and displays
the current phase in Midnight's Mount control menu.

`GET /mount` on port 8081 includes state, error (degrees × 10000),
next axis, direction, requested pulse length and whether motors are
armed. `POST /goto/stop` requests cancellation. The physical motor output
remains unarmed today.


## Random starting orientation and arbitrary motor axes

The camera does **not** need a known home orientation. Its first blind plate
solve determines J2000 RA/Dec. The mount uses the actual solved coordinates
and the selected target to calculate a great-circle bearing on the celestial
sphere, including crossing RA=0. It never assumes that axis 1 points north,
axis 2 points east, the two axes are square, or that either forward direction
has the expected polarity.

To avoid applying one local motor calibration across a huge patch of sky,
a distant destination becomes a series of local guidance vectors no longer
than **5 degrees**. Individual commanded motor pulses stay <=1000 ms.
The controller takes a new plate-solved still after a bounded pair of pulses.
Each motor stops and acknowledges separately. A two-axis pair avoids
oscillation when a strongly skewed mount would move *away* from the
destination if one of the two necessary corrections were attempted alone.
If only one motor correction is needed, it commands just that one.

**Final physical-ESP32 randomized results (paired-axis loop):**

| Test | Tracking | Starting RA | Starting Dec | Photos | Pulses | Error |
|---|---|---:|---:|---:|---:|---:|
| 11 | Off | 162.86° | +49.59° | 16 | 24 | 0.0006° |
| 22 | On | 344.96° | +34.91° | 22 | 36 | 0.0250° |
| 33 | On | 205.32° | +52.13° | 14 | 20 | 0.0005° |
| 44 | Off | 147.07° | +48.97° | 16 | 24 | 0.0002° |
| 55 | Off | 32.52° | +63.27° | 7 | 6 | 0.0012° |
| 66 | On | 25.49° | +45.21° | 12 | 16 | 0.0002° |

**6/6 passed.** The virtual camera started at a different sky position
in each test, and both motor polarities, speed and axis skew were unknown
to the ESP32. These are synthetic sky positions supplied over actual ESP32
serial, not camera pictures or real motor measurements.

The randomized software tests include 240 independently seeded starts
distributed across all 360 degrees of RA and Dec roughly -57 to +57 degrees,
with signed unknown motor rates, both forward/reverse polarities,
non-orthogonal axes, tracking on/off, simulated solved-image noise, and
target offsets of a few to tens of degrees. **240/240 reached** the target
within the modeled limits. These randomized tests run the *production*
GoToLoop and mount_model implementations.

The physical ESP32 hardware-in-the-loop script also supports:

    controller/.venv/bin/python controller/tests/hil_simulate.py --scenario random

This sends six camera orientations with unknown RA, Dec +30 to +65 degrees,
random axis tilt/polarity/speed, tracking on/off, and realistic 32-byte PTP
packets to the actual ESP32. It checks both OG HTTP-path commands and
H-bridge pin truth tables without moving physical motors.

**"Mostly up" is an installation assumption, not a measured property.**
The camera has no calibrated gravity/azimuth sensor. With a known observing
location, time, and plate-solved RA/Dec, future firmware could also verify
target altitude. At present the controller cannot enforce horizon, tripod,
cable-twist, or mechanical end-stop limits. Very near celestial poles,
or a directly antipodal target with no unique shortest direction, the
solver deliberately refuses risky movement instead of guessing.
Automatic real-motor GoTo is still not armed.

Precision: normal target threshold is 0.035 deg (2.1 arcmin), with a
0.05 deg (3 arcmin) fallback only if the smallest safe motor pulse would
be larger than the remaining correction. A slow, near-parallel mount or a
destination outside mechanical range can time out or reach the correction
limit. The controller fails closed in those cases.

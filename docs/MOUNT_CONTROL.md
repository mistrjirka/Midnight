# Mount control: still-photo pointing and empirical axes

## Camera menu

![Real Canon 600D QEMU Mount control menu](images/midnight-mount-control-600d-qemu.png)

`Focus -> Midnight -> Mount control`:

| Item | Effect |
|---|---|
| Sync from photo | Capture and solve a new still photo; store current J2000 RA/Dec |
| Measure movement | Capture and solve again; show east/north displacement from previous point |
| Exposure | 1 or 2 seconds (default 2) |
| Position | Last verified RA/Dec |
| Last movement | Last measured east/north displacement |
| GoTo target | **Send**: publish the selected target for ESP32-controlled navigation |
| Stop mount | Publish Stop and cancel controller navigation |

The measurement path does **not** use the Live View feature, its digital gain, or an amplified Live View frame. In Manual photo mode, it temporarily requests the camera's maximum *analogue* ISO, widest electronically controlled aperture (lowest f-number), and 1 or 2 second shutter. It then triggers a standard still photo without autofocus, waits for the camera file number to advance, and plate-solves the Canon saved-photo playback preview. Camera settings are restored even if capture fails. Open manual-aperture lenses physically.

The saved preview is currently trusted to be the newest photo after `enter_play_mode`. This assumption **requires real hardware validation**. Magic Lantern's camera emulator does not prove that the shutter creates a file or that playback displays the newest photo. A future motor-movement loop must verify the new image association before using the measurement.

## Planned closed-loop GoTo

The camera is the pointing sensor; the ESP32-S3 is the motor bridge.

1. Sync from a newly captured photo.
2. Probe the RA motor forward and backward. Re-solve after each safe motion.
3. Probe the secondary motor forward and backward in the same way.
4. Estimate each motor's **empirical east/north movement vector** in sky coordinates, including skew and backlash. No orthogonality assumption.
5. Convert the target offset into motor pulse durations by solving the 2x2 calibration matrix.
6. Move one axis for a bounded duration, then capture a new photo and measure the real result.
7. Update the measured motor vectors from accepted probes and repeat until the target is within tolerance. Abort on bad solves, nearly parallel axes, communication failures, excessive error or cancellation.

The pure-C `mount_model.c` already implements axis-vector estimation, RA wrap handling, non-orthogonal 2x2 inversion, bounded pulse planning and singularity checks. Its deterministic tests simulate iterative convergence.

**GoTo is intentionally disabled:** a physical Canon USB host connection is not verified, the camera-to-ESP motion command and acknowledgement channel is not implemented, and the secondary motor driver and GPIO/power wiring are not known. The existing ESP32 firmware controls the OG Star Tracker's RA motor only. No automatic motor action occurs on camera startup or after Sync.

The mount should not assume arbitrary sidereal drift subtraction. With RA tracking enabled, the before/after measured offset already includes any tracking error. Re-solving after each move is the feedback signal.


## Actual controller-owned flow (software implemented)

1. Select Target on the camera and press GoTo target.
2. The ESP32 polls PTP 0x9B34 and receives the selected J2000 RA/Dec.
3. The ESP32 requests a fresh saved still with a unique ID via 0x9B33.
4. The camera worker captures, solves and publishes pointing via 0x9B32.
5. The ESP32 checks the matching completion ID and fresh solve sequence.
6. The ESP32's own copy of the arbitrary two-axis model can calculate
   correction pulses after empirical motor calibration.
7. Each future movement will be followed by another still photo and re-solve.

Target transfer, remote photo requests, completion tracking and the
controller's navigation state machine are implemented and unit-tested.
**Actual motor GoTo stays disabled** until the USB connection and the
second-axis motor/driver are physically tested.


## Two-axis loop and H-bridge simulation

The ESP32 feedback state machine takes two stationary photos to learn sky
drift, tests both motors forward/backward, and requests another still
photo after every bounded movement. The controller estimates signed
movement vectors. This avoids assuming perpendicular axes or known motor
speed. With tracking off, it subtracts observed sky drift before fitting
motor response.

The ESP32's second-axis H-bridge driver is included but pins are not
configured. The OG RA drive has no guaranteed stop on lost Wi-Fi.
Therefore automatic physical GoTo remains disabled.

See docs/GOTO_HIL_TESTS.md for actual ESP32 simulation results.


## Random camera orientation

Point the camera generally at unobstructed sky and select a target. You
do **not** need to pre-align either motor direction. Midnight first solves
the camera's actual position, then the ESP32 learns each motor's signed
sky vector from short forward/backward test moves. The camera is allowed
to start at arbitrary RA and Dec, subject to a solvable star field and
mechanical reach. Targets far away are handled as repeated <=5 degree
local sky bearings and <=1 second motor pulses. The axes may be skewed.

Tracking off is also allowed. Two photos taken while the motors are stopped
measure sky drift. The ESP32 subtracts this drift from the motion used to
calibrate motors. It refreshes the drift estimate when progress stalls or
after a long move. Each pair of corrections is followed by another photo.

**No gravity sensor or limit-switch data is available.** "Mostly up" is a
physical setup condition, not proof that a particular GoTo path will avoid
a tripod, cables, horizon or hard stop. Hardware safeguards must be
installed before enabling automatic movement.

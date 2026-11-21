# Midnight pointing link

Camera is a USB peripheral. The ESP32-S3 must act as **USB host** to poll it. A USB power/data host adapter or a board with a dedicated USB-OTG port is required. A PC and the camera cannot both share the ESP32-S3 native USB port simultaneously.

Midnight registers a vendor PTP operation `0x9B32` when the astro module loads. It returns a fixed 32-byte payload. The PTP host requests it after the Canon session opens. PTP never moves a motor. The separate capture operation can enqueue an
ordinary Canon still exposure, but never blocks the USB callback.

Wire payload (all multi-byte integers little-endian):

- `[0:4]` = ASCII `MNT1`
- `[4:8]` = monotonically increasing solution sequence (`uint32`)
- `[8:12]` = camera uptime in milliseconds (`uint32`)
- `[12:16]` = RA, degrees x 10000 (`uint32`)
- `[16:20]` = signed Dec, degrees x 10000 (`int32`)
- `[20:22]` = horizontal field of view, centidegrees (`uint16`)
- `[22:24]` = independently matched star count (`uint16`)
- `[24]` = source: 1=photo LiveView, 2=gallery, 3=synthetic test, 4=Live View guide, 5=new 1-2 s saved still
- `[25]` = validity: 1 only for a successful verified solution
- `[26:28]` = reserved, zero
- `[28:32]` = camera uptime at PTP readout in milliseconds (`uint32`). Subtract the sample uptime from this value to obtain the true age, including plate-solving time.

On camera startup or before the first successful solution, validity is zero. A previous valid solution may be stale. The camera stores the *capture time*, not the time at which a potentially slow solver completed. The receiver uses readout time minus capture time to reject old data. Poller stores a second clock for time spent since USB receipt. Only source 1/4 (Live View, under 10 seconds) or source 5 (a new 1-2 second Canon still-photo solve, under 65 seconds) is eligible for future closed-loop control. Sources 2/3 (gallery/test) are rejected. The prototype never starts automatic GoTo from pointing data.

The ESP32's serial `POINT` command is a **test-only** injection and not the PTP transport. USB-host integration and physical camera validation remain necessary.

## Validation and limitations (November 21, 2026)

- Verified in Canon 600D 1.0.2 QEMU: the current GCC-built Midnight
  module registers opcode `0x9B32` (reported registration result `0`),
  and its unchanged sky solver solves the bundled image with 11 matches.
- Verified in a host integration test: the production camera callback
  generates a 32-byte pointing payload, and the production ESP32 decoder
  receives and validates it across fragmented simulated PTP bulk transfers.
- The ESP32 USB host compiles and is flashed on grammetry, but the physical
  Canon-to-ESP32 OTG link has **not** been connected or tested.
- QEMU does not prove real camera USB electrical compatibility. Do not use
  synthetic or gallery coordinates as automatic mount-movement targets.

Diagnostics are available at `/diagnostics` on port 8081 while connected
to the OG Star Tracker Wi-Fi, and through `HOST LOG` on the serial port
before switching the ESP32 to host mode. See `controller/README.md`.

### Mount control: still-photo prototype

Midnight's new **Mount control** submenu has **Sync from photo** and
**Measure movement**. Both require M-mode still shooting, not Live View,
and take 1-2 second exposures at maximum supported analogue ISO and the
widest electronically controlled aperture. Canon manual-aperture lenses
must be opened by hand. Saved-photo playback luma is plate-solved. Exposure
settings are restored even if capture fails.

The two-axis pure-C empirical pointing model already computes local east/north
displacements and safe bounded pulse plans using an arbitrary, non-perpendicular
motor basis. It rejects near-parallel axes, bad/old positions and movement
larger than 20 degrees in one planning step. It is not yet connected to the
motor controllers.

**GoTo is disabled.** There is no verified USB camera->controller command
channel yet and the secondary-axis driver/pins have not been specified.
Sync and Measure photo capture cannot be physically confirmed from 600D
QEMU, where the shutter and saved-photo preview may not behave like hardware.


## New camera target and remote shutter protocol

The Canon PTP USB peripheral cannot initiate a transaction. Instead,
the ESP32 host polls the camera mailbox to receive the selected target.

| PTP opcode | Function |
|---|---|
| 0x9B32 | Read 32-byte verified pointing packet, MNT1 |
| 0x9B33 | Request one new still exposure and solve, parameter = nonzero request ID |
| 0x9B34 | Read target command, request state and completed solve, MNC1 |

The camera's response for 0x9B33 is 0x2001 if accepted or idempotently
repeated, and 0x2019 if busy or not in manual still-photo mode.
The shutter runs later on the existing worker task, not in the USB callback.

MNC1 is 32 bytes, with little-endian values:

- 0:4 = ASCII MNC1
- 4:8 = command sequence, increments for every GoTo/Stop
- 8:12 = target RA (degrees times 10000)
- 12:16 = signed target Dec (degrees times 10000)
- 16:20 = last accepted request ID
- 20:24 = last completed request ID
- 24:26 = status: 0 idle, 1 capturing, 2 solved, 3 failed
- 26 = command: 0 none, 1 GoTo target, 2 Stop
- 27 = target valid flag
- 28:32 = sequence of the successful completed pointing packet

A camera request is accepted once per unique ID. The ESP32 accepts the
result only when the completed ID and pointing sequence match its request,
and only if the solution came from a newly captured still (source 5).

The ESP32 enters MOTORS_UNAVAILABLE after it receives the first verified
position, rather than moving without a confirmed second motor.
USB hardware and real Canon shutter capture remain unverified.


### GoTo progress status

The ESP32 host sends progress to the camera through USB PTP operation
0x9B35 with five unsigned parameters: phase and motor-ready flag,
remaining angular error in 1e-4 degree, correction count, stage (0-6)
and matching target command sequence. Midnight rejects stale status
commands and displays the current phase in Mount control.

The status command has no physical motor or shutter side effects.

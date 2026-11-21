# Midnight RA controller, ESP32-S3

This bridge uses the ESP32-S3 Wi-Fi radio to control **only the right ascension (RA)** drive of the OG Star Tracker. Declination (DEC) hardware is not used.

The firmware never moves the mount at startup. ESP32 USB serial at 115200 baud accepts:

- `PING`, `WIFI`, `VERSION`, `STATUS`, `POSITION`
- `POINT 1230000 -250000 12 200`: inject a synthetic test solution in 1e-4 degrees (RA, Dec, matched stars, FOV centidegrees). This does not move the mount.
- `GETPOINT`: read last reported pointing.
- `TRACK 1 0 15956`: enable RA tracking (only on explicit request). `TRACK 0`: turn off.
- `SLEW 0 200 400`: request an RA movement followed by a stop after 200 ms. Requested intervals cannot exceed 1000 ms, but network latency can extend actual motion. `STOP`: stop any pending manual slew.

The ESP32 scans for SSIDs starting with `OG Star Tracker` (including factory suffixes) and connects using the factory default password, `password123`. Its HTTP calls target `192.168.4.1`. Set different credentials in ESP32 NVS under namespace `midnight` keys `ssid`, `password` before deployment if changed.

**Important:** The tracker firmware has no guaranteed remote-stop watchdog. Wi-Fi loss or HTTP timeout during a manual slew can delay or prevent the stop command. The bridge retries and attempts to stop even when the start response is lost. Supervise all physical slews and have direct power access.

## Build and flash

From `controller/firmware` run `../.venv/bin/pio run -e esp32s3`, then `../.venv/bin/pio run -t upload -e esp32s3`.

## Camera USB host and telemetry

The firmware includes a Canon PTP USB host that can request a still
exposure, receive verified coordinates, and poll the target the user selected
on the camera. It stays **off** at startup because the ESP32-S3 shares the internal USB PHY between native USB serial/JTAG and USB OTG. Use `HOST START` to switch to host mode after making the camera-side USB wiring ready. The debug USB serial connection can disappear. Reboot the board to restore USB serial.

Power the Canon normally and provide USB host VBUS from a suitable powered OTG adapter. Verify the exact ESP32-S3 board USB wiring first. Do not connect 5 V directly to the ESP32-S3 data pins.

When Wi-Fi is connected, the bridge provides read-only `http://<esp-ip>:8081/health` and `http://<esp-ip>:8081/pointing` endpoints. They show a verified camera snapshot, its source and capture-to-readout age. They cannot start the mount.

The `POINT` serial command is a development injection, **not** camera telemetry. Until a physical Canon USB host test succeeds, live camera-to-ESP transfer remains unverified.

## Debugging the camera link

The ESP32 logs the latest **32 USB/PTP events** in memory, including USB
device detection, vendor/product IDs, the selected USB interface and endpoint
addresses, session opening, packet lengths, response errors, disconnects,
and accepted pointing updates. Logs are intentionally rate-limited.

- Before USB host mode: send `HOST STATUS` or `HOST LOG` on the ESP32
  115200 baud serial connection.
- After switching to camera USB host mode: connect a phone/laptop to the
  tracker AP and open `http://192.168.4.2:8081/diagnostics`. This remains
  available even if the ESP32 USB serial/JTAG connection disappears.
- `http://192.168.4.2:8081/health`: host state and Wi-Fi connectivity.
- `http://192.168.4.2:8081/pointing`: latest verified pointing with source
  and age. Synthetic POINT serial injections are never shown as camera data.
- Logs are stored in RAM and disappear on ESP32 reboot. Copy them before
  resetting after a failure.

Expected state transitions: `OFF -> AWAIT_CAMERA -> PTP_OPENING -> PTP_OPEN ->
NO_VERIFIED_POINT -> POINT_READY`. If it stops earlier, the log contains a
reason such as `NOT_A_PTP_CAMERA`, `PTP_CLAIM_FAILED`,
`ptp_command_write_failed`, `ptp_data_failed` or
`ptp_unexpected`. The USB bulk parser accepts partial and combined
PTP packets, but it rejects malformed headers, wrong transaction IDs
and payloads outside the documented bounds.

**Do not run HOST START while the ESP32 is still wired to a computer's
USB port.** Only switch to USB host after confirming its USB OTG data
connection and powered VBUS wiring for the Canon.

## Camera telemetry protocol

Midnight's camera module publishes only independently verified plate solutions. A USB PTP vendor operation reads a versioned snapshot containing RA, Dec, field of view, matched-star count and solution age. Do not treat a test/gallery frame as live pointing. See `docs/CONTROLLER_PTP.md`.

The camera module also accepts solved normal still-photo measurements as
telemetry source 5. The ESP32 rejects source 2 (gallery) and source 3 (test)
for any future motion, and uses strict age checks for still measurements.
Automatic GoTo and secondary-axis control remain disabled.


## Camera-selected target and controller-requested images

- Camera: select Target in Midnight, then press GoTo target. The camera
  publishes the J2000 RA/Dec and a monotonic command sequence. It then closes
  the menu so the ESP32 can request an ordinary saved still photo.
- ESP32: polls PTP operation 0x9B34 for the target and command. On a new
  target, queues a single still photo via 0x9B33 with a unique request ID.
  It reads the completed request ID and fresh pointing from 0x9B32.
- ESP32: owns the skewed, two-axis mathematical model and navigation state.
  It does not automatically move motors until the second driver and the
  USB host's electrical wiring are verified.
- Camera Stop mount: publishes an abort, cancels any active camera worker,
  and makes the ESP32 attempt an RA stop.

While the ESP32 is on the tracker Wi-Fi AP, use the new endpoints:
- GET /mount: target, navigation, last photo and disabled-motor status.
- POST /measure: request a new still. HTTP 202 includes the request ID;
  409 indicates the camera is offline/busy.
- GET /diagnostics: USB/PTP logs.

Before USB host mode, serial commands CAMERA STATUS, CAMERA TARGET and
CAMERA MEASURE inspect the same state or queue a photo.


## Closed-loop GoTo and second-axis H-bridge

The production ESP32 firmware includes an empirical two-axis feedback
controller. It requests two photos while stationary to measure apparent
sky drift with tracking on or off. Then it uses bounded pulses in both
directions of each axis. After every move it requests a new still photo and
fits signed east/north motor vectors. Reversed wires and non-perpendicular
motor axes are handled by those measured vectors.

The second axis has a DC H-bridge implementation with IN1/IN2 direction
pins, optional ENABLE/ESTOP, a local watchdog, a 20 ms reversal deadtime,
and 50-1000 ms pulse lengths. It remains unconfigured until the wiring
and emergency cutoff are confirmed.

Real ESP32-S3 hardware-in-the-loop tests used 32-byte MNT1/MNC1 camera
packets with both physical motor directions deliberately reversed and
tracking ON/OFF. No real motor commands are issued in the simulation.

See docs/GOTO_HIL_TESTS.md for wiring, validation and reproduction.


### Arbitrary starting position

The controller learns its starting RA/Dec from a still photo. It computes
spherical great-circle bearings, not a guessed motor direction. Each signed
motor vector is calibrated with test pulses, so the camera may start at any
solvable sky position and the physical motor polarity can be reversed.
Motor axes need not be perpendicular. Long travel uses repeated short
waypoint vectors and re-solving after paired corrections. See the
randomized validation results in docs/GOTO_HIL_TESTS.md.

The operator must still point the camera at clear sky and verify mechanical
travel limits. No hardware pose, horizon, cable collision or endstop sensor
is available yet. The real automatic motors remain unarmed.

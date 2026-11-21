# Midnight RA controller, ESP32-S3

This bridge uses the ESP32-S3 Wi-Fi radio to control **only the right ascension (RA)** drive of the OG Star Tracker. Declination (DEC) hardware is not used.

The firmware never moves the mount at startup. ESP32 USB serial at 115200 baud accepts:

- `PING`, `WIFI`, `VERSION`, `STATUS`, `POSITION`
- `POINT 1230000 -250000 12 200`: inject a verified test solution in 1e-4 degrees (RA, Dec, matched stars, FOV centidegrees). This does not move the mount.
- `GETPOINT`: read last reported pointing.
- `TRACK 1 0 15956`: enable RA tracking (only on explicit request). `TRACK 0`: turn off.
- `SLEW 0 200 400`: move RA in one direction for at most 200 ms. Max interval is 1000 ms. `STOP`: stop any pending manual slew.

The ESP32 scans for SSIDs starting with `OG Star Tracker` (including factory suffixes) and connects using the factory default password, `password123`. Its HTTP calls target `192.168.4.1`. Set different credentials in ESP32 NVS under namespace `midnight` keys `ssid`, `password` before deployment if changed.

**Important:** The tracker firmware has no guaranteed remote-stop watchdog. Wi-Fi loss during a manual slew can prevent a stop command. Supervise all physical slews and have direct power access.

## Build and flash

From `controller/firmware` run `../.venv/bin/pio run -e esp32s3`, then `../.venv/bin/pio run -t upload -e esp32s3`.

## Camera USB host and telemetry

The firmware includes a read-only Canon PTP USB host. It stays **off** at startup because the ESP32-S3 shares the internal USB PHY between native USB serial/JTAG and USB OTG. Use `HOST START` to switch to host mode after making the camera-side USB wiring ready. The debug USB serial connection can disappear. Reboot the board to restore USB serial.

Power the Canon normally and provide USB host VBUS from a suitable powered OTG adapter. Verify the exact ESP32-S3 board USB wiring first. Do not connect 5 V directly to the ESP32-S3 data pins.

When Wi-Fi is connected, the bridge provides read-only `http://<esp-ip>:8081/health` and `http://<esp-ip>:8081/pointing` endpoints. They show a verified camera snapshot, its source and capture-to-readout age. They cannot start the mount.

The `POINT` serial command is a development injection, **not** camera telemetry. Until a physical Canon USB host test succeeds, live camera-to-ESP transfer remains unverified.

## Camera telemetry protocol

Midnight's camera module publishes only independently verified plate solutions. A USB PTP vendor operation reads a versioned snapshot containing RA, Dec, field of view, matched-star count and solution age. Do not treat a test/gallery frame as live pointing. See `docs/CONTROLLER_PTP.md`.

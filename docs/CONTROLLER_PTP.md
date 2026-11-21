# Midnight pointing link

Camera is a USB peripheral. The ESP32-S3 must act as **USB host** to poll it. A USB power/data host adapter or a board with a dedicated USB-OTG port is required. A PC and the camera cannot both share the ESP32-S3 native USB port simultaneously.

Midnight registers a read-only vendor PTP operation `0x9B32` when the astro module loads. It returns a fixed 32-byte payload. The PTP host requests it after the Canon session opens. There are no camera motor commands.

Wire payload (all multi-byte integers little-endian):

- `[0:4]` = ASCII `MNT1`
- `[4:8]` = monotonically increasing solution sequence (`uint32`)
- `[8:12]` = camera uptime in milliseconds (`uint32`)
- `[12:16]` = RA, degrees x 10000 (`uint32`)
- `[16:20]` = signed Dec, degrees x 10000 (`int32`)
- `[20:22]` = horizontal field of view, centidegrees (`uint16`)
- `[22:24]` = independently matched star count (`uint16`)
- `[24]` = source: 1=photo LiveView, 2=gallery, 3=synthetic test, 4=Live View guide
- `[25]` = validity: 1 only for a successful verified solution
- `[26:28]` = reserved, zero
- `[28:32]` = camera uptime at PTP readout in milliseconds (`uint32`). Subtract the sample uptime from this value to obtain the true age, including plate-solving time.

On camera startup or before the first successful solution, validity is zero. A previous valid solution may be stale. The camera stores the *capture time*, not the time at which a potentially slow solver completed. The receiver uses readout time minus capture time to reject old data. Poller stores a second clock for time spent since USB receipt. Only source 1 or 4 with total age under 10 seconds is eligible for future closed-loop control. The prototype never starts automatic GoTo from pointing data.

The ESP32's serial `POINT` command is a **test-only** injection and not the PTP transport. USB-host integration and physical camera validation remain necessary.

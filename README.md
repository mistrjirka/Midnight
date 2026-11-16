# Midnight

Astronomy tools for **Magic Lantern on nine Canon EOS cameras**: 100D, 550D, 600D, 650D, 700D, 1100D, 60D, 70D and 6D. Exact Canon firmware versions are in the [installation guide](docs/INSTALL.md).

[Experimental nine-camera release: v0.5.0-alpha.1](https://github.com/mistrjirka/midnight/releases/tag/v0.5.0-alpha.1) · [Installation and recovery guide](docs/INSTALL.md)

## Features

| Feature | Function |
|---|---|
| Plate solving | Identifies sky coordinates without pointing hints. Optional focal-length hints speed up solving |
| Target guiding | Optional: starts unset. Select an M, NGC, IC, Caldwell, Barnard or Sharpless object to show its marker or guiding arrow |
| Polar alignment | Uses two sky solutions about the RA axis, then guides mount adjustment |
| Sky annotations | Star names, constellations, and oriented deep-sky outlines |
| Manual star focus | Size score at 5x/10x Live View |
| Autofocus | Experimental electronic lens adjustments, not hardware validated |
| Live View gain | Photo-only preview gain, Off to +6 EV, without changing saved photos |
| Timed shooting | Original Magic Lantern intervalometer and Bulb Timer links |

Open **Trash/Delete → Focus → Midnight**.

**Target guidance is optional.** New installations start with no target. Choose **Target → Catalogue** and **Number** to activate an arrow or marker. Choose **None** to clear the target. Existing saved selections are preserved.

[Architecture review](docs/ARCHITECTURE_AUDIT.md).

## 600D emulation

![Canon 600D QEMU: 11 matched stars, RA/Dec, sky outlines and guiding arrow](docs/images/midnight-modular-600d-qemu.png)

The screenshot is from **Canon 600D QEMU**, not a physical exposure.
Its bundled synthetic sky solved with **11 matches in 0.19 s of emulator-reported solving time**.
[Testing details and limits](docs/MODULAR_MIGRATION.md).

## Installation

**Choose the installation path for your camera:**

1. **[Fresh camera, without Magic Lantern](docs/INSTALL.md#a-fresh-camera-install-magic-lantern-and-midnight):** prepare a spare SD card, copy the matching **full ZIP**, and run the first-time Magic Lantern installer in the camera.
2. **[Magic Lantern already installed](docs/INSTALL.md#b-magic-lantern-already-installed-add-midnight):** back up your card and copy the matching **module-only ZIP**. Keep your existing Magic Lantern boot files.

[Check supported models, exact Canon firmware versions and download links](docs/INSTALL.md#supported-cameras-and-downloads). Both paths include step-by-step instructions and share [verification, troubleshooting and recovery](docs/INSTALL.md#check-that-midnight-works).

New camera ports are experimental and need on-camera validation. Do not use a full ZIP to update an existing working Magic Lantern card.

## Source

The modular architecture is derived from
[kubakubakuba/AstroML](https://github.com/kubakubakuba/AstroML).
Portable algorithms live in modules/astro/core, camera integration in
modules/astro, and host tests in tests. Original Midnight code is retained in
tests/reference_midnight for regression comparisons.

Build and run tests:

    PYTHON=python3 RUN_SLOW_TESTS=1 bash scripts/build.sh

[Migration status](docs/MODULAR_MIGRATION.md) · [C code review](docs/C_CODE_REVIEW.md) · [Cancellation design](docs/CANCELLATION.md)

**Safety:** The newly added camera ports are compile-tested, not hardware-tested. Gallery solving, Live View, available RAM and lens motors require on-camera checks. Back up the card. Use module-only updates on existing compatible Magic Lantern installations.

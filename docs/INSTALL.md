# Install Midnight

Midnight adds astronomy tools to **Magic Lantern** on supported Canon EOS cameras. It does not replace Canon's firmware.

**Choose the path that matches your camera:**

## Contents

- [A. Fresh camera: install Magic Lantern and Midnight](#a-fresh-camera-install-magic-lantern-and-midnight)
- [B. Magic Lantern already installed: add Midnight](#b-magic-lantern-already-installed-add-midnight)
- [Supported cameras and downloads](#supported-cameras-and-downloads)
- [Check that Midnight works](#check-that-midnight-works)
- [Troubleshooting](#troubleshooting)
- [Restore your previous setup](#restore-your-previous-setup)

> **Experimental software.** The 600D has user-reported camera testing, but the other supported models still need physical-camera validation. Keep a backup. Use a spare card for a first installation. Do not test lens autofocus or unattended mount movement until you have verified your camera.

## Supported cameras and downloads

On your camera, open the **Canon Menu**, find **Firmware Ver.** (under the wrench/settings tabs), and read the number. **Your model and Canon firmware must both match** a row below. If they do not, stop. Do not try a different camera's ZIP.

These files are from [Midnight v0.5.0-alpha.1](https://github.com/mistrjirka/midnight/releases/tag/v0.5.0-alpha.1).

| Camera | Canon firmware | Fresh installation: full ZIP | Existing Magic Lantern: module-only ZIP |
| --- | --- | --- | --- |
| EOS 100D | **1.0.1** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-100D.101.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-100D.101.zip) |
| EOS 550D | **1.0.9** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-550D.109.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-550D.109.zip) |
| EOS 600D | **1.0.2** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-600D.102.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-600D.102.zip) |
| EOS 650D | **1.0.4** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-650D.104.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-650D.104.zip) |
| EOS 700D | **1.1.5** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-700D.115.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-700D.115.zip) |
| EOS 1100D | **1.0.5** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-1100D.105.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-1100D.105.zip) |
| EOS 60D | **1.1.1** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-60D.111.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-60D.111.zip) |
| EOS 70D | **1.1.2** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-70D.112.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-70D.112.zip) |
| EOS 6D | **1.1.6** | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-6D.116.zip) | [Download](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/midnight-module-6D.116.zip) |

**Full ZIP** = Magic Lantern boot files plus Midnight. Use it when preparing a first Magic Lantern installation. **Module-only ZIP** = Midnight and its sky data. Use it when Magic Lantern already works.

## A. Fresh camera: install Magic Lantern and Midnight

Use this path if **Magic Lantern does not already start on your camera**.

### What you need

- A supported camera running the **exact Canon firmware** listed above.
- A **fully charged battery**, a computer and an SD card reader.
- An **empty or spare SD card**. A 32 GB or smaller card is a simple choice for first-time Magic Lantern setup. Copy any photos off it first: formatting erases them.

### Steps

1. **Check your camera model and firmware.** Use the Canon menu and the [table above](#supported-cameras-and-downloads). Stop if they do not match.
2. **Prepare the spare card.** Insert it into the camera and format it using the camera's **Format card** option. Do not format a card that holds photos or a working Magic Lantern installation you want to keep.
3. **Download the full ZIP** for your exact camera from the **Fresh installation** column. Its filename starts with `magiclantern-astro-`. You do not need the module-only ZIP as well.
4. **Copy the files to the card.** Turn the camera off, move the card to your computer, open the downloaded ZIP, and copy **everything inside** to the top level of the card. The card should show `autoexec.bin`, `ML-SETUP.FIR` and an `ML` folder side by side. Do not copy the ZIP itself or put its contents in an extra folder.
5. **Safely eject the card**, insert it into the camera and set the mode dial to **M** (Manual).
6. **Run the Magic Lantern first-time installer.** In the Canon menu, open **Firmware Ver. → Firmware update** (the wording varies by camera). Follow the installer prompts on the camera screen. This step enables the camera to boot Magic Lantern from a prepared card. **Do not turn the camera off or remove the battery while it runs.** Wait for its success message.
7. **Restart the camera** as the installer instructs. Press the **Trash/Delete** button to open Magic Lantern. Go to **Focus → Midnight**.
8. **If Midnight is missing**, open Magic Lantern's **Modules** tab, enable `astro` and restart.

Then [check that Midnight works](#check-that-midnight-works).

**Important:** Copying the full ZIP alone does **not** complete first-time Magic Lantern setup. If you cannot find `ML-SETUP.FIR`, the firmware-update option does not work, or the installer does not report success, **stop** and consult the [Magic Lantern first-time installation guide](https://wiki.magiclantern.fm/install). Do not try another camera's firmware files.

## B. Magic Lantern already installed: add Midnight

Use this path if the camera **already starts Magic Lantern successfully**. It is also the path for updating Midnight.

### What you need

- A working Magic Lantern SD card and a camera with the **matching model and Canon firmware** from the [table above](#supported-cameras-and-downloads).
- A computer and an SD card reader to back up and copy the files.

### Steps

1. **Turn the camera off** and wait until the card activity light goes out. Remove the SD card.
2. **Back up the card** to your computer, including the `ML` folder, `autoexec.bin` and any photos you want to keep.
3. **Download the module-only ZIP** from the **Existing Magic Lantern** column. Its filename starts with `midnight-module-`. **Do not download the full ZIP for this path.**
4. **Copy the ZIP contents to the card.** Open the ZIP and extract everything to the **top level of the SD card**. If asked, replace the existing Midnight module and sky catalogue files. Keep all other Magic Lantern files, including your original `autoexec.bin`.
5. **Safely eject the card**, put it back in the camera and turn the camera on.
6. Press **Trash/Delete → Focus → Midnight**. If it is missing, enable `astro` in Magic Lantern's **Modules** tab and restart.

Then [check that Midnight works](#check-that-midnight-works).

This update does **not** change Canon firmware, Magic Lantern boot files or the card's boot settings.

## Check that Midnight works

1. **Open the Midnight menu:** press **Trash/Delete → Focus → Midnight**.
2. **Test the sky solver without taking a photo:** open **Developer → Solve image file**, with `TEST.PGM` selected. It should display an annotated sky result. This checks the installed module and sky files.
3. **Test an existing photo:** display a photo full-screen using Canon's **Play** button. Press **Q**, then **SET** to ask Midnight to solve it. On the **EOS 100D**, press **Av / exposure compensation**, then **SET** instead. A suitable star-field photo should show sky coordinates and labels. The original photograph is not changed.
4. **Test Live View:** in **photo mode**, use **1× Live View**, then select **Solve Live View (3s)**. The three-second delay is for stabilization, not an exposure. The solver cannot use 5× or 10× Live View.
5. **Choose a target only if you want guidance:** Midnight starts with **Target → None**. Select a catalogue and object number to show a marker or arrow. Set it back to **None** to clear it.

If a solve fails, open **Last solve → Status** to see the reason. A first-time failure on a new camera model is useful hardware-test feedback, not proof that the installation itself failed.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| Magic Lantern does not start after a fresh installation | Check that the first-time installer **finished successfully**, the firmware matches, and `autoexec.bin` and `ML` are at the card's top level. See the [official installation guide](https://wiki.magiclantern.fm/install). |
| Camera hangs or screen stays black | Turn it off. If needed, remove the battery, then **remove the SD card** and restart without it. Do not keep retrying a mismatched build. |
| Midnight is not in **Focus** | Open Magic Lantern **Modules**, enable `astro`, restart, and check `ML/modules/astro.mo` exists on the card. |
| `SKY index missing/corrupt` | Extract the complete ZIP for your camera again. Keep all `ML/ASTRO` files from the same Midnight release. |
| `Use 1x Live View` | Turn off 5×/10× zoom and use **1× photo Live View**. |
| Gallery shortcut shows no result | Use full-screen **Play**. On 100D, use **Av → SET**. Check **Last solve → Status**. |
| Module or memory error | Disable other Magic Lantern modules and restart. New camera ports may lack enough free memory. |
| SD card reader disconnects or files disappear | Stop writing. Try a different reader or port and check the card for errors first. |

## Restore your previous setup

**After a module-only update:**

1. Turn off the camera and remove the card.
2. Copy your backed-up `ML/modules/astro.mo` and the full `ML/ASTRO` folder back onto the card. Restore `ML/SETTINGS` if needed.
3. Safely eject the card and restart the camera. You do not normally need to change `autoexec.bin`.

**After a fresh installation:** use your original spare/backup card to return to a known working setup. To remove Magic Lantern's **camera boot setting**, use the official [Magic Lantern uninstall instructions](https://wiki.magiclantern.fm/install). **Deleting files alone is not the same as running the uninstall procedure.**

## Optional files and model-specific notes

- [Wide-field sky index](https://github.com/mistrjirka/midnight/releases/download/v0.5.0-alpha.1/magiclantern-astro-wide-index.zip): extra sky data for fields wider than about **60°**. Most telescope users do not need it. Extract it to the card root if needed.
- [100D, 650D, 60D, 1100D and 70D testing notes](EASY_CAMERA_PORTS.md)
- [550D testing notes](550D_PORT.md) and [700D testing notes](700D_PORT.md)

Midnight is based in part on [AstroML](https://github.com/kubakubakuba/AstroML).
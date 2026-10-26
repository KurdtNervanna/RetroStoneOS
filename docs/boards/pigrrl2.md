# Adafruit PiGRRL 2

The [PiGRRL 2](https://learn.adafruit.com/pigrrl-2) is Adafruit's DIY handheld: a Raspberry Pi 2 or 3, a 2.8"
PiTFT (ILI9341 display on SPI, STMPE610 resistive touch), 12 buttons on the Pi's GPIOs and a small speaker
amplifier on the Pi's analog audio. RetroStoneOS runs on it with the normal Raspberry Pi image and a few lines in
`config.txt`: there is no separate PiGRRL 2 image.

**Status: first test on a PiGRRL 2 done** (issue #6, 0.1.4 on a Pi 3): the PiTFT picture and the sound work. Up/down,
L/Select and R/Start were swapped; 0.1.5 fixes them (the table below), and shrinks the boot logo to fit the
320x240 screen (0.1.4 showed only its middle). The PiTFT side buttons are not mapped. Reports are welcome.

## Setup

1. Flash the Raspberry Pi image: `rpi2` (Pi 2, or a Pi 3 in 32-bit mode) or `rpi3-64` (Pi 3), version 0.1.4 or
   later (docs/boards.md).
2. Open the `RSOS-BOOT` drive of the card on a PC and edit `config.txt`:
   - change the line `dtoverlay=vc4-kms-v3d` to `dtoverlay=vc4-kms-v3d,nohdmi`;
   - at the end of the file, remove the `#` in front of the three PiGRRL 2 lines:

     ```
     dtoverlay=pitft28-resistive,drm,rotate=90,speed=62500000
     dtoverlay=pigrrl2-buttons
     dtparam=audio=on
     ```
3. Put the card in the PiGRRL 2 and switch it on. The menu shows up on the PiTFT.

To go back to HDMI, undo the two edits.

## What each line does

| Line | Effect |
|---|---|
| `vc4-kms-v3d,nohdmi` | The Pi's own display driver (vc4) keeps the GPU (N64 cores) but has no HDMI output. The frontend opens the first DRM device that has a connector: with HDMI off, that is the PiTFT. The frontend drives only one screen, so HDMI and the PiTFT cannot be used together. |
| `pitft28-resistive,drm` | The PiTFT on SPI0 (CE0, D/C on GPIO 25) through the kernel's ILI9341 DRM driver (`drm` selects it instead of the old fbtft framebuffer). It registers an `SPI-1` connector, which the board profile treats as the built-in screen (`internal_display = spi`). `rotate=90` gives a landscape 320x240 picture, the RetroStone2's own resolution. The backlight is always on (a GPIO of the STMPE610). The touchscreen is not used. |
| `pigrrl2-buttons` | RetroStoneOS's overlay: the 12 buttons as one gamepad named "PiGRRL 2 buttons" (gpio-keys, Linux gamepad codes). The board profile makes it the built-in pad (`builtin_pad_prefix = PiGRRL`), so it is player 1 and its hotkeys always work. |
| `dtparam=audio=on` | The Pi's analog audio (3.5 mm jack / PWM pins), where the PiGRRL 2 amplifier is wired. A board hook loads `snd-bcm2835` once the menu is up; the game sound goes to it because the built-in screen is in use. |

### The picture

- **Upside down**: `rotate=270` instead of `rotate=90`.
- **Garbage or stripes**: lower the SPI clock, `speed=32000000`. The Pi divides its core clock, so 62.5 MHz is the
  fastest useful value.
- **Frame rate**: SPI limits it. A full 320x240 16-bit frame is 1.23 Mbit, so at most about 50 frames/s at
  62.5 MHz and 26 at 32 MHz: 60 Hz games will drop frames. The ILI9341 DRM mode has no real refresh rate, so the
  game host should pace on the audio clock rather than on the display (untested).

## Buttons

Defaults of `pigrrl2-buttons`, BCM GPIO numbers, as a PiGRRL 2 tester reported them (issue #6; 0.1.4 had up/down,
L/Select and R/Start swapped). Each button
connects its GPIO to ground (active low, internal pull-up).

| Button | GPIO | Parameter | Linux code |
|---|---|---|---|
| D-pad up | 16 | `up` | `BTN_DPAD_UP` |
| D-pad down | 26 | `down` | `BTN_DPAD_DOWN` |
| D-pad left | 4 | `left` | `BTN_DPAD_LEFT` |
| D-pad right | 19 | `right` | `BTN_DPAD_RIGHT` |
| A | 14 | `a` | `BTN_EAST` |
| B | 15 | `b` | `BTN_SOUTH` |
| X | 20 | `x` | `BTN_NORTH` |
| Y | 18 | `y` | `BTN_WEST` |
| L | 12 | `l` | `BTN_TL` |
| R | 13 | `r` | `BTN_TR` |
| Select | 5 | `select` | `BTN_SELECT` |
| Start | 6 | `start` | `BTN_START` |

If a button does the wrong thing, give it its real GPIO, for example:

```
dtoverlay=pigrrl2-buttons,a=15,b=14
```

GPIO 14/15 are also the serial console (`ttyAMA0`); with the buttons there, the console is not usable. The
PiTFT itself uses GPIO 7-11 (SPI0), 24 (touch interrupt) and 25 (D/C); its four side buttons (GPIO 17, 22, 23,
27 on PiTFTs that have them) are not mapped.

Hotkeys are the usual ones (README.md): **Select+Start** exits a game, **Select+X** opens the in-game menu,
**Select+R/L** save/load a state.

## Why a `config.txt` block and not a PiGRRL 2 image

The Raspberry Pi images are built by the CI for each board (docs/ci.md). A PiGRRL 2 image would be one more 1 GB
build per release for a handful of users, with the same kernel and root file system as the Pi 2/3 images. Instead
those images carry everything the PiGRRL 2 needs, all of it idle until `config.txt` turns it on:

- the kernel has the SPI controller, the ILI9341 DRM driver and the GPIO backlight built in (no udev: modules would
  never load), `board/rpi2/linux.fragment` and `board/rpi3/linux.fragment`;
- `pigrrl2-buttons.dtbo` is built with the kernel (`board/rpi2/pigrrl2-buttons.dtso`,
  `BR2_LINUX_KERNEL_CUSTOM_DTS_PATH`) and copied to `overlays/` by `board/rpi4/post-image.sh`;
- `board.ini` names the SPI screen and the PiGRRL 2 buttons, which match nothing on a Pi without them.

A dedicated image would only be worth it if the PiGRRL 2 needed different packages or a different board profile.

## What to send back

From the `RETROSTONE` drive of the card after a boot: `rsos/logs/` (the newest `boot<N>` folder, or `last.txt`).
The useful lines:

- the frontend log: the display lines (which DRM card and connector it opened, the mode) and the `input:` line of
  "PiGRRL 2 buttons" (it should end in `built-in`);
- `dmesg.txt`: the `ili9341`, `spi`, `gpio-keys` and `vc4` lines.

And: which buttons did what (or nothing), whether the picture was right side up and smooth, and whether there was
sound.

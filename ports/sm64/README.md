# Super Mario 64 on the RetroStone2 (native, built from your own ROM)

This folder builds [sm64-port](https://github.com/sm64-port/sm64-port) (the PC port of the Super Mario 64
decompilation) for RetroStoneOS, where it runs as a **native game**: ARM code with the Mali-400 GPU (GLES2), no N64
emulator (docs/native-games.md).

**Nothing from the game is in this repository, in the OS images or in the System updates.** The build takes the
game's data from **your own ROM**, on your PC, and the result is yours alone: copy it to your own SD card, never
publish it (not in a release, an update, a fork or a download link).

## What you need

* A PC with Linux or WSL (Ubuntu), the usual build tools (`build-essential`, `git`, `python3`) and a built
  RetroStoneOS tree for the RetroStone2 with SDL2 (this fork's `retrostone2_release_defconfig`; `~/rsos/output`
  or `RSOS_OUTPUT=<dir>`).
* Your Super Mario 64 ROM, **US** version, `.z64` byte order (SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`).
* RetroStoneOS 0.1.4.17 (build 17) or newer of this fork on the console: it has "Native games" and SDL2.

## Build

```sh
ports/sm64/build-sm64.sh ~/baserom.us.z64 ~/sm64-retrostone2
```

It clones sm64-port at a fixed commit into `~/rsos/sm64-work`, applies `retrostone2.patch` and adds
`controller_rsos.c`, builds the asset tools for the PC and the game for the console (`TARGET_LINUX_KMSDRM=1`,
the RetroStoneOS cross compiler), and writes:

```
~/sm64-retrostone2/roms/native/Super Mario 64.sh
~/sm64-retrostone2/roms/native/sm64/sm64.us
```

## Install

Put the console's SD card in the PC and copy the `roms` folder onto the **RETROSTONE** partition (it merges with
the `roms` folder already there), or use the menu's USB import. In the menu: **Native games > Super Mario 64**.

## Controls

| RetroStone2 | N64 |
|---|---|
| A / B | A / B |
| Start | Start |
| L1, L2, Z | Z |
| R1 | R |
| X / Y / C / R2 | C-up / C-left / C-right / C-down |
| D-pad, or the analog stick (Pro) | the stick |

**Select + Start** held for a second goes back to the menu. A USB pad works too (SDL; A = A, X = B, the
shoulders = Z/R, the right stick = C buttons).

Saves (`sm64_save_file.bin`) and settings (`sm64config.txt`) stay in `roms/native/sm64/`. The game saves on its
own (each star); quitting or the power key never loses a saved star.

## What the patch changes in sm64-port

* `TARGET_LINUX_KMSDRM=1`: SDL2 on KMS/DRM with a GLES2 context (`#version 100` shaders with
  `precision mediump float`), ALSA, no X11/PulseAudio/Wii U adapter, no `-march=native`; `CROSS_PC` (the
  compiler of the game only, the asset tools keep the PC's) and `SDL2_CONFIG`.
* `controller_rsos.c`: the console's three input devices (buttons, shoulder buttons, analog stick) read
  together from `/dev/input`; `controller_sdl` leaves them alone and keeps USB pads.
* `tools/armips.cpp`: `<cstdint>` for newer host compilers.

## Status

Built and checked on the PC (an ARM program linked against the image's SDL2, EGL, GLES2 and ALSA); not yet run on a
RetroStone2. The speed on the A20 (2 x Cortex-A7, Mali-400 MP2) is unknown: report it, with
`rsos/logs/boot<N>/game.log` from the SD card if something goes wrong.

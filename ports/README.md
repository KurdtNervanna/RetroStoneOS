# Native ports for the RetroStone2

PC ports of N64 games (and other open-source engines) can run on the RetroStone2 **as native games**: ARM programs
that draw with the console's GPU, with no emulator in between. They are much faster and look much better than
emulating the N64: Super Mario 64 runs at full speed on the RetroStone2.

**This fork never ships any game.** Each port is built **on your PC, from your own ROM**, and the result goes onto
your own SD card. Keep it to yourself: never upload it, share it or put it in a release.

## How to get a native port (step by step)

1. **Update the console** to build 17 or newer of this fork (Settings > System update, or flash the image from
   [Releases](https://github.com/KurdtNervanna/RetroStoneOS/releases)). It adds **Native games** to the menu and
   the libraries the ports need (SDL2, GLES2 and ALSA).
2. **Set up the build on a PC** with Linux or Windows + WSL (Ubuntu):
   * the usual tools: `sudo apt install build-essential git python3`;
   * this fork, built once for the RetroStone2: it provides the compiler for the console and its libraries.
     Follow [docs/build.md](../docs/build.md) with `retrostone2_release_defconfig` (the output in
     `~/rsos/output`). A port built once usually keeps working after the console's System updates.
3. **Build the port** with its script, giving it your ROM (each port's README says which ROM version):
   ```sh
   ports/sm64/build-sm64.sh ~/baserom.us.z64 ~/my-ports
   ```
4. **Copy** the `roms` folder from the output (`~/my-ports/roms`) onto the SD card's **RETROSTONE** partition
   (it merges with the `roms` folder there). From WSL, Windows sees it at `\\wsl$\Ubuntu\home\<you>\my-ports`.
5. In the menu: **Native games** > the game. **Hold Select + Start** for a second to go back to the menu.

## Available ports

| Game | Folder | Port | Status on the RetroStone2 |
|---|---|---|---|
| Super Mario 64 | [sm64](sm64/README.md) | [sm64-port](https://github.com/sm64-port/sm64-port) | **Works**: full speed, built-in controls, analog stick (Pro) or D-pad |

## Other N64 PC ports: can they run here?

The RetroStone2's GPU is a **Mali-400 MP2: OpenGL ES 2.0 only** (Mesa's lima driver), with 1 GB of RAM and two
Cortex-A7 cores at 960-1056 MHz. That decides which ports can work:

| Port family | Games | GPU it needs | Here |
|---|---|---|---|
| sm64-port's renderer (Fast3D, GLSL 1.10 / ES 1.00) | Super Mario 64 (sm64-port, sm64ex) | OpenGL 2.1 / **ES 2.0** | **Yes** (Super Mario 64 works) |
| [libultraship](https://github.com/Kenix3/libultraship) (HarbourMasters) | Star Fox 64 ([Starship](https://github.com/HarbourMasters/Starship)), Ocarina of Time ([Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)), Majora's Mask ([2Ship2Harkinian](https://github.com/HarbourMasters/2ship2harkinian)), Mario Kart 64 ([SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart)), [Ghostship](https://github.com/HarbourMasters/Ghostship), Banjo-Kazooie ([Lighthouse](https://github.com/HarbourMasters/Lighthouse)), Paper Mario ([PaperBoat](https://github.com/HarbourMasters/PaperBoat)) | OpenGL 3.0 (GLSL 1.30) or **ES 3.0**, and SDL3 | **Not as they are**: they need a GLES2 renderer and SDL3 (see below) |
| [Perfect Dark port](https://github.com/fgsfdsfgs/perfect_dark) | Perfect Dark | OpenGL 3.0 / ES 3.0 | Not as it is (the same GLES2 work) |
| N64 Recompiled (RT64) | Zelda 64 Recomp, Banjo: Recompiled, DK64, Star Fox 64 Recomp, ... | Vulkan / Direct3D 12 | **No**: the Mali-400 has neither |

### The way to Star Fox 64, Zelda and the other libultraship ports

libultraship's OpenGL renderer has three shader variants: desktop GL 4.1, desktop GLSL 1.30 and **GLES 3.0**. A
**GLES 2.0 variant** (GLSL ES 1.00, as sm64-port uses) is the missing piece: the shaders use a few GLSL 1.30
features (`textureSize()`, `in`/`out`, `texture()`) that have GLES2 equivalents (a size uniform, `attribute`/
`varying`, `texture2D()`). SDL3 would be added to RetroStoneOS next to SDL2. Then each game needs its own memory
and speed check: **Star Fox 64 (Starship)** is the best first candidate (a lighter game, and its assets are
already well understood here); Ocarina of Time and Majora's Mask are much heavier for 1 GB of RAM and two
Cortex-A7 cores. Work in progress: nothing to build yet.

## Making a new port

A native game is any ARM program with a launch script in `roms/native/` (docs/native-games.md). For an SDL2 port:
build it with the RetroStoneOS cross compiler (`~/rsos/output/host/bin/arm-linux-`) against the staging
libraries (`~/rsos/output/staging`), use GLES2 for 3D, and read the built-in controls from `/dev/input` (three
devices: "RetroStone2 Buttons", "RetroStone2 Shoulder Buttons" and "analog-stick"; `sm64/controller_rsos.c` is
an example). Put the game's code changes in a patch next to a build script, never the game's data.

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
| Star Fox 64 | [starship](starship/README.md) | [Starship](https://github.com/HarbourMasters/Starship) + a GLES2 libultraship | **Work in progress**: builds for the RetroStone2, not run yet |

## Other N64 PC ports: can they run here?

The RetroStone2's GPU is a **Mali-400 MP2: OpenGL ES 2.0 only** (Mesa's lima driver), with 1 GB of RAM and two
Cortex-A7 cores at 960-1056 MHz. That decides which ports can work:

| Port family | Games | GPU it needs | Here |
|---|---|---|---|
| sm64-port's renderer (Fast3D, GLSL 1.10 / ES 1.00) | Super Mario 64 (sm64-port, sm64ex) | OpenGL 2.1 / **ES 2.0** | **Yes** (Super Mario 64 works) |
| [libultraship](https://github.com/Kenix3/libultraship) (HarbourMasters) | Star Fox 64 ([Starship](https://github.com/HarbourMasters/Starship)), Ocarina of Time ([Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)), Majora's Mask ([2Ship2Harkinian](https://github.com/HarbourMasters/2ship2harkinian)), Mario Kart 64 ([SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart)), [Ghostship](https://github.com/HarbourMasters/Ghostship), Banjo-Kazooie ([Lighthouse](https://github.com/HarbourMasters/Lighthouse)), Paper Mario ([PaperBoat](https://github.com/HarbourMasters/PaperBoat)) | OpenGL 3.0 (GLSL 1.30) or **ES 3.0**, and SDL3 | **In progress**: this fork adds a GLES2 mode to libultraship (Star Fox 64 first); the newest libultraship also moved to SDL3 |
| [Perfect Dark port](https://github.com/fgsfdsfgs/perfect_dark) | Perfect Dark | OpenGL 3.0 / ES 3.0 | Not as it is (the same GLES2 work) |
| N64 Recompiled (RT64) | Zelda 64 Recomp, Banjo: Recompiled, DK64, Star Fox 64 Recomp, ... | Vulkan / Direct3D 12 | **No**: the Mali-400 has neither |

### The way to Star Fox 64, Zelda and the other libultraship ports

libultraship's OpenGL renderer has shader variants for desktop GL and **GLES 3.0**; this fork adds a **GLES 2.0**
mode (`USE_GLES2`, [starship/libultraship-gles2.patch](starship/libultraship-gles2.patch)): GLSL ES 1.00 shaders,
GLES2 framebuffers and depth buffers, no multisampling. **Star Fox 64 (Starship)** is first: it builds for the
RetroStone2 and its data comes from your ROM ([starship/README.md](starship/README.md)). Its libultraship still uses
SDL2, so no SDL3 is needed for it. Ocarina of Time and Majora's Mask are much heavier for 1 GB of RAM and two
Cortex-A7 cores, and the newest libultraship moved to SDL3: they come after Star Fox 64 runs.

## Making a new port

A native game is any ARM program with a launch script in `roms/native/` (docs/native-games.md). For an SDL2 port:
build it with the RetroStoneOS cross compiler (`~/rsos/output/host/bin/arm-linux-`) against the staging
libraries (`~/rsos/output/staging`), use GLES2 for 3D, and read the built-in controls from `/dev/input` (three
devices: "RetroStone2 Buttons", "RetroStone2 Shoulder Buttons" and "analog-stick"; `sm64/controller_rsos.c` is
an example). Put the game's code changes in a patch next to a build script, never the game's data.

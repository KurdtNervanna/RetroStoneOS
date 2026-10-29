# Mario Kart 64 (SpaghettiKart) on the RetroStone2

[SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart) is the PC port of the Mario Kart 64
decompilation, built on HarbourMasters' [libultraship](https://github.com/Kenix3/libultraship), like Star Fox 64
and Ocarina of Time ([../starship](../starship/README.md), [../soh](../soh/README.md)). This folder adds the same
OpenGL ES 2.0 mode to SpaghettiKart's libultraship and builds the game for the console as a native game
(docs/native-games.md).

**Nothing from the game is in this repository, the OS images or the updates.** The game's data comes from **your
own ROM** on your PC and stays yours. SpaghettiKart takes one ROM only: **Mario Kart 64 (US)**, SHA-1
`579c48e211ae952530ffc8738709f078d5dd215e` in `.z64` order (`.v64` and `.n64` dumps are converted). A modified
dump (`[h]`, `[t]`, `[f]` in its name) has another checksum and is refused.

## Build

```sh
ports/spaghettikart/build-spaghettikart.sh ~/"Mario Kart 64 (U).z64" ~/my-ports
```

It fetches SpaghettiKart at a fixed commit into `~/rsos/spaghettikart-work`, applies `spaghettikart.patch` and
`libultraship-gles2.patch`, builds the game for the console (`USE_GLES2`, the RetroStoneOS cross compiler) and
Torch for the PC, which makes `mk64.o2r` from the ROM and `spaghetti.o2r` (with the patched shaders), then
writes:

```
~/my-ports/roms/native/Mario Kart 64.sh
~/my-ports/roms/native/spaghettikart/   Spaghettify, mk64.o2r, spaghetti.o2r, config.yml, yamls/, meta/
```

Copy the `roms` folder onto the SD card's **RETROSTONE** partition; in the menu: **Native games > Mario Kart
64**. Settings and saves stay in `roms/native/spaghettikart/` (`SHIP_HOME`). Hold **Select + Start** for a
second to go back to the menu. The console needs a RetroStoneOS with Native games, the port libraries and
`rsos-gamepad`.

## Controls

SpaghettiKart's own defaults, through `rsos-gamepad` (by position), by the labels on the console:

| RetroStone2 | N64 |
|---|---|
| B (bottom) | A (accelerate) |
| Y (left) | B (brake) |
| L2 | Z (item) |
| R1 or R2 | R (hop, drift) |
| L1 | L |
| X (top) / A (right) | C-left / C-down |
| Start | Start |
| Analog stick (Pro), D-pad | steering, the D-pad |

## What the patches change

* **`libultraship-gles2.patch`**: Ship of Harkinian's GLES2 patch ([../soh](../soh/README.md): the OpenGL ES 2.0
  mode, the 32-bit ARM and Mali-400 fixes, the speed work), carried over to SpaghettiKart's libultraship
  (`60c9bb7`, 25 commits before Ship of Harkinian's). This older libultraship dropped every texture address
  below `0x10000000` on Linux, which on 32-bit ARM is the heap: the same fix applies. Also backported from newer
  libultraship (`766a6bad`): SIGTERM no longer calls `exit()` inside the signal handler, whose destructors
  deadlocked. The menu quits a native game with SIGTERM, so every quit hung until the menu killed the game; it
  now quits at once.
* **`spaghettikart.patch`**: the GLES2 changes in SpaghettiKart's own copy of the shaders (packed into
  `spaghetti.o2r`).

## Status

**Builds** for the RetroStone2. A desktop build of the same GLES2 code runs the intro, the title screen and the
attract-mode race correctly; not run on the console yet. Building `mk64.o2r` logs one "Failed to process" per track
for its last packed display list: an empty end marker that nothing uses.

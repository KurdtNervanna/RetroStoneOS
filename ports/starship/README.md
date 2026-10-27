# Star Fox 64 (Starship) on the RetroStone2: work in progress

[Starship](https://github.com/HarbourMasters/Starship) is the PC port of the Star Fox 64 decompilation, built on
HarbourMasters' [libultraship](https://github.com/Kenix3/libultraship). libultraship draws with OpenGL 3.0 or
OpenGL ES 3.0; the RetroStone2's Mali-400 only has **OpenGL ES 2.0**. This folder adds an OpenGL ES 2.0 mode to
libultraship (`USE_GLES2`) and builds Starship for the console as a native game (docs/native-games.md).

**Nothing from the game is in this repository, the OS images or the updates.** As for Super Mario 64, the game's
data comes from **your own ROM** on your PC (Star Fox 64 US 1.1, `.z64`, SHA-1
`09f0d105f476b00efa5303a3ebc42e60a7753b7a`) and stays yours.

## Status

| Step | |
|---|---|
| GLES2 renderer (`libultraship-gles2.patch`) | written |
| Cross-build for the RetroStone2 (32-bit ARM) | **builds** (Starship `3322643f`, libultraship `eaaf9d0`, Torch `cd92cc0`) |
| Game data from the ROM (`sf64.o2r`) and the port archive (`starship.o2r`) | **built** on a PC |
| Libraries in the OS (Ogg/Vorbis, libzip, tinyxml2, spdlog, fmt) | in the configs, from RetroStoneOS build 18 |
| The built-in controls | to do: they are three devices, and libultraship reads SDL game controllers |
| Build script for the SD card folder | to do |
| Run on a RetroStone2 | not yet |

Starship's libultraship still uses **SDL2** (only libultraship's newest code moved to SDL3), so no SDL3 is needed.

## What the GLES2 mode changes

* **Context**: asks SDL for an OpenGL ES 2.0 context (Mesa's lima would otherwise give desktop OpenGL 2.1).
* **Shaders**: GLSL ES 1.00 (`attribute`/`varying`, `texture2D`, `gl_FragColor`, `precision mediump float`);
  `textureSize()` replaced by the texture's size for masks; `mix(..., bvec3)` written with `vec3()`; the noise
  constant kept below 65504 (Mali-400 fragment shaders have 16-bit floats only). Both copies of the shader
  templates: libultraship's and Starship's own `port/shaders` (packed into `starship.o2r`).
* **Framebuffers**: unsized texture formats, packed depth+stencil when the driver has
  `GL_OES_packed_depth_stencil` (else 16-bit depth), clamp-to-edge on the screen-sized colour textures (GLES2 has
  no repeat on non-power-of-two textures), no multisampling, no vertex array object.
* **What GLES2 cannot do**: framebuffer blits (copies go through `glCopyTexSubImage2D`, unscaled only) and depth
  reads (reported as "far"). Starship uses neither; only libultraship's debug window does.
* A shader that fails to compile now prints the compiler's message and the shader before aborting.

## Building (for now, by hand)

```sh
git clone --recurse-submodules https://github.com/HarbourMasters/Starship && cd Starship
git checkout 3322643f && git submodule update --init --recursive
git apply <fork>/ports/starship/starship.patch
git -C libultraship apply <fork>/ports/starship/libultraship-gles2.patch
cmake -S . -B build-gles2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OPENGLES=ON -DUSE_GLES2=ON \
  -DCMAKE_TOOLCHAIN_FILE=~/rsos/output/host/share/buildroot/toolchainfile.cmake
cmake --build build-gles2 --target Starship
cp <your rom> baserom.z64
cmake --build build-gles2 --target ExtractAssets GeneratePortO2R
```

A `build-starship.sh` like `ports/sm64/build-sm64.sh` will replace these steps.

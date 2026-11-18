# This fork vs. the official RetroStoneOS

This is a fork of the official RetroStoneOS ([PaddleStroke/RetroStoneOS](https://github.com/PaddleStroke/RetroStoneOS)).
The base is the same: the same kernel, the same menu, the same emulators. The fork adds a few things on top, mostly for
the **RetroStone2**. This page says what is different, what has been offered back to the official project, and how
to switch between the two.

**Which one should I use?** If you want the stable, official system, use the official image. If you want the extra
features below (native N64 ports above all), use the fork. Both run on the same RetroStone2.

## What the fork adds

| Feature | Official | This fork |
|---|---|---|
| Emulated systems, menu, themes, save states, HDMI switching | yes | yes (same code) |
| Bluetooth controllers (0.1.5) | yes | yes (same code) |
| **Ports** list (Doom, Cave Story and native games in one menu list) | no | yes |
| **Native N64 ports**: Super Mario 64, Ocarina of Time, Majora's Mask, Star Fox 64, Mario Kart 64, GoldenEye 007 (also Perfect Dark and Smash 64: see [ports/README.md](ports/README.md) for the current status of each) | no | yes: [how to get them](ports/README.md) |
| Native ports follow HDMI live, N64 pad layouts per port | no | yes |
| **Game store**: Settings > Game store installs a port made from *your own ROM* ([docs/store-ports.md](docs/store-ports.md)) | no | yes |
| CPU speed setting (1008 / 1056 MHz, with a shutdown-confirmed trial) | no | yes |
| Game covers: title screens, smarter matching, *Choose cover* | basic covers | extended |
| WiFi network chosen from a list; addresses shown next to the switches | no | yes |
| Controller set-up: known pads, pad types, N64 profile, wizard | basic | extended |
| Over-the-air updates | official releases | this fork's releases (own signing key) |

No game is ever included. Native ports are built on a PC from your own ROM, or made on the console from a ROM you
already have, and they stay on your own SD card.

## What has been offered back to the official project

The fork tries to stay close to the official code. Work that fits the official project is sent there as pull requests,
so the fork has less and less to carry.

| Change | Official PR | Status |
|---|---|---|
| Native games: starting a program built for the console, without an emulator | [#14](https://github.com/PaddleStroke/RetroStoneOS/pull/14) | open, under discussion |
| `rsos-gamepad`: the built-in controls as one SDL gamepad | [#15](https://github.com/PaddleStroke/RetroStoneOS/pull/15) | open, builds on #14 |
| The native game ports | [#16](https://github.com/PaddleStroke/RetroStoneOS/pull/16) | open, builds on #15 |
| Game store, CPU speed, extended covers, WiFi list, and the rest of the table above | not submitted | fork only for now |

When something is merged upstream, it leaves this page's "fork only" list and comes to the fork with the next sync.

## Installing, updating and switching

- **Fork builds** are on this repository's [Releases](https://github.com/KurdtNervanna/RetroStoneOS/releases), named like
  `0.1.5 pro-storage build 30`. An installed fork updates over the air from these releases (Settings > System update).
- **Official builds** update from the official releases. The two projects sign their updates with different keys, so an
  installed system only accepts updates from the project it came from.
- **To switch** either way, flash the other project's image to the SD card. Games and saves on the `RETROSTONE`
  partition are normal files: copy them somewhere safe first (Settings > *Back up saves*, or from a PC).
- **Boards:** the fork's extra features target the **RetroStone2** and **RetroStone2 Pro**. Other boards are not
  tested with the fork's additions.

## How the fork is kept in sync

- `main` is a plain copy of the official `main`. Nothing is committed to it.
- Each feature lives on its own branch off `main`, so it can be sent upstream as a pull request.
- `pro-storage` is the branch the fork's releases are built from: all the feature branches merged together.
- The official `main` is merged into `pro-storage` after each official change, so the fork stays on the current
  official base.
- Most fork-only code is in new files (`ports/`, the store scripts, `docs/store-ports.md`) so it does not collide with
  official changes.

## Reporting problems

If you use a fork build, report problems on [this repository's issues](https://github.com/KurdtNervanna/RetroStoneOS/issues),
so the official maintainer is not asked to support code that is not theirs. If the problem also happens on the official
image, say so and it can go upstream.

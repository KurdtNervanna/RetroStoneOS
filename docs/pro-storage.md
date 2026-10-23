# RetroStone2 "Pro" storage, game covers, multi-disc games

The Pro variant has a 16 GB eMMC (U23, mmc2, 4-bit) and an M.2 M-key slot (J1) for a **SATA** SSD (the A20
has no PCIe: NVMe drives do not work). This page describes how RetroStoneOS uses them, and two game list
features that came with this work: box covers from the libretro thumbnail server, and grouping multi-disc games.

## Regular or Pro

The first start of a card flashed with RetroStoneOS 0.1.4 or later finds out which RetroStone2 it runs on.
`boot.cmd` sees no `rsos_model` in the U-Boot environment, so it probes U-Boot's `mmc 1` (the eMMC U23) once and
saves `rsos_model=pro` or `regular`. **That is all it changes**: `rsos_overlays` stays as the user set it. The eMMC
and the M.2 slot stay **off by default** on both models, so that every RetroStone2 starts as fast as before; on a
Pro the user turns them on in **Settings > Storage** (**Internal eMMC**, **M.2 SATA drive**, then a restart).
A regular unit's Settings > Storage hides the Internal storage section; the M.2 section stays.

The probe needs this system's U-Boot (its bootcmd sets `rsos_sysdev`). A card flashed with an older version and
updated over the air keeps its old U-Boot, which has no `mmc 1`: there the probe does not run, `rsos_model`
stays empty (Model: "found at the next start") and every storage setting stays available. The Pro storage
features need a freshly flashed 0.1.4 card.

Settings > Storage shows **This console > Model** (also in System information) and **Detect the model again**
(`rsos-storage redetect` clears `rsos_model`; the next start probes again). A unit that runs from the eMMC or
through a starter card, or whose eMMC answers in Linux, counts as a Pro whatever `rsos_model` says. The probe
costs one failed MMC initialisation on a regular unit, once (TODO(hw): measure it, bootstage of the first two
starts against 0.1.3). `boot-ab-qemu-test.sh` covers both models and the old bootcmd (cases 14 to 16).

## What the player sees

**Settings > Storage**, started from the microSD:

| Item | What it does |
|---|---|
| Internal eMMC (toggle) | `rsos_overlays` in the U-Boot environment; the eMMC appears after a restart ("not fitted" on a unit without one) |
| Install to internal storage | copies RetroStoneOS to the eMMC, **with or without the games**, then offers: Use as starter card / **Keep SD as it is** (preselected; the starter card asks again, CANCEL first) |
| Update internal storage | the same when the eMMC already holds RetroStoneOS: keep its games and saves, or erase it |
| Use this SD card as starter card | when the eMMC holds RetroStoneOS (see "Starter card") |
| M.2 SATA drive (toggle) | the SATA controller (overlay `sata`); any unit with the slot fitted |
| Erase it and use it for games | the M.2 drive becomes a game disk (one exFAT partition, `roms/<system>/`) |

Running from the eMMC: the card in the slot is shown with what it holds; a starter card says "Keep it in"
and offers nothing that could erase it. Another RetroStoneOS card can become a games card, any other card can be
erased for games.

The games of every mounted game disk (the eMMC or microSD the system does not run from, the M.2 drive) are
merged into the game lists. **Import games** gets **Copy games to** (the system disk or a game disk; another disk
receives only the games, BIOS files, saves and themes stay on the system disk). **Move games between drives**
copies the games of one disk to another and, with "Delete from ... after copying", deletes them from the source
once everything is flushed, and only after a run that left nothing behind: if a file failed, a differing file was
kept or the copy was cancelled, nothing is deleted. `gamelist.xml`, links and a file that is its own copy (same
inode) are never deleted, and a folder with a file whose bytes differ keeps all its files (a `.cue` and its
`.bin` stay together).

**Settings > Game lists**: **Download game covers** and **Group multi-disc games** (below).
**System information > Credits** lists the contributors from the git history
(`scripts/support/update-credits.sh` writes `frontend/assets/credits.txt`).

## The eMMC and the A20 boot ROM (why a starter card)

The A20 boot ROM tries microSD, NAND, eMMC (mmc2), SPI NOR, then USB FEL. On the tested unit (Samsung AGND3R,
EXT_CSD_REV 7 = **eMMC 5.0**, PARTITION_CONFIG 0, RST_n_FUNCTION 0) the boot ROM **never runs the SPL from the
eMMC**, although the install is verified (the boot loader at 8 KiB is byte-identical, the layout is the SD's):
U-Boot's own breadcrumbs (below) stay unset and the screen stays dark (FEL). Olimex saw the same with eMMC 5.x on
the A20 and added SPI flash to their boards. So:

* **Starter card**: after an install, the microSD can keep its boot loader and its RETROSTONE partition (the
  games) and drop its two system partitions (`rsos-storage sd-starter`: MBR entries 2 and 3 removed, the boot
  state reset for the eMMC's slot A). At power-on its U-Boot finds no `boot.scr` on the card and loads the one on
  the eMMC; Linux runs from the eMMC, U-Boot's environment stays on the card (`rsos.envdev=mmcblk0` on the kernel
  command line, `/dev/rsos-disk` in rcS). A freshly flashed card always starts first: the way back.
  `sd-starter` refuses (Nothing changed, "flash a new card first") unless the card's own U-Boot at 8 KiB is this
  system's (the one with `mmc 1`) and its saved bootcmd has the eMMC fallback (`rsos_sysdev`): a card flashed with
  an older version and updated over the air would otherwise no longer start at all.
  **Make this SD card a full system again** (`sd-full`) is power-cut safe: the old file systems of the card's
  slots lose their superblock before their MBR entries come back, the system is copied and verified without its
  first 4 KiB, and those are written last (then read back), before the boot state: until then the card stays a
  starter card.
* The eMMC still gets its own boot loader at every install, so a unit whose boot ROM does start its eMMC (another
  eMMC part, or a future workaround) boots it without a card, and nothing else changes.
* **Possible hardware route** (not done): the board has test pads on **PC0/PC1/PC2/PC23**, the A20's SPI0. An
  8-pin SPI NOR there would let the boot ROM start U-Boot from SPI (`SPL_SPI_SUNXI`), which then loads the system
  from the eMMC: a boot without any card, as on Olimex's later boards.

## How it works

* **U-Boot** (`uboot.fragment`): the A20-OLinuXino-LIME-eMMC device tree (the Lime's plus mmc2, wired like the
  RetroStone2: PC6-PC11 4-bit, reset PC16) and `MMC_SUNXI_SLOT_EXTRA=2`: the eMMC is U-Boot's `mmc1`. U-Boot sets
  `mmc_bootdev` (0 microSD, 1 eMMC) and keeps its environment on the disk it started from. The boot command picks
  the system disk `rsos_sysdev`: the boot disk, or the eMMC when started from a card without `boot.scr` (starter).
  It loads the card's `boot.scr` first, exactly as before (no extra file system lookup on a normal start), and
  tries `mmc 1` the same way only when neither of the card's slots has one.
* **boot.cmd** loads from `mmc ${rsos_sysdev}` (`root=/dev/mmcblk<sysdev>p<slot>`; the DT aliases make the eMMC
  `mmcblk1` in Linux too), applies the `emmc` overlay when the system is on the eMMC, and passes `rsos.envdev`.
* **Linux**: `board.ini` `data_disk = auto` (the disk of `root=`); `/etc/fw_env.config` uses `/dev/rsos-disk`, the
  disk with U-Boot's environment; `rsos-boot-ok`, the updater and `rsos-storage` write there. A/B updates work the
  same with the system on the eMMC.
* **`/usr/bin/rsos-storage`** (board overlay) does the disk work; the menu (`frontend/src/ui/storage.c`) runs it as
  a job behind a progress page (`/run/rsos/storage-progress`). Every step is in the kernel log and in
  `rsos/logs/storage.log` on the card.
* **The install** writes the SD image's layout to the eMMC: the boot area zeroed first, the partition table, the
  running root slot in 16 MiB chunks read back from the eMMC (page cache dropped), slot B emptied, a new exFAT
  `RETROSTONE` sized to the eMMC with the settings, saves, BIOS files, themes (and the games, after a space check
  done before anything is written), then **last** the boot loader (checked with its sha256, read back). The
  microSD is never written by the install.
* **Game disks** are mounted on `/run/rsos/media/<emmc|sd|m2>`; `/run/rsos/libraries` lists those with `roms/`,
  and the loader (`loader_set_libraries`, `games_merge`) merges `<disk>/roms/<system>/` into each list, each disk
  with its own scan cache. **Nothing runs at boot on a unit with both off** (the default): `rsos-storage boot`
  (in the background once the menu is up) only runs when the eMMC controller (`1c11000.mmc`, the `emmc` overlay)
  or the SATA controller (`1c18000.sata`) is enabled, or the system is not on the microSD. The menu does not run
  `rsos-storage refresh` when it starts: it polls `/sys/block` every 10 s (a card or drive that comes or goes runs
  a refresh), and Settings > Storage runs one when it opens.
* **Diagnostics**, by hand (support): `rsos-storage report` writes `rsos/logs/emmc-report.txt` (the eMMC's boot
  loader, partitions, U-Boot environment, EXT_CSD boot registers, the logs a start from it left) and the AXP209
  data registers: U-Boot patch 0003 writes REG0E/0F = `5a e1` (SPL) / `5a e2` (U-Boot proper) when it runs from
  the eMMC (`axpstamp raw` reads them; the boot logger's `axpstamp show` never clears REG0D-0F).

## Game covers

`rsos-covers` (frontend/src/update, the updater's HTTPS client) gets box art from
`thumbnails.libretro.com/<libretro system>/Named_Boxarts/`, the server RetroArch uses. The menu writes the
games without a picture to a request file; per system the helper reads the server's listing once and matches
each ROM file name: exactly (No-Intro names), else the title without its (tags), the ROM's region first, then
USA, World, Europe, Japan, and finally a title that starts with the file's name ("Donkey Kong Country 2" ->
"... - Diddy's Kong Quest"). Covers are scaled to 400 px and saved as JPEG (~40 KB) in
`roms/<system>/media/covers/<file name>.jpg`; the game lists show them when `gamelist.xml` gives no image
(`apply_covers` in ui/games.c). Nothing else is written: Skraper's `gamelist.xml` and existing art win. Arcade
systems are skipped (zip names, not titles). The games not found are listed in `rsos/logs/covers-missing.txt`.
Needs WiFi and a correct clock (HTTPS).

## Multi-disc games

`.m3u` playlists already make one entry of a multi-disc game (the discs it lists are hidden; Select+X > Disc
changes discs). **Group multi-disc games** writes the missing playlists: for every set of `... (Disc N) ...`
files in a folder without one, `<name>.m3u` listing the discs in order (written to a temporary file, synced,
then renamed). Only for systems whose core takes `.m3u` files: elsewhere the playlist would hide the discs behind
an entry that cannot start. Files are never moved or renamed.

## Memory speed (Settings > Power > Memory speed)

The DRAM runs at 384 MHz with the Lime's vendor timings. **Faster** = 432 MHz (+12.5 %), **Fastest** = 480 MHz
(+25 %), each with U-Boot's DDR3-1066F timings for that clock (the A20 boards that run there: Cubietruck,
Bananapi, Lamobo R1). The clock is chosen by the SPL before it starts the DRAM (U-Boot patch 0004), from the
AXP209 data register REG0D, which the PMIC keeps while the battery is in:

* the menu writes "requested" (`axpstamp dram-set`); every start at a faster level is a **trial** (the SPL
  writes "trial" before using it);
* 30 s after a stable start, `rsos-boot-ok` runs `rsos-memtest` (96 MiB, 4 patterns, 2 passes): passed →
  "confirmed"; failed → "failed" and an immediate restart at the normal speed (a memory that flips bits must not
  write files);
* a start that is never confirmed (freeze, crash, power cut, forced power-off) is seen by the next SPL: normal
  speed, "failed", shown in the menu. An orderly shutdown before the confirmation retries at the next start.
* the battery out (REG0D cleared): the normal speed.

The shutdown timestamps (`axpstamp save/show`) moved to REG04-0C to leave REG0D-0F alone.

## Hardware test checklist

0. A regular RetroStone2, freshly flashed: boots 1 and 2 start as fast as with 0.1.3 (compare
   `rsos/logs/boot<N>/bootstage.txt`); Settings > Storage: Model RetroStone2, no Internal storage section, both
   switches off.
1. A Pro, freshly flashed (the Pro storage needs a 0.1.4 card, not an over-the-air update): Settings > Storage:
   **Model** says RetroStone2 Pro, **Internal eMMC** and **M.2 SATA drive** are off (the default, for a fast
   boot). Turn on **Internal eMMC** (and **M.2 SATA drive** with an SSD fitted), restart.
2. **Install to internal storage**, then **Use as starter card** and **CONTINUE** in its confirmation, then
   **Restart now** (keep the card in).
3. Settings > Storage shows "Started from: internal + SD starter card"; the card's games are in the lists.
4. M.2: **Erase it and use it for games**, import to it with **Copy games to**, play a game from it.
5. Game lists: **Group multi-disc games**, **Download game covers** (WiFi on).
6. Recovery: a freshly flashed RetroStoneOS card starts the console from the card.

# RetroStone2 "Pro" storage, game covers, multi-disc games

The Pro variant has a 16 GB eMMC (U23, mmc2, 4-bit) and an M.2 M-key slot (J1) for a **SATA** SSD (the A20
has no PCIe: NVMe drives do not work). This page describes how RetroStoneOS uses them, and two game list
features that came with this work: box covers from the libretro thumbnail server, and grouping multi-disc games.

## Regular or Pro

The first start of a card finds out which RetroStone2 it runs on. `boot.cmd` sees no `rsos_model` in the U-Boot
environment, so it probes U-Boot's `mmc 1` (the eMMC U23) once and saves `rsos_model=pro` or `regular`:

- **Pro** (the eMMC answers): `rsos_overlays` gets `emmc` and `sata`, applied on that same start, so the
  internal storage and the M.2 slot work without touching a setting (the toggles can still switch them off).
- **Regular** (no eMMC): `emmc` is removed from `rsos_overlays` (`sata` and the others stay), and Settings >
  Storage hides the Internal storage section; the M.2 section stays.

Settings > Storage shows **This console > Model** (also in System information) and **Detect the model again**
(`rsos-storage redetect` clears `rsos_model`; the next start probes again). A unit that runs from the eMMC or
through a starter card, or whose eMMC answers in Linux, counts as a Pro whatever `rsos_model` says. The probe
costs one failed MMC initialisation (milliseconds) on a regular unit, once. `boot-ab-qemu-test.sh` covers both
cases (cases 14 and 15).

## What the player sees

**Settings > Storage**, started from the microSD:

| Item | What it does |
|---|---|
| Internal eMMC (toggle) | `rsos_overlays` in the U-Boot environment; the eMMC appears after a restart ("not fitted" on a unit without one) |
| Install to internal storage | copies RetroStoneOS to the eMMC, **with or without the games**, then asks: **Use as starter card** / Keep SD as it is |
| Update internal storage | the same when the eMMC already holds RetroStoneOS: keep its games and saves, or erase it |
| Use this SD card as starter card | when the eMMC holds RetroStoneOS (see "Starter card") |
| M.2 SATA drive (toggle) | the SATA controller (overlay `sata`); any unit with the slot fitted |
| Erase it and use it for games | the M.2 drive becomes a game disk (one exFAT partition, `roms/<system>/`) |

Running from the eMMC: the card in the slot is shown with what it holds; a starter card says "Keep it in"
and offers nothing that could erase it. Another RetroStoneOS card can become a games card, any other card can be
erased for games.

The games of every mounted game disk (the eMMC or microSD the system does not run from, the M.2 drive) are
merged into the game lists. **Import games** gets **Copy games to** (the system disk or a game disk; another disk
receives only the games, BIOS files, saves and themes stay on the system disk).

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
  with its own scan cache. The menu polls `/sys/block` every 2 s: a card or drive that comes or goes runs
  `rsos-storage refresh`.
* **Diagnostics**: at every start from the microSD with the eMMC visible, `rsos/logs/emmc-report.txt` (its boot
  loader, partitions, U-Boot environment, EXT_CSD boot registers, the logs a start from it left) and the AXP209
  data registers: U-Boot patch 0003 writes REG0E/0F = `5a e1` (SPL) / `5a e2` (U-Boot proper) when it runs from
  the eMMC; rcS reads them (`axpstamp raw`) before the boot logger clears them.

## Game covers

`rsos-covers` (frontend/src/update, the updater's HTTPS client) gets pictures from
`thumbnails.libretro.com/<libretro system>/`, the server RetroArch uses. **Settings > Game lists > Picture style**
picks the kind: **Title screen** (`Named_Titles`, 4:3, like the pictures of the built-in RetroStone games; the
default) or **Box art** (`Named_Boxarts`); a game the server has no picture of that kind for gets the other one.
Changing the style offers to download the existing pictures again (`--replace`: the downloaded `.jpg` files
only).

The menu writes the games without a picture to a request file; per system the helper reads the server's listing
once (cached 7 days in `rsos/cache/covers/<system>-<kind>.txt`) and matches each ROM file name: exactly
(No-Intro names), else the same title, else a title that starts with the file's name ("Donkey Kong Country 2" ->
"... - Diddy's Kong Quest"). "The same title" ignores (tags), "the", accents, apostrophes and a disc number
written as words ("Final Fantasy VII Disc 1", "CD2"), reads "&" as "and" and II-IX as 2-9; among equal titles the
ROM's region comes first, then USA, World, Europe, Japan, and the same disc. Pictures are scaled to 400 px and
saved as JPEG in `roms/<system>/media/covers/<file name>.jpg`; the game lists show them when `gamelist.xml`
gives no image (`apply_covers` in ui/games.c). Nothing else is written: Skraper's `gamelist.xml` and existing art
win. Arcade systems are skipped (zip names, not titles). Needs WiFi and a correct clock (HTTPS).

**Choose cover** (a game's options; also **Settings > Game lists > Choose missing covers**, the games without a
picture, ticked once they have one): the 20 names on the server closest to the file name (`rsos-covers
--suggest`: shared words, 4+ letter word starts count half), **Search by name...** for other words, and choosing
one downloads it (`--get`). **Use my own picture...** shows where a picture of your own goes:
`roms/<system>/media/covers/<file name>.png` (a `.png` wins over a downloaded `.jpg`; 640 x 480 looks like the
RetroStone games). **Remove the picture** deletes it.

## Multi-disc games

`.m3u` playlists already make one entry of a multi-disc game (the discs it lists are hidden; Select+X > Disc
changes discs). **Group multi-disc games** writes the missing playlists: for every set of `... (Disc N) ...`
files in a folder without one, `<name>.m3u` listing the discs in order. The disc number may also be plain
words (`Name Disc 1.cue`, `Name - CD2.chd`, `Name_Disc_2 (USA).cue`, `[Disc 1 of 2]`). Files are never moved or
renamed.

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

1. Flash the image, boot, Settings > Storage: **Model** says RetroStone2 Pro, and **Internal eMMC** and **M.2
   SATA drive** are already on (on a regular unit: Model RetroStone2, no Internal storage section).
2. **Install to internal storage**, then **Use as starter card**, then **Restart now** (keep the card in).
3. Settings > Storage shows "Started from: internal + SD starter card"; the card's games are in the lists.
4. M.2: **Erase it and use it for games**, import to it with **Copy games to**, play a game from it.
5. Game lists: **Group multi-disc games** (also "Name Disc 1" files), **Download game covers** (WiFi on; title
   screens by default), **Choose missing covers** / a game's **Choose cover**.
6. Recovery: a freshly flashed RetroStoneOS card starts the console from the card.

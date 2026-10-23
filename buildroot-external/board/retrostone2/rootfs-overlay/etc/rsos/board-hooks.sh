# RetroStone2 board hooks, sourced by /usr/libexec/rsos/board.sh (from rcS).
# Only function definitions: nothing runs when this file is read.

# rcS, in the background once the menu is up.
# Audio: the volume wheel and the speaker/headphone switch are analog, so
# the codec output is simply set to a fixed high level with the DAC routed
# to the power amplifier (A20 sun4i-codec, ALSA card 0).
# TODO(hw): tune the level (0..63) so the wheel covers a useful range.
rsos_board_late_audio() {
	if [ -x /usr/bin/amixer ]; then
		amixer -q -c 0 cset name='Power Amplifier Volume' 56 2>/dev/null
		amixer -q -c 0 cset name='Power Amplifier DAC Playback Switch' on 2>/dev/null
		amixer -q -c 0 cset name='Power Amplifier Mute Switch' on 2>/dev/null
	fi
}

# rcS, in the background, after the USB storage modules.
# SATA ("Pro" units, sata overlay): libata/ahci_sunxi are modules; the
# platform device only exists when the overlay enabled the controller.
# Then the game libraries of the other disks (rsos-storage boot: the eMMC
# or the microSD that the system does not run from, the M.2 drive), and the
# first start from the eMMC carries the microSD's overlays over. In the
# background: the SATA link takes a moment to come up. Only when there is
# such a disk: the eMMC controller (the emmc overlay, 1c11000.mmc) or the
# SATA controller (the sata overlay) is enabled, or the system does not run
# from the microSD. A RetroStone2 with both off (the default: fast boot)
# starts nothing here.
rsos_board_late_storage() {
	[ -d /sys/bus/platform/devices/1c18000.sata ] && modprobe -q ahci_sunxi
	if [ -e /sys/bus/platform/devices/1c11000.mmc ] || [ -e /sys/bus/platform/devices/1c18000.sata ] ||
		[ "${RSOS_DISK:-/dev/mmcblk0}" != /dev/mmcblk0 ]; then
		[ -x /usr/bin/rsos-storage ] && /usr/bin/rsos-storage boot < /dev/null > /dev/null 2>&1 &
	fi
}

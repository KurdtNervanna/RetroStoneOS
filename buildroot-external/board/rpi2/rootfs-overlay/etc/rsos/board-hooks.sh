# RetroStoneOS board hooks: Raspberry Pi 2/3 (sourced by
# /usr/libexec/rsos/board.sh; definitions only, nothing runs here).

# rcS, background, once the menu is up: the Pi's analog audio (the 3.5 mm
# jack, and the PiGRRL 2's speaker amplifier) when config.txt turns it on
# ("dtparam=audio=on": the firmware then passes
# snd_bcm2835.enable_headphones=1). snd-bcm2835 is a module and nothing
# loads it without udev; HDMI sound (vc4-hdmi) does not need it. With an
# SPI screen (the PiGRRL 2) the game sound follows that "LCD" to the first
# card that is not HDMI: this one (games open it when they start).
rsos_board_late_audio() {
	case " $CMDLINE " in
	*" snd_bcm2835.enable_headphones=1 "*) modprobe -q snd-bcm2835 ;;
	esac
}

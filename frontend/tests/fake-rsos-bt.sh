#!/bin/sh
# fake-rsos-bt.sh: a stand-in for rsos-bt (buildroot-external/board/common/
# rootfs-overlay/usr/bin/rsos-bt) for the preview tool (--bt-helper): the
# devices live in $FAKE_BT_DIR/devices ("MAC<TAB>state<TAB>name", the format
# of "rsos-bt list"), created with one paired and two found devices; every
# call is logged to $FAKE_BT_DIR/calls.log. FAKE_BT_FAIL=pair makes pairing
# fail.
D=${FAKE_BT_DIR:?FAKE_BT_DIR is not set}
mkdir -p "$D"
DEV=$D/devices
T=$(printf '\t')
if [ ! -f "$DEV" ]; then
	printf '%s\t%s\t%s\n' \
		"E4:17:D8:00:00:01" paired "8BitDo SN30 Pro" \
		"F0:13:C3:00:00:02" found "Stadia5RHM-1a2b" \
		"A0:5A:5D:00:00:03" found "Wireless Controller" > "$DEV"
fi
echo "$*" >> "$D/calls.log"

# set MAC STATE: change the state of a device
set_state() {
	sed "s/^${1}${T}[a-z]*${T}/${1}${T}${2}${T}/" "$DEV" > "$DEV.tmp" && mv "$DEV.tmp" "$DEV"
}

case "$1" in
list)
	cp "$DEV" "$2.tmp" && mv "$2.tmp" "$2"
	;;
scan) ;;
pair)
	sleep 1
	if [ "${FAKE_BT_FAIL:-}" = pair ]; then
		echo "rsos-bt: pairing failed: org.bluez.Error.AuthenticationFailed"
		exit 1
	fi
	set_state "$2" connected
	;;
connect) set_state "$2" connected ;;
disconnect) set_state "$2" paired ;;
forget)
	grep -v "^$2$T" "$DEV" > "$DEV.tmp"
	mv "$DEV.tmp" "$DEV"
	;;
*) exit 2 ;;
esac
exit 0

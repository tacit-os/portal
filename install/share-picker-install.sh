#!/bin/bash
# Screen-share picker: install / undo / status. Does not restart anything.
#   share-picker-install.sh install   picker -> ~/.local/bin, xdph.conf custom_picker_binary, shell sheet files
#   share-picker-install.sh undo      removes the custom_picker_binary line it added (a line it replaced comes back), and
#                                     every other file it put in place; the backup is in ~/.local/share/tacit-portal/share-picker-backup
# A run that stops half way leaves a "partial" marker: undo works from it, and running install again is allowed.
# Needs first (DialogBridge step): the rebuilt tacit-portal running, and the shell's services/DialogBridge.qml.
# Until the shell runs the sheet, the picker hands over to the stock hyprland-share-picker, so sharing keeps working.
# After install: restart xdg-desktop-portal-hyprland (systemctl --user restart xdg-desktop-portal-hyprland) and the shell.
set -eu
here=$(dirname "$(realpath "$0")")/..
src=${SHELL_WORK:-$HOME/book4-edge/tacit-fix/end4-pC}
live=${SHELL_LIVE:-$HOME/.config/quickshell/end4-pC}
bin=$HOME/.local/bin/tacit-share-picker
conf=$HOME/.config/hypr/xdph.conf
bak=$HOME/.local/share/tacit-portal/share-picker-backup
family=$live/panelFamilies/IllogicalImpulseFamily.qml
sheet=$live/modules/ii/sharePicker
. "$here/install/dialogs-common.sh"
imp='import qs.modules.ii.sharePicker'
ld='    PanelLoader { component: SharePicker {} }'
line="custom_picker_binary = $bin"

case ${1:-} in
install)
	[ -f "$live/services/DialogBridge.qml" ] || [ -f "$src/services/DialogBridge.qml" ] || { echo "no DialogBridge.qml"; exit 1; }
	mkdir -p "$bak" "$HOME/.local/bin"
	[ -e "$bak/installed" ] && { echo "already installed (run undo first)"; exit 1; }
	date >"$bak/partial"
	# picker
	install -m 755 "$here/share-picker/tacit-share-picker" "$bin"
	# xdph.conf: created, or the screencopy block gets the line; the old file is kept for undo
	# (kept once: a second run after a failure must not replace the original with the patched file)
	if [ ! -e "$bak/xdph.conf" ] && [ ! -e "$bak/xdph.conf.absent" ]; then
		if [ -f "$conf" ]; then cp -p "$conf" "$bak/xdph.conf"; else : >"$bak/xdph.conf.absent"; fi
	fi
	python3 - "$conf" "$line" <<'PY'
import os, re, sys
p, line = sys.argv[1:3]
s = open(p).read() if os.path.exists(p) else ""
if re.search(r"^\s*custom_picker_binary\s*=", s, re.M):
    s = re.sub(r"^(\s*)custom_picker_binary\s*=.*$", lambda m: m.group(1) + line, s, flags=re.M)
elif re.search(r"^\s*screencopy\s*\{", s, re.M):
    s = re.sub(r"^(\s*screencopy\s*\{)", lambda m: m.group(1) + "\n    " + line, s, count=1, flags=re.M)
else:
    s += ("\n" if s and not s.endswith("\n") else "") + "screencopy {\n    " + line + "\n}\n"
os.makedirs(os.path.dirname(p), exist_ok=True)
open(p, "w").write(s)
PY
	# shell: the sheet files, DialogBridge when the live tree lacks it, and the loader line in the panel family
	mkdir -p "$sheet"
	cp "$src/modules/ii/sharePicker/"*.qml "$sheet/"
	bridge_use share-picker
	family_patch "$imp" "import qs.modules.ii.regionSelector" '    PanelLoader { component: RegionSelector {} }' "$ld" 'Screen-share picker for xdph custom_picker_binary (tacit-share-picker); also keeps DialogBridge connected.'
	mv "$bak/partial" "$bak/installed"
	echo "installed. Restart xdg-desktop-portal-hyprland and the shell (tacit-portal must be the rebuilt one)."
	echo "UNVERIFIED against a live xdph: the window id, the restore-token flag and the region coordinates (sent output-local;"
	echo "a global-layout origin would be wrong on a second monitor). Try one share of each kind, on a non-primary monitor if you"
	echo "have one, and read: journalctl --user -u xdg-desktop-portal-hyprland -n 40. If a cast is cancelled, run undo."
	;;
undo)
	[ -e "$bak/installed" ] || [ -e "$bak/partial" ] || { echo "nothing to undo"; exit 0; }
	unpatch_conf "$conf" "custom_picker_binary" "$bak/xdph.conf" "$line" screencopy
	rm -f "$bin"
	family_unpatch "$imp" "$ld"
	rm -rf "$sheet"
	bridge_release share-picker
	rm -rf "$bak"
	echo "undone. Restart xdg-desktop-portal-hyprland and the shell."
	;;
status)
	[ -e "$bak/installed" ] && echo "installed $(cat "$bak/installed")" || echo "not installed"
	grep -n custom_picker_binary "$conf" 2>/dev/null || true
	;;
*) echo "usage: $0 install|undo|status"; exit 2 ;;
esac

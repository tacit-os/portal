#!/bin/bash
# Open with (AppChooser): install / undo / status. Restarts nothing.
#   open-with-install.sh install   builds tacit-portal in ./build, adds AppChooser = tacit to the user portals conf,
#                                  copies the shell sheet and adds its loader line to the panel family
#   open-with-install.sh undo      removes the AppChooser line it added (a line it replaced comes back) and the sheet; the backup
#                                  is in ~/.local/share/tacit-portal/open-with-backup
# A run that stops half way leaves a "partial" marker: undo works from it, and running install again is allowed.
# Two steps stay with you (root, running daemons): the one sudo line and the restarts, printed at the end.
# Needs the DialogBridge step (services/DialogBridge.qml). Until the shell runs the sheet, or when no app opens the
# type, the call goes to the GTK backend unchanged, so Open with never stops working.
set -eu
here=$(dirname "$(realpath "$0")")/..
src=${SHELL_WORK:-$HOME/book4-edge/tacit-fix/end4-pC}
live=${SHELL_LIVE:-$HOME/.config/quickshell/end4-pC}
# The hyprland-portals.conf that counts: with the link-portals method (INSTALL.md) xdg-desktop-portal reads the one in
# XDG_DESKTOP_PORTAL_DIR (env, else the systemd drop-in), not the one in ~/.config.
portal_dir=${XDG_DESKTOP_PORTAL_DIR:-}
if [ -z "$portal_dir" ]; then
	portal_dir=$(sed -n 's/^Environment=XDG_DESKTOP_PORTAL_DIR=//p' "$HOME"/.config/systemd/user/xdg-desktop-portal.service.d/*.conf 2>/dev/null | tail -1 | sed "s|%h|$HOME|g")
fi
if [ -n "$portal_dir" ]; then conf=$portal_dir/hyprland-portals.conf; else conf=$HOME/.config/xdg-desktop-portal/hyprland-portals.conf; fi
bak=$HOME/.local/share/tacit-portal/open-with-backup
family=$live/panelFamilies/IllogicalImpulseFamily.qml
sheet=$live/modules/ii/openWith
. "$here/install/dialogs-common.sh"
imp='import qs.modules.ii.openWith'
ld='    PanelLoader { component: OpenWith {} }'
build=${BUILD_DIR:-$here/build}
iface=org.freedesktop.impl.portal.AppChooser

case ${1:-} in
install)
	[ -f "$live/services/DialogBridge.qml" ] || [ -f "$src/services/DialogBridge.qml" ] || { echo "no DialogBridge.qml"; exit 1; }
	[ -e "$bak/installed" ] && { echo "already installed (run undo first)"; exit 1; }
	mkdir -p "$bak"
	date >"$bak/partial"
	# the portal: new file next to the old one, swapped by rename so the running process keeps its inode
	cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
	nice -n 10 cmake --build "$build" --target tacit-portal >/dev/null
	# user conf: AppChooser line (the old file is kept for undo)
	# (kept once: a second run after a failure must not replace the original with the patched file; the path is
	# kept too, so undo edits the file install edited)
	echo "$conf" >"$bak/conf-path"
	if [ -L "$conf" ]; then
		echo "$conf is a link (link-portals.sh) to $(readlink -f "$conf"); not edited, that copy already has: $iface = tacit"
		: >"$bak/conf-is-link"
	else
	mkdir -p "$(dirname "$conf")"
	if [ ! -e "$bak/hyprland-portals.conf" ] && [ ! -e "$bak/hyprland-portals.conf.absent" ]; then
		if [ -f "$conf" ]; then cp -p "$conf" "$bak/hyprland-portals.conf"; else : >"$bak/hyprland-portals.conf.absent"; fi
	fi
	python3 - "$conf" "$iface" <<'PY'
import os, re, sys
p, iface = sys.argv[1:3]
s = open(p).read() if os.path.exists(p) else "[preferred]\ndefault = hyprland;gtk\n"
line = iface + " = tacit"
if re.search(r"^" + re.escape(iface) + r"\s*=", s, re.M):
    s = re.sub(r"^" + re.escape(iface) + r"\s*=.*$", line, s, flags=re.M)
else:
    s += ("\n" if not s.endswith("\n") else "") + line + "\n"
open(p, "w").write(s)
PY
	fi
	# shell: the sheet, DialogBridge when the live tree lacks it, the loader line
	mkdir -p "$sheet"
	cp "$src/modules/ii/openWith/"*.qml "$sheet/"
	bridge_use open-with
	family_patch "$imp" "import qs.modules.ii.regionSelector" '    PanelLoader { component: RegionSelector {} }' "$ld" '"Open with" menu for tacit-portal AppChooser (DialogBridge kind open.choose).'
	mv "$bak/partial" "$bak/installed"
	cat <<MSG
installed. Still to do by hand:
  1. sudo install -m644 $here/install/tacit.portal /usr/share/xdg-desktop-portal/portals/tacit.portal
     (adds AppChooser to Interfaces; undo: put the old line back, "Interfaces=org.freedesktop.impl.portal.FileChooser")
  2. systemctl --user restart xdg-desktop-portal   # after the sudo step
     and stop the old tacit-portal by its PID (D-Bus starts the new binary on the next call)
  3. restart the shell (new files)
MSG
	;;
undo)
	[ -e "$bak/installed" ] || [ -e "$bak/partial" ] || { echo "nothing to undo"; exit 0; }
	[ -f "$bak/conf-path" ] && conf=$(cat "$bak/conf-path")
	[ -e "$bak/conf-is-link" ] || unpatch_conf "$conf" "$iface" "$bak/hyprland-portals.conf" "$iface = tacit"
	family_unpatch "$imp" "$ld"
	rm -rf "$sheet"
	bridge_release open-with
	rm -rf "$bak"
	echo "undone. By hand: put the Interfaces line of /usr/share/xdg-desktop-portal/portals/tacit.portal back to FileChooser only,"
	echo "restart xdg-desktop-portal and the shell. (The portal binary stays: it forwards AppChooser to GTK when not configured.)"
	;;
status)
	[ -e "$bak/installed" ] && echo "installed $(cat "$bak/installed")" || echo "not installed"
	[ -e "$bak/installed" ] || [ -e "$bak/partial" ] && [ -f "$bak/conf-path" ] && conf=$(cat "$bak/conf-path")
	grep -H "AppChooser" "$conf" /usr/share/xdg-desktop-portal/portals/tacit.portal 2>/dev/null || true
	;;
*) echo "usage: $0 install|undo|status"; exit 2 ;;
esac

#!/bin/bash
# Keyring prompter: install / undo / status. Restarts nothing except the prompter itself.
#   keyring-prompter-install.sh install   builds the prompter, puts it in ~/.local/bin, adds the two D-Bus activation
#                                         files that make it the owner of org.gnome.keyring.{System,Private}Prompter,
#                                         copies the shell sheet and adds its loader line to the panel family
#   keyring-prompter-install.sh undo      puts every file back from ~/.local/share/tacit-portal/keyring-prompter-backup
# A run that stops half way leaves a "partial" marker: undo works from it, and running install again is allowed.
# Needs: the rebuilt tacit-portal running (it owns the bridge socket) and the shell's services/DialogBridge.qml.
# With no shell on the bridge (or a shell that restarts, or whose sheet is not up yet) the prompter waits 30 s for
# one, then ends the prompt as dismissed (never a hang).
# After install: restart the shell (new files; hot reload misses a new module's singleton users). The prompter starts by
# D-Bus activation on the next secret prompt. gnome-keyring stays the store.
set -eu
here=$(dirname "$(realpath "$0")")/..
src=${SHELL_WORK:-$HOME/book4-edge/tacit-fix/end4-pC}
live=${SHELL_LIVE:-$HOME/.config/quickshell/end4-pC}
bin=$HOME/.local/bin/tacit-keyring-prompter
svcdir=$HOME/.local/share/dbus-1/services
names="org.gnome.keyring.SystemPrompter org.gnome.keyring.PrivatePrompter"
bak=$HOME/.local/share/tacit-portal/keyring-prompter-backup
family=$live/panelFamilies/IllogicalImpulseFamily.qml
sheet=$live/modules/ii/keyring
. "$here/install/dialogs-common.sh"
imp='import qs.modules.ii.keyring'
ld='    PanelLoader { component: Keyring {} }'

reload_bus() { busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig >/dev/null 2>&1 || true; }

case ${1:-} in
install)
	[ -f "$live/services/DialogBridge.qml" ] || [ -f "$src/services/DialogBridge.qml" ] || { echo "no DialogBridge.qml"; exit 1; }
	[ -e "$bak/installed" ] && { echo "already installed (run undo first)"; exit 1; }
	make -C "$here/keyring-prompter" >/dev/null
	mkdir -p "$bak" "$HOME/.local/bin" "$svcdir"
	date >"$bak/partial"
	install -m 755 "$here/keyring-prompter/tacit-keyring-prompter" "$bin"
	for n in $names; do
		f=$svcdir/$n.service
		# (kept once: a second run after a failure must not replace the original with ours)
		if [ ! -e "$bak/$n.service" ] && [ ! -e "$bak/$n.service.absent" ]; then
			if [ -e "$f" ]; then cp -p "$f" "$bak/$n.service"; else : >"$bak/$n.service.absent"; fi
		fi
		printf '[D-BUS Service]\nName=%s\nExec=%s\n' "$n" "$bin" >"$f"
	done
	mkdir -p "$sheet"
	cp "$src/modules/ii/keyring/"*.qml "$sheet/"
	bridge_use keyring
	family_patch "$imp" "import qs.modules.ii.island" '    PanelLoader { component: BluetoothPairing {} }' "$ld" 'Keyring unlock / new password / confirm from tacit-keyring-prompter (gcr prompter replacement) through DialogBridge.'
	reload_bus
	mv "$bak/partial" "$bak/installed"
	echo "installed. Restart the shell. If the stock /usr/lib/gcr-prompter is running it keeps the names until it idles out"
	echo "(a minute after its last prompt); to hurry it, when no keyring dialog is open:  pkill -x gcr-prompter"
	echo "Check who owns the names (should be tacit-keyring-prompter after the next prompt); if the stock prompter still"
	echo "answers, log out and in so dbus-broker rereads ~/.local/share/dbus-1/services:"
	echo "  busctl --user status org.gnome.keyring.PrivatePrompter | grep -E '^(PID|Comm)'"
	;;
undo)
	[ -e "$bak/installed" ] || [ -e "$bak/partial" ] || { echo "nothing to undo"; exit 0; }
	for n in $names; do
		if [ -f "$bak/$n.service" ]; then cp -p "$bak/$n.service" "$svcdir/$n.service"; else rm -f "$svcdir/$n.service"; fi
	done
	reload_bus
	# A prompter that is running keeps its names and finishes its prompt; it exits by itself when idle (a minute), and the
	# next secret prompt starts the stock one. It is not killed here: that could be mid-prompt.
	rm -f "$bin"
	family_unpatch "$imp" "$ld"
	rm -rf "$sheet"
	bridge_release keyring
	rm -rf "$bak"
	echo "undone. Restart the shell."
	;;
status)
	[ -e "$bak/installed" ] && echo "installed $(cat "$bak/installed")" || echo "not installed"
	for n in $names; do grep -H Exec "$svcdir/$n.service" 2>/dev/null || true; done
	;;
*) echo "usage: $0 install|undo|status"; exit 2 ;;
esac

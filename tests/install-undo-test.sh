#!/bin/bash
# Install / undo edge cases in a private HOME (nothing live is touched): undo takes out only the line install added,
# a rerun after a failed install keeps the original backup, and a half-done install can be undone.
set -u
here=$(dirname "$(realpath "$0")")/..
REAL_HOME=$HOME
work=$HOME/.cache/tacit-dialogs/undo-test
rm -rf "$work"; mkdir -p "$work/shim"
export HOME=$work/home SHELL_WORK=${SHELL_WORK:-$REAL_HOME/book4-edge/tacit-fix/end4-pC} BUILD_DIR=$REAL_HOME/.cache/tacit-dialogs/pb
for c in busctl pkill; do printf "#!/bin/sh\nexit 0\n" >"$work/shim/$c"; chmod +x "$work/shim/$c"; done
export PATH=$work/shim:$PATH SHELL_LIVE=$work/live
live=$SHELL_LIVE; fail=0
ok() { [ "$2" = 0 ] && echo "ok   $1" || { echo "FAIL $1"; fail=1; }; }
fresh() { rm -rf "$live" "$HOME"; mkdir -p "$live/modules/ii" "$HOME"; LIVE_SRC=$REAL_HOME/.config/quickshell/end4-pC; cp -r "$LIVE_SRC/panelFamilies" "$LIVE_SRC/services" "$live/"; }
sp=$here/install/share-picker-install.sh; conf=$HOME/.config/hypr/xdph.conf

fresh; mkdir -p "$(dirname "$conf")"; printf 'screencopy {\n    max_fps = 30\n}\n' >"$conf"
bash "$sp" install >/dev/null 2>&1; ok "install adds the line" $(grep -q custom_picker_binary "$conf"; echo $?)
echo "# edited after install" >>"$conf"
bash "$sp" undo >/dev/null 2>&1
ok "undo removes the line and keeps the later edit" $(! grep -q custom_picker_binary "$conf" && grep -q "edited after install" "$conf"; echo $?)
ok "undo keeps the file's own setting" $(grep -q max_fps "$conf"; echo $?)

fresh
bash "$sp" install >/dev/null 2>&1; bash "$sp" undo >/dev/null 2>&1
ok "a conf install created is removed by undo" $([ ! -e "$conf" ]; echo $?)

# a failed install (family layout unexpected), a rerun, then undo: the original conf comes back, not the patched one
fresh; mkdir -p "$(dirname "$conf")"; printf 'screencopy {\n    max_fps = 30\n}\n' >"$conf"
cp "$live/panelFamilies/IllogicalImpulseFamily.qml" "$work/family.orig"; echo "// nothing to anchor on" >"$live/panelFamilies/IllogicalImpulseFamily.qml"
bash "$sp" install >/dev/null 2>&1; r=$?
ok "install fails on an unexpected family file" $([ $r != 0 ]; echo $?)
bash "$sp" install >/dev/null 2>&1
ok "partial install is undoable" $(bash "$sp" undo 2>&1 | grep -q "undone"; echo $?)
ok "original conf back after the failed runs" $(! grep -q custom_picker_binary "$conf" && grep -q max_fps "$conf"; echo $?)

# open with: a prior AppChooser line comes back
oi=$here/install/open-with-install.sh; pc=$HOME/.config/xdg-desktop-portal/hyprland-portals.conf
fresh; mkdir -p "$(dirname "$pc")"; printf '[preferred]\ndefault = hyprland;gtk\norg.freedesktop.impl.portal.AppChooser = gtk\n' >"$pc"
bash "$oi" install >/dev/null 2>&1; ok "open-with sets AppChooser = tacit" $(grep -q "AppChooser = tacit" "$pc"; echo $?)
bash "$oi" undo >/dev/null 2>&1; ok "open-with undo puts the old AppChooser line back" $(grep -q "AppChooser = gtk" "$pc" && ! grep -q tacit "$pc"; echo $?)

# link-portals method: the conf in XDG_DESKTOP_PORTAL_DIR is the one that counts
fresh; d=$HOME/.local/share/tacit-portal/portals; mkdir -p "$d" "$HOME/.config/systemd/user/xdg-desktop-portal.service.d"
cp "$here/install/xdg-desktop-portal.service.d/tacit.conf" "$HOME/.config/systemd/user/xdg-desktop-portal.service.d/"
printf '[preferred]\ndefault = hyprland;gtk\n' >"$d/hyprland-portals.conf"
bash "$oi" install >/dev/null 2>&1; ok "open-with edits the folder's conf under the link-portals method" $(grep -q "AppChooser = tacit" "$d/hyprland-portals.conf" && [ ! -e "$HOME/.config/xdg-desktop-portal/hyprland-portals.conf" ]; echo $?)
bash "$oi" undo >/dev/null 2>&1; ok "...and undo takes it out of the same file" $(! grep -q AppChooser "$d/hyprland-portals.conf"; echo $?)
[ $fail = 0 ] && echo "install-undo-test: ok" || { echo "install-undo-test: FAILED"; exit 1; }

#!/bin/bash
# Fills the folder xdg-desktop-portal reads its .portal files from (XDG_DESKTOP_PORTAL_DIR replaces the system
# folder, it does not add to it): a link to every system .portal, plus tacit.portal.
# Run again after a package adds or removes a .portal file.
#   link-portals.sh [DIR]     default ~/.local/share/tacit-portal/portals
set -eu
here=$(dirname "$(realpath "$0")")
dir=${1:-$HOME/.local/share/tacit-portal/portals}
mkdir -p "$dir"
find "$dir" -maxdepth 1 -type l -delete
for f in /usr/share/xdg-desktop-portal/portals/*.portal; do
	ln -s "$f" "$dir/"
done
ln -s "$here/tacit.portal" "$dir/tacit.portal"
ln -s "$here/hyprland-portals.conf" "$dir/hyprland-portals.conf"
ls -l "$dir"

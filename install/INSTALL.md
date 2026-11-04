# Installing tacit-portal

Nothing here is applied by the build. Steps, each undone by the line under it.

1. Build: `cd ~/dev/tacit-portal && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
2. Activation file (the session bus starts the backend on the first call):
   `cp install/org.freedesktop.impl.portal.desktop.tacit.service ~/.local/share/dbus-1/services/`
   Undo: delete it.
3. Tell xdg-desktop-portal about `tacit.portal`. Two ways:
   - No sudo: `install/link-portals.sh` fills `~/.local/share/tacit-portal/portals` with links to every system
     .portal plus tacit.portal and hyprland-portals.conf; then
     `mkdir -p ~/.config/systemd/user/xdg-desktop-portal.service.d && cp install/xdg-desktop-portal.service.d/tacit.conf ~/.config/systemd/user/xdg-desktop-portal.service.d/`.
     XDG_DESKTOP_PORTAL_DIR replaces the system folder and the place portals.conf is read from, so the
     hyprland-portals.conf in that folder is the one that counts (the script links this repo's copy).
     Undo: delete the drop-in.
   - One sudo: `sudo install -m644 install/tacit.portal /usr/share/xdg-desktop-portal/portals/`, and change
     `org.freedesktop.impl.portal.FileChooser = kde` to `tacit` in `~/.config/xdg-desktop-portal/hyprland-portals.conf`.
     A package update may remove the file. Undo: delete the file, put `kde` back.
4. `systemctl --user daemon-reload && systemctl --user restart xdg-desktop-portal`.
5. Needs the shell from the work copy with `files pick`, `pickdir`, `save`, `savefolder` and `picked []` deployed (services/Files.qml, modules/ii/files/FilesWindow.qml). Without a running shell, SaveFile and SaveFiles go to the KDE backend (`forward()`).

Environment: `TACIT_PORTAL_QS` (default `qs`) and `TACIT_PORTAL_CONFIG` (default `end4-pC`) name the Quickshell
program and the shell config that runs Files.

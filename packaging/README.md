# What the tacit-portal package installs (tacit-packages/tacit-portal)

| File | Installed as |
|---|---|
| `build/tacit-portal` | `/usr/lib/tacit-portal/tacit-portal` |
| `org.freedesktop.impl.portal.desktop.tacit.service` | `/usr/share/dbus-1/services/` (the session bus starts the backend) |
| `../install/tacit.portal` | `/usr/share/xdg-desktop-portal/portals/tacit.portal` |
| `tacit-portals.conf` | `/usr/share/xdg-desktop-portal/tacit-portals.conf` (used when XDG_CURRENT_DESKTOP names Tacit) |
| `keyring-prompter/tacit-keyring-prompter` | `/usr/bin/tacit-keyring-prompter` |
| `org.gnome.keyring.SystemPrompter.service`, `...PrivatePrompter.service` | `/usr/share/tacit/dbus-1/services/`: the Tacit session puts `/usr/share/tacit` first in XDG_DATA_DIRS, so these win over gcr's own without replacing its files |
| `share-picker/tacit-share-picker` | `/usr/bin/tacit-share-picker` (the session's xdph.conf names it) |

The `install/*.sh` scripts are the hand install from before the package; they stay for a computer without it.

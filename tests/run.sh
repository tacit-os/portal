#!/bin/bash
# Runs inside the jail (see run-in-jail.sh): a private bus with the real /usr/lib/xdg-desktop-portal, the Tacit
# backend and a stand-in KDE backend; calls the portal's FileChooser with portal-call and prints the Response signals.
# Files is tests/bin/qs, which answers as Files does.
set -u
S=/out/stage          # binaries, install/ and tests/bin, copied in by run-in-jail.sh
T=/tmp/tp
export TP_OUT=/out TP_SCENARIO=$T/scenario FAKE_KDE_LOG=/out/kde.log
mkdir -p $T/portals $T/services $T/config/xdg-desktop-portal
"$S/install/link-portals.sh" $T/portals >/dev/null
cp "$S/install/hyprland-portals.conf" $T/config/xdg-desktop-portal/
sed "s|^Exec=.*|Exec=$S/tacit-portal|" "$S/install/org.freedesktop.impl.portal.desktop.tacit.service" >$T/services/tacit.service
cat >$T/services/kde.service <<EOF
[D-BUS Service]
Name=org.freedesktop.impl.portal.desktop.kde
Exec=$S/fake-kde
EOF
cat >$T/services/portal.service <<EOF
[D-BUS Service]
Name=org.freedesktop.portal.Desktop
Exec=/usr/lib/xdg-desktop-portal -v
EOF
cat >$T/bus.conf <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><keep_umask/><listen>unix:path=$T/bus</listen><auth>EXTERNAL</auth>
<servicedir>$T/services</servicedir>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
EOF
export PATH=$S/tests-bin:/usr/bin XDG_DESKTOP_PORTAL_DIR=$T/portals XDG_CONFIG_HOME=$T/config XDG_CURRENT_DESKTOP=Hyprland \
	XDG_DATA_DIRS=/usr/share QT_QPA_PLATFORM=offscreen DBUS_SESSION_BUS_ADDRESS=unix:path=$T/bus
dbus-daemon --config-file=$T/bus.conf --nofork >/out/bus.log 2>&1 &
busPid=$!
for _ in $(seq 50); do [ -S $T/bus ] && break; sleep 0.1; done
dbus-monitor --session >/out/monitor-bus.log 2>&1 &
sleep 0.5
PC=$S/portal-call
PATHS="[\"$HOME/Pictures/test/mid dag å.jpg\",\"$HOME/Pictures/test/dusk.jpg\"]"
scenario() { echo "$1" >$T/scenario; echo "Files answers: $1"; }

echo "=== OpenFile, single (Files offers two paths, the call wants one), current_folder"
scenario "choose $PATHS"; "$PC" OpenFile "Choose a picture" folder=$HOME/Pictures/test
echo; echo "=== OpenFile, multiple"
scenario "choose $PATHS"; "$PC" OpenFile "Choose pictures" multiple
echo; echo "=== OpenFile, directory (Choose with nothing selected: the folder Files shows)"
scenario "choosefolder"; "$PC" OpenFile "Choose a folder" directory folder=$HOME/Pictures
echo; echo "=== OpenFile, cancel (Files sends an empty list)"
scenario "cancel"; "$PC" OpenFile "Choose files"
echo; echo "=== OpenFile, no shell to ask"
scenario "noshell"; "$PC" OpenFile "Choose files"

echo; echo "=== two OpenFile at once: the second waits for the first"
scenario "choose $PATHS"
"$PC" OpenFile "First" multiple token=q1 >/out/q1.txt & p1=$!
sleep 0.3
"$PC" OpenFile "Second" token=q2 >/out/q2.txt & p2=$!
wait $p1 $p2
echo "first:"; cat /out/q1.txt; echo "second:"; cat /out/q2.txt
grep -n "qs -c" /out/qs-calls.log | tail -2 | cut -c1-200

echo; echo "=== OpenFile, then Request.Close before an answer, then a new pick"
scenario "hold"; "$PC" OpenFile "Held" token=c1 close=1500
sleep 1
echo "backend's reply to the frontend for the closed call:"
grep -B2 -A3 "^method return.*sender=:1.3 -> destination=:1.2" /out/monitor-bus.log | grep -B1 -A1 "uint32 2" | tail -3
scenario "choose $PATHS"; "$PC" OpenFile "After the close" token=c2

mkdir -p /tmp/sv && : >"/tmp/sv/taken.txt" && : >"/tmp/sv/b.txt"
echo; echo "=== SaveFile, name free: Files answers folder/name (Save)"
scenario "savename"; "$PC" SaveFile "Save as" name="new report.txt" folder=/tmp/sv
echo; echo "=== SaveFile, name taken, Replace: Files answers the same path"
scenario "savename"; "$PC" SaveFile "Save as" name="taken.txt" folder=/tmp/sv
echo; echo "=== SaveFile, name taken, Keep both: Files answers 'taken 2.txt'"
scenario "savekeep"; "$PC" SaveFile "Save as" name="taken.txt" folder=/tmp/sv
echo; echo "=== SaveFile, current_file gives folder and name"
scenario "savename"; "$PC" SaveFile "Save as" file=/tmp/sv/taken.txt
echo; echo "=== SaveFile, cancel"
scenario "cancel"; "$PC" SaveFile "Save as" name=x.txt folder=/tmp/sv
echo; echo "=== SaveFiles, folder chosen; b.txt exists there, so it comes back as 'b 2.txt'"
scenario "choosefolder"; "$PC" SaveFiles "Save files" files=a.txt,b.txt,b.txt folder=/tmp/sv
echo; echo "=== SaveFiles, cancel"
scenario "cancel"; "$PC" SaveFiles "Save files" files=a.txt folder=/tmp/sv
echo; echo "=== SaveFile and SaveFiles, no shell to ask: forwarded to the KDE stand-in"
scenario "noshell"; "$PC" SaveFile "Save as" name="my report.txt"
echo "what the stand-in KDE backend was sent:"; grep "kde: SaveFile " /out/kde.log
scenario "noshell"; "$PC" SaveFiles "Save files" files=x
grep "kde: SaveFiles" /out/kde.log
echo; echo "=== a Save waits behind an Open"
scenario "choose $PATHS"
"$PC" OpenFile "First" token=s1 >/out/s1.txt & p1=$!
sleep 0.3
scenario "savename"
"$PC" SaveFile "Second" name=q.txt folder=/tmp/sv token=s2 >/out/s2.txt & p2=$!
wait $p1 $p2
echo "open:"; cat /out/s1.txt; echo "save:"; cat /out/s2.txt

echo; echo "=== what Files was asked (qs calls)"; grep -v "^.* qs -p " /out/qs-calls.log | cut -c1-200
echo; echo "=== frontend log: which backend serves FileChooser"; grep "FileChooser" /out/bus.log | grep -i "tacit\|prefer\|config" | head
kill $busPid

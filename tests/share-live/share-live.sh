#!/bin/sh
# share-live.sh: does a window shared on its own keep moving while it is off screen? For a test computer's own desktop
# (it opens windows and moves them between workspaces 1 and 2): a foot printing the time, a Qt Quick window and a Thyris
# page that never stop moving, each shared by share-watch the way Hyprland's portal shares a window, while
#   shown      on the workspace shown
#   elsewhere  on a workspace not shown
#   covered    behind a fullscreen window on the workspace shown
# "different" is the frames a second that changed: a frozen share has 0.
#   share-live.sh                    the computer as it is
#   share-live.sh <tacit-share.so>   with the plugin loaded for the run
here=$(dirname "$(readlink -f "$0")")
plugin=${1:+$(readlink -f "$1")}
run() { setsid "$@" > /dev/null 2>&1 & echo $!; }
# the window of a process, else of a class (Thyris's, browser-cef, belongs to a child of the launcher)
addr() { for _ in $(seq 160); do a=$(hyprctl clients -j | python3 -c 'import json, re, sys
p, c = sys.argv[1], sys.argv[2]
w = [x for x in json.load(sys.stdin) if str(x["pid"]) == p or (c and re.search(c, x["class"], re.I))]
print(w[0]["address"] if w else "")' "$1" "$2"); [ -n "$a" ] && { echo "$a"; return; }; sleep 0.25; done; }
watch() { printf '  %-9s %-7s ' "$1" "$2"; "$here/share-watch" "$3" 5 30 | tail -3 | awk '{f += $3; d += $5} END {printf "frames %4.1f/s  different %4.1f/s\n", f / NR, d / NR}'; }
move() { for a in $windows; do hyprctl dispatch "hl.dsp.window.move({ workspace = \"$1\", follow = false, window = \"address:$a\" })" > /dev/null; done; }
each() { for w in "foot $clock" "qt $spin" "thyris $page"; do set -- $w; [ -n "$2" ] && watch "$case" "$1" "$2"; done; }

[ -x "$here/share-watch" ] || make -s -C "$here" share-watch || exit 1
hyprctl dispatch 'hl.dsp.focus({ workspace = "1" })' > /dev/null
[ -n "$plugin" ] && hyprctl plugin load "$plugin" > /dev/null
echo "plugin: $(hyprctl plugin list | grep -o 'tacit-share' || echo none)"

clock=$(addr "$(run foot sh -c 'while :; do date +%T.%N; sleep 0.01; done')")
spin=$(addr "$(run qml6 "$here/spinner.qml")")
page=$(addr "$(run thyris 'data:text/html,<body style="background:%231d1a19"><div id=b style="width:80px;height:80px;background:%23dfc0b2"></div><script>let t=0;(function f(){b.style.marginLeft=(t++%25400)+"px";requestAnimationFrame(f)})()</script>')" browser-cef)
windows="$clock $spin $page"
echo "windows: foot ${clock:-none}  qt ${spin:-none}  thyris ${page:-none}"
sleep 3

case=shown; each
move 2; sleep 0.5
case=elsewhere; each
move 1
cover=$(addr "$(run foot)")
hyprctl dispatch "hl.dsp.window.fullscreen({ mode = \"fullscreen\", action = \"set\", window = \"address:$cover\" })" > /dev/null
sleep 0.5
case=covered; each

# closed through Hyprland: killing the launchers' process groups left the windows open on Tacit (2026-10-23)
for a in $windows $cover; do hyprctl dispatch "hl.dsp.window.close({ window = \"address:$a\" })" > /dev/null; done
[ -n "$plugin" ] && hyprctl plugin unload "$plugin" > /dev/null
true

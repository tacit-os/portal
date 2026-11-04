#!/usr/bin/env python3
"""tacit-share-picker against bridge-daemon with a fake ui and a hyprctl shim (no Hyprland, no xdph).
usage: share-picker-test.py <bridge-daemon> <tacit-share-picker>"""
import json, os, socket, subprocess, sys, time
daemon, picker = sys.argv[1:3]
work = os.path.expanduser("~/.cache/tacit-dialogs/picker-test"); os.makedirs(work, mode=0o700, exist_ok=True)
sock = os.path.join(work, "d.sock")
bindir = os.path.join(work, "bin"); os.makedirs(bindir, exist_ok=True)
MONS = [{"id": 0, "name": "eDP-1", "width": 1920, "height": 1080, "x": 0, "y": 0, "scale": 1.0, "transform": 0, "focused": True}]
CLIENTS = [{"address": "0x1000", "class": "foot", "title": "~/work", "at": [10, 10], "size": [700, 400], "monitor": 0},
           {"address": "0x2000", "class": "huddle", "title": "Standup", "at": [800, 10], "size": [786, 620], "monitor": 0}]
open(os.path.join(bindir, "hyprctl"), "w").write("""#!/bin/sh
case "$2" in
monitors) cat <<'J'
%s
J
;; clients) cat <<'J'
%s
J
;; activewindow) cat <<'J'
%s
J
;; esac
""" % (json.dumps(MONS), json.dumps(CLIENTS), json.dumps(CLIENTS[1])))
os.chmod(os.path.join(bindir, "hyprctl"), 0o755)
open(os.path.join(bindir, "stock"), "w").write("#!/bin/sh\necho STOCK \"$@\"\n"); os.chmod(os.path.join(bindir, "stock"), 0o755)
LIST = f"{0x11}[HC>]foot[HT>]~/work[HE>]{0x1000}[HA>]{0x22}[HC>]huddle[HT>]Standup[HE>]{0x2000}[HA>]"
env = dict(os.environ, TACIT_DIALOGS_SOCK=sock, XDPH_WINDOW_SHARING_LIST=LIST, PATH=bindir + ":" + os.environ["PATH"],
           TACIT_STOCK_PICKER=os.path.join(bindir, "stock"), QT_QPA_PLATFORM="offscreen")
fails = []
def check(n, c, d=""):
    print(("ok   " if c else "FAIL ") + n + ((" " + str(d)) if not c and d else ""))
    if not c: fails.append(n)
class C:
    def __init__(s, role):
        s.s = socket.socket(socket.AF_UNIX); s.s.settimeout(8); s.s.connect(sock); s.buf = b""
        s.send(op="hello", role=role, proto=1); s.hello = s.recv()
    def send(s, **o): s.s.sendall(json.dumps(o).encode() + b"\n")
    def recv(s, t=8):
        s.s.settimeout(t)
        while b"\n" not in s.buf:
            d = s.s.recv(4096)
            if not d: return None
            s.buf += d
        l, s.buf = s.buf.split(b"\n", 1); return json.loads(l)
if os.path.exists(sock): os.unlink(sock)
d = subprocess.Popen(["nice", "-n", "10", daemon], env=env, stdout=subprocess.DEVNULL, stdin=subprocess.PIPE)
try:
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    def run(args=(), answer=None, ui=True, term=False):
        u = C("ui") if ui else None
        p = subprocess.Popen([picker, *args], env=env, stdout=subprocess.PIPE, text=True)
        ask = None
        if u:
            ask = u.recv()
            if term:
                p.terminate(); c = u.recv(); out, _ = p.communicate(); return ask, c, out, p.returncode
            if answer == "cancel": u.send(op="reject", id=ask["id"], reason="cancelled")
            elif answer is not None: u.send(op="answer", id=ask["id"], result=answer)
        out, _ = p.communicate(timeout=15)
        if u: u.s.close()
        return ask, None, out, p.returncode
    ask, _, out, rc = run(["--allow-token"], {"kind": "window", "id": "34", "token": True})
    pr = ask["params"]
    check("ask kind", ask["kind"] == "share.pick", ask)
    check("windows parsed with ids and hypr addresses", [(w["id"], w["address"]) for w in pr["windows"]] == [("17", "0x1000"), ("34", "0x2000")], pr["windows"])
    check("asker is the active window, on its monitor", pr["asker"] == {"x": 800, "y": 10, "w": 786, "h": 620} and pr["monitor"] == "eDP-1" and pr["appClass"] == "huddle", pr)
    check("screens and monitor size", pr["screens"] == [{"name": "eDP-1", "w": 1920, "h": 1080}] and pr["screen"]["w"] == 1920, pr)
    check("allowToken follows --allow-token", pr["allowToken"] is True)
    check("window + token line", out == "[SELECTION]r/window:34\n" and rc == 0, out)
    _, _, out, rc = run([], {"kind": "screen", "name": "eDP-1", "token": False})
    check("screen line, no flag", out == "[SELECTION]/screen:eDP-1\n", out)
    ask, _, out, rc = run([], {"kind": "region", "output": "eDP-1", "x": 330.4, "y": 250, "w": 760, "h": 470, "token": False})
    check("region line", out == "[SELECTION]/region:eDP-1@330,250,760,470\n", out)
    check("allowToken false without the flag", ask["params"]["allowToken"] is False)
    _, _, out, rc = run([], "cancel")
    check("cancel prints nothing and exits 1", out == "" and rc == 1, (out, rc))
    _, _, out, rc = run(["--allow-token"], None, ui=False)
    check("no shell: stock picker, args kept", out.startswith("STOCK --allow-token"), out)
    ask, c, out, rc = run([], None, term=True)
    check("picker killed: the sheet is withdrawn", c == {"op": "cancel", "id": ask["id"]} and out == "", c)
    env2 = dict(env, TACIT_DIALOGS_SOCK=os.path.join(work, "none.sock"), PATH=bindir + ":/usr/bin:/bin")
    r = subprocess.run([picker], env=env2, capture_output=True, text=True, timeout=30)
    check("no portal at all: stock picker", r.stdout.startswith("STOCK"), r.stdout + r.stderr)
finally:
    d.terminate(); d.wait()
print("FAILED: %s" % fails if fails else "all passed")
sys.exit(1 if fails else 0)

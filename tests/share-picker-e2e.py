#!/usr/bin/env python3
"""Whole chain without the owner's session: kwin_wayland --virtual (own socket), bridge-daemon, the real shell wrapper
SharePicker.qml + DialogBridge.qml under `qs -p` (layer-shell window on the virtual output), and tacit-share-picker
with a hyprctl shim. The answer is given over qs ipc (a click cannot be sent here); the sheet's own click/key paths are
covered by qs-ui/share-picker-interact.qml.
usage: share-picker-e2e.py <bridge-daemon> <tacit-share-picker> <shell tree with e2e-shell.qml>"""
import json, os, signal, subprocess, sys, time
daemon, picker, tree = [os.path.abspath(a) for a in sys.argv[1:4]]
work = os.path.expanduser("~/.cache/tacit-dialogs/e2e"); os.makedirs(work, exist_ok=True)
rt = "/run/user/%d" % os.getuid(); sock = os.path.join(work, "d.sock"); disp = "tdlg"
shim = os.path.join(work, "bin"); os.makedirs(shim, exist_ok=True)
J = lambda o: json.dumps(o)
open(os.path.join(shim, "hyprctl"), "w").write("""#!/bin/sh
case "$2" in
monitors) echo '%s';; clients) echo '%s';; activewindow) echo '%s';; esac
""" % (J([{"id": 0, "name": "Virtual-1", "width": 1600, "height": 1000, "x": 0, "y": 0, "scale": 1.0, "transform": 0, "focused": True}]),
        J([{"address": "0x1000", "class": "foot", "title": "~/work", "at": [10, 10], "size": [700, 400], "monitor": 0}]),
        J({"address": "0x1000", "class": "foot", "title": "~/work", "at": [10, 10], "size": [700, 400], "monitor": 0})))
os.chmod(os.path.join(shim, "hyprctl"), 0o755)
LIST = f"{0x11}[HC>]foot[HT>]~/work[HE>]{0x1000}[HA>]"
base = dict(os.environ, TACIT_DIALOGS_SOCK=sock)
fails = []
def check(n, c, d=""):
    print(("ok   " if c else "FAIL ") + n + ((" " + str(d)) if not c and d else ""))
    if not c: fails.append(n)
procs = []
def spawn(args, **kw):
    p = subprocess.Popen(args, **kw); procs.append(p); return p
try:
    if os.path.exists(sock): os.unlink(sock)
    spawn(["nice", "-n", "10", "kwin_wayland", "--virtual", "--socket", disp, "--no-lockscreen", "--no-global-shortcuts", "--width", "1600", "--height", "1000"],
          env=dict(base, XDG_RUNTIME_DIR=rt), stdout=open(work + "/kwin.log", "w"), stderr=subprocess.STDOUT)
    spawn([daemon], env=dict(base, QT_QPA_PLATFORM="offscreen"), stdout=subprocess.DEVNULL, stdin=subprocess.PIPE)
    for _ in range(100):
        if os.path.exists(rt + "/" + disp): break
        time.sleep(0.1)
    qlog = work + "/qs.log"
    qenv = dict(base, WAYLAND_DISPLAY=disp, QT_QPA_PLATFORM="wayland", QT_LOGGING_RULES="", QT_FORCE_STDERR_LOGGING="1",
                XDG_STATE_HOME=work + "/state")
    qs = ["qs", "-p", os.path.join(tree, "e2e-shell.qml"), "--no-duplicate"]
    spawn(["nice", "-n", "10"] + qs, env=qenv, stdout=open(qlog, "w"), stderr=subprocess.STDOUT)
    def log(): return open(qlog, errors="replace").read()
    def until(pred, t=15):
        s = time.time()
        while time.time() - s < t:
            if pred(): return True
            time.sleep(0.2)
        return False
    def ui_connected():
        import socket
        try:
            c = socket.socket(socket.AF_UNIX); c.settimeout(2); c.connect(sock)
            c.sendall(b'{"op":"hello","role":"client","proto":1}\n'); r = json.loads(c.recv(512).split(b"\n")[0]); c.close()
            return r.get("ui")
        except (OSError, ValueError): return False
    check("shell connects to the bridge", until(ui_connected, 30), log()[-800:])
    penv = dict(base, XDPH_WINDOW_SHARING_LIST=LIST, PATH=shim + ":" + os.environ["PATH"])
    p = spawn([picker, "--allow-token"], env=penv, stdout=subprocess.PIPE, text=True)
    check("sheet comes up on the asker's monitor with its windows", until(lambda: "E2E up=true monitor=Virtual-1 windows=1" in log()), log()[-800:])
    time.sleep(1.5)
    check("a layer window is on the virtual compositor (no QML errors)", "ERROR" not in log().split("E2E up=true")[-1] and "TypeError" not in log(), log()[-800:])
    r = subprocess.run(qs + ["ipc", "call", "e2e", "answer"], env=qenv, capture_output=True, text=True, timeout=10)
    out = p.communicate(timeout=10)[0]
    check("answer reaches xdph's stdout in its format", out == "[SELECTION]r/window:34\n", (out, r.stderr))
    check("sheet closes after the answer", until(lambda: "E2E up=false" in log()))
    # dismissed
    p = spawn([picker], env=penv, stdout=subprocess.PIPE, text=True)
    until(lambda: log().count("E2E up=true") >= 2)
    subprocess.run(qs + ["ipc", "call", "e2e", "dismiss"], env=qenv, capture_output=True, text=True, timeout=10)
    out = p.communicate(timeout=10)[0]
    check("dismissed: nothing printed, picker exits 1", out == "" and p.returncode == 1, (out, p.returncode))
    # picker killed while the sheet is up: the sheet goes away
    n = log().count("E2E up=false")
    p = spawn([picker], env=penv, stdout=subprocess.PIPE, text=True)
    until(lambda: log().count("E2E up=true") >= 3)
    p.terminate(); p.wait()
    check("picker killed: sheet withdrawn", until(lambda: log().count("E2E up=false") > n), log()[-500:])
finally:
    for p in procs:
        try: p.terminate()
        except OSError: pass
    for p in procs:
        try: p.wait(5)
        except Exception: p.kill()
print("FAILED: %s" % fails if fails else "all passed")
sys.exit(1 if fails else 0)

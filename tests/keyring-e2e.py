#!/usr/bin/env python3
"""Whole chain without the owner's session: kwin_wayland --virtual, private D-Bus, real gnome-keyring-daemon,
tacit-keyring-prompter (D-Bus activated), bridge-daemon and the real Keyring.qml + DialogBridge.qml under `qs -p`
(layer-shell window on the virtual output). The answers are given over qs ipc (a click cannot be sent here; the sheet's
own keys are covered by qs-ui/keyring-interact.qml). Made-up passwords only.
usage: keyring-e2e.py <bridge-daemon> <tacit-keyring-prompter> <shell tree with e2e-keyring-shell.qml>"""
import os, shutil, subprocess, sys, time
daemon, prompter, tree = [os.path.abspath(a) for a in sys.argv[1:4]]
work = os.path.expanduser("~/.cache/tacit-dialogs/kp/e2e"); shutil.rmtree(work, ignore_errors=True)
for d in ("rt", "data", "home", "services", "state"): os.makedirs(os.path.join(work, d), mode=0o700)
os.chmod(work, 0o700)
realrt = "/run/user/%d" % os.getuid(); sock = os.path.join(work, "d.sock"); disp = "tkp"
busaddr = "unix:path=" + work + "/bus"
PW1, WRONG = "made-up-pass-one", "made-up-wrong"
fails = []
# gnome-keyring's own way in for a password already known (what the lock's PAM check does at boot): unlock a collection
# with its password over a plain session. argv: collection path, password (made up).
UNLOCK = r"""
import sys
from gi.repository import Gio, GLib
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
def call(path, iface, method, args, ret):
    return bus.call_sync("org.freedesktop.secrets", path, iface, method, args, GLib.VariantType(ret), 0, 10000, None)
_, session = call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "OpenSession",
                  GLib.Variant("(sv)", ("plain", GLib.Variant("s", ""))), "(vo)").unpack()
call("/org/freedesktop/secrets", "org.gnome.keyring.InternalUnsupportedGuiltRiddenInterface", "UnlockWithMasterPassword",
     GLib.Variant("(o(oayays))", (sys.argv[1], (session, b"", sys.argv[2].encode(), "text/plain"))), "()")
"""
def check(n, c, d=""):
    print(("ok   " if c else "FAIL ") + n + ((" " + str(d)) if not c and d else ""))
    if not c: fails.append(n)
open(work + "/bus.conf", "w").write(f"""<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>{busaddr}</listen><auth>EXTERNAL</auth><servicedir>{work}/services</servicedir>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
""")
for n in ("SystemPrompter", "PrivatePrompter"):
    open(f"{work}/services/org.gnome.keyring.{n}.service", "w").write(f"[D-BUS Service]\nName=org.gnome.keyring.{n}\nExec={prompter}\n")
base = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=busaddr, TACIT_DIALOGS_SOCK=sock)
kenv = dict(base, HOME=work + "/home", XDG_DATA_HOME=work + "/data", XDG_RUNTIME_DIR=work + "/rt", XDG_CONFIG_HOME=work + "/home/.config")
kenv.pop("GNOME_KEYRING_CONTROL", None); kenv.pop("SSH_AUTH_SOCK", None)
procs = []
def spawn(args, **kw):
    p = subprocess.Popen(args, **kw); procs.append(p); return p
def tool(*a, inp=None, timeout=40):
    return subprocess.run(["secret-tool"] + list(a), env=kenv, input=inp, capture_output=True, text=True, timeout=timeout)
qlog = work + "/qs.log"
def log(): return open(qlog, errors="replace").read()
def until(pred, t=15):
    s = time.time()
    while time.time() - s < t:
        if pred(): return True
        time.sleep(0.2)
    return False
qenv = dict(base, WAYLAND_DISPLAY=disp, QT_QPA_PLATFORM="wayland", QT_LOGGING_RULES="", QT_FORCE_STDERR_LOGGING="1",
            XDG_STATE_HOME=work + "/state", XDG_RUNTIME_DIR=realrt)
qs = ["qs", "-p", os.path.join(tree, "e2e-keyring-shell.qml"), "--no-duplicate"]
def ipc(*a): return subprocess.run(qs + ["ipc", "call", "e2e"] + list(a), env=qenv, capture_output=True, text=True, timeout=10)
try:
    spawn(["dbus-daemon", "--config-file=" + work + "/bus.conf", "--nofork"], env=dict(base, TACIT_KEYRING_DEBUG="1"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    until(lambda: os.path.exists(work + "/bus"))
    spawn(["nice", "-n", "10", "kwin_wayland", "--virtual", "--socket", disp, "--no-lockscreen", "--no-global-shortcuts", "--width", "1600", "--height", "1000"],
          env=dict(base, XDG_RUNTIME_DIR=realrt), stdout=open(work + "/kwin.log", "w"), stderr=subprocess.STDOUT)
    spawn([daemon], env=dict(base, QT_QPA_PLATFORM="offscreen"), stdout=subprocess.DEVNULL, stdin=subprocess.PIPE)
    until(lambda: os.path.exists(realrt + "/" + disp) and os.path.exists(sock))
    spawn(["gnome-keyring-daemon", "--foreground", "--components=secrets"], env=kenv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    spawn(["nice", "-n", "10"] + qs, env=qenv, stdout=open(qlog, "w"), stderr=subprocess.STDOUT)
    time.sleep(6)

    # 1. a new keyring: the sheet comes up on the virtual output and the answer creates it
    st = subprocess.Popen(["secret-tool", "store", "--label=t", "k", "v"], env=kenv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    st.stdin.write(PW1); st.stdin.close()
    check("new keyring: sheet is asked for", until(lambda: "E2E asking=true kind=keyring.password" in log(), 40), log()[-600:])
    time.sleep(1.5)
    check("a layer window is up (no QML errors)", "E2E up=true" in log() and "TypeError" not in log() and "ReferenceError" not in log(), log()[-600:])
    ipc("password", PW1)
    rc = st.wait(timeout=30)
    check("answer reaches gnome-keyring: store succeeds", rc == 0, st.stderr.read()[:200])
    check("sheet folds after the answer", until(lambda: "E2E up=false" in log()))

    # 2. locked + wrong password: the sheet stays up through the retry (no fold between)
    import re
    path = re.search(r'"([^"]+)"', subprocess.run(["busctl", "--user", "call", "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "ReadAlias", "s", "default"], env=kenv, capture_output=True, text=True).stdout).group(1)
    subprocess.run(["busctl", "--user", "call", "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "Lock", "ao", "1", path], env=kenv, capture_output=True)
    ups0 = log().count("E2E up=true")
    lk = subprocess.Popen(["secret-tool", "lookup", "k", "v"], env=kenv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    check("unlock: sheet is asked for", until(lambda: log().count("E2E asking=true kind=keyring.password retry=false") >= 2), log()[-600:])
    ipc("password", WRONG)
    check("wrong password: asked again as a retry", until(lambda: "retry=true" in log()), log()[-600:])
    downs = log().count("E2E up=false")
    time.sleep(1.2)
    check("the sheet did not fold and reopen for the retry", log().count("E2E up=true") == ups0 + 1 and log().count("E2E up=false") == downs, (log().count("E2E up=true"), ups0, log().count("E2E up=false"), downs))
    ipc("password", PW1)
    out = lk.communicate(timeout=30)[0]
    check("right password: lookup returns the value", lk.returncode == 0 and out.strip() == PW1, lk.returncode)

    # 3. a dialog waits while the screen is locked and appears when it unlocks
    subprocess.run(["busctl", "--user", "call", "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "Lock", "ao", "1", path], env=kenv, capture_output=True)
    time.sleep(1)
    ipc("lock", "true"); time.sleep(0.5)
    n = log().count("E2E asking=true")
    lk = subprocess.Popen(["secret-tool", "lookup", "k", "v"], env=kenv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(3)
    check("screen locked: nothing shown", log().count("E2E asking=true") == n)
    ipc("lock", "false")
    check("screen unlocked: the waiting dialog appears", until(lambda: log().count("E2E asking=true") > n), log()[-400:])
    ipc("refuse")
    lk.communicate(timeout=30)
    check("refused: lookup fails", lk.returncode != 0)

    # 4. an unlock ask set off behind the lock whose keyring is then opened another way (at boot, the lock's own PAM
    #    check): moot, so the shell drops it and nothing shows when the screen unlocks (Keyring.qml, all_unlocked.sh)
    subprocess.run(["busctl", "--user", "call", "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "Lock", "ao", "1", path], env=kenv, capture_output=True)
    ipc("lock", "true"); time.sleep(0.5)
    n = log().count("E2E asking=true")
    lk = subprocess.Popen(["secret-tool", "lookup", "k", "v"], env=kenv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(2)
    opened = subprocess.run([sys.executable, "-c", UNLOCK, path, PW1], env=kenv, capture_output=True, text=True, timeout=20)
    check("(the keyring opened behind the lock, as PAM opens it)", opened.returncode == 0, opened.stderr[-300:])
    time.sleep(2.5)
    ipc("lock", "false")
    time.sleep(2)
    check("moot unlock ask: nothing shown when the screen unlocks", log().count("E2E asking=true") == n, log()[-400:])
    try:
        lk.communicate(timeout=20); ended = True
    except subprocess.TimeoutExpired:
        lk.kill(); ended = False
    check("its asker is answered (not left waiting)", ended)
finally:
    for p in reversed(procs):
        try: p.terminate()
        except OSError: pass
    for p in procs:
        try: p.wait(5)
        except Exception: p.kill()
print("FAILED: %s" % fails if fails else "all passed")
sys.exit(1 if fails else 0)

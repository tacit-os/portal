#!/usr/bin/env python3
"""tacit-keyring-prompter against a real gnome-keyring-daemon, on a private D-Bus session, with a fake shell on a
private bridge socket. Nothing of the owner's is touched: own HOME/XDG dirs, own bus, made-up passwords only.
usage: keyring-test.py <bridge-daemon> <tacit-keyring-prompter>"""
import json, os, shutil, signal, socket, subprocess, sys, threading, time

daemon, prompter = [os.path.abspath(a) for a in sys.argv[1:3]]
work = os.path.expanduser("~/.cache/tacit-dialogs/kp/run")
shutil.rmtree(work, ignore_errors=True)
for d in ("rt", "data", "home", "services"): os.makedirs(os.path.join(work, d), mode=0o700)
os.chmod(work, 0o700)
rt = os.path.join(work, "rt"); sock = os.path.join(work, "d.sock"); busaddr = "unix:path=" + os.path.join(work, "bus")
PW1, PW2, WRONG = "made-up-pass-one", "made-up-pass-two", "made-up-wrong"
fails = []
def check(n, c, d=""):
    print(("ok   " if c else "FAIL ") + n + ((" " + str(d)) if not c and d else ""))
    if not c: fails.append(n)

open(os.path.join(work, "bus.conf"), "w").write(f"""<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>{busaddr}</listen><auth>EXTERNAL</auth>
<servicedir>{work}/services</servicedir>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
""")
for n in ("SystemPrompter", "PrivatePrompter"):
    open(f"{work}/services/org.gnome.keyring.{n}.service", "w").write(
        f"[D-BUS Service]\nName=org.gnome.keyring.{n}\nExec={prompter}\n")
env = dict(os.environ, HOME=work + "/home", XDG_DATA_HOME=work + "/data", XDG_RUNTIME_DIR=rt, XDG_CONFIG_HOME=work + "/home/.config",
           XDG_CACHE_HOME=work + "/home/.cache", DBUS_SESSION_BUS_ADDRESS=busaddr, TACIT_DIALOGS_SOCK=sock,
           TACIT_KEYRING_NOSHELL_WAIT="4", TACIT_KEYRING_DEBUG="1", QT_QPA_PLATFORM="offscreen")
env.pop("GNOME_KEYRING_CONTROL", None); env.pop("SSH_AUTH_SOCK", None)
procs = []
def spawn(args, **kw):
    p = subprocess.Popen(["nice", "-n", "10"] + args, env=env, **kw); procs.append(p); return p

class Ui:
    """The shell's side: reads asks, answers by a rule."""
    def __init__(s, rule):
        s.rule, s.asks, s.cancels = rule, [], []
        s.s = socket.socket(socket.AF_UNIX); s.s.connect(sock)
        s.send(op="hello", role="ui", proto=1)
        s.buf = b""; s.alive = True
        threading.Thread(target=s.run, daemon=True).start()
    def send(s, **o): s.s.sendall(json.dumps(o).encode() + b"\n")
    def run(s):
        while s.alive:
            try: d = s.s.recv(4096)
            except OSError: return
            if not d: return
            s.buf += d
            while b"\n" in s.buf:
                l, s.buf = s.buf.split(b"\n", 1)
                m = json.loads(l)
                if m.get("op") == "cancel": s.cancels.append(m["id"])
                if m.get("op") == "ask":
                    s.asks.append(m)
                    r = s.rule(m, len(s.asks))
                    if r is None: continue
                    if "reject" in r: s.send(op="reject", id=m["id"], reason=r["reject"])
                    else: s.send(op="answer", id=m["id"], result=r)
    def close(s):
        s.alive = False; s.s.close()

def tool(*a, inp=None, timeout=20):
    return subprocess.run(["secret-tool"] + list(a), env=env, input=inp, capture_output=True, text=True, timeout=timeout)

def lock_default():
    out = subprocess.run(["busctl", "--user", "call", "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service",
                          "ReadAlias", "s", "default"], env=env, capture_output=True, text=True).stdout
    path = out.split('"')[1]
    subprocess.run(["busctl", "--user", "call", "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service",
                    "Lock", "ao", "1", path], env=env, capture_output=True, text=True)
    return path

def prompter_running():
    return subprocess.run(["pgrep", "-f", prompter], capture_output=True).returncode == 0   # test-only: path is unique to this build

try:
    spawn(["dbus-daemon", "--config-file=" + work + "/bus.conf", "--nofork"], stdout=subprocess.DEVNULL, stderr=open(work + "/bus.err", "w"))
    for _ in range(50):
        if os.path.exists(work + "/bus"): break
        time.sleep(0.1)
    spawn([daemon], stdout=subprocess.DEVNULL, stdin=subprocess.PIPE)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.1)
    spawn(["gnome-keyring-daemon", "--foreground", "--components=secrets"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)

    # 1. no shell: the first prompt (new keyring) must end, not hang, after the short wait
    t0 = time.time()
    r = tool("store", "--label=t", "k", "v", inp=PW1)
    dt = time.time() - t0
    check("no shell: store fails within the wait", r.returncode != 0 and dt < 15, (r.returncode, dt, r.stderr[:200]))

    # 2. a shell that appears during the wait gets the ask (retry)
    def rule_new(m, n):
        p = m["params"]
        if p["mode"] == "password" and p["passwordNew"]: return {"reply": "continue", "password": PW1, "choice": False}
        if p["mode"] == "confirm": return {"reply": "continue", "choice": False}
        return {"reject": "refused"}
    ui = Ui(rule_new)
    r = tool("store", "--label=t", "k", "v", inp=PW1)
    kinds = [(a["kind"], a["params"]["mode"], a["params"]["passwordNew"]) for a in ui.asks]
    print("   new-keyring asks:", kinds)
    for a in ui.asks:
        p = a["params"]; print("   params:", {k: v for k, v in p.items()})
    check("new keyring: password ask arrives", ("keyring.password", "password", True) in kinds, kinds)
    check("new keyring: a real password needs no 'store unencrypted?' confirm (password-strength is set)", not any(k[1] == "confirm" for k in kinds), kinds)
    check("new keyring: store succeeds", r.returncode == 0, r.stderr[:200])
    check("params carry no password", all("password" not in a["params"] for a in ui.asks))
    check("ask kinds are keyring.*", all(a["kind"].startswith("keyring.") for a in ui.asks))

    # 3. locked: unlock with a wrong password first -> same prompt asked again with a warning
    ui.close(); time.sleep(0.3)
    def rule_unlock(m, n):
        p = m["params"]
        if p["mode"] == "password": return {"reply": "continue", "password": WRONG if n == 1 else PW1, "choice": False}
        return {"reply": "continue", "choice": False}
    ui = Ui(rule_unlock)
    lock_default()
    r = tool("lookup", "k", "v")
    pws = [a["params"] for a in ui.asks if a["params"]["mode"] == "password"]
    print("   unlock asks:", [(p["title"], p["message"], p["description"], p["warning"], p["choiceLabel"], p["retry"]) for p in pws])
    check("unlock: wrong then right = two asks", len(pws) >= 2, len(pws))
    check("unlock: second ask is a retry with a warning", len(pws) >= 2 and pws[1]["retry"] and pws[1]["warning"] != "", pws[1:] )
    check("unlock: first ask is not a retry", pws and not pws[0]["retry"])
    check("unlock: lookup returns the stored value", r.returncode == 0 and r.stdout.strip() == PW1, (r.returncode, r.stderr[:100]))

    # 3b. the shell restarts under the ask (shell-gone) or its sheet is not loaded yet (unsupported): the prompt is asked again
    ui.close(); time.sleep(0.3)
    def rule_gone(m, n):
        if n == 1: return {"reject": "shell-gone"}
        if n == 2: return {"reject": "unsupported"}
        return {"reply": "continue", "password": PW1, "choice": False} if m["params"]["mode"] == "password" else {"reply": "continue", "choice": False}
    ui = Ui(rule_gone)
    lock_default()
    r = tool("lookup", "k", "v", timeout=30)
    check("shell-gone then unsupported are retried, then the unlock goes through", len(ui.asks) >= 3 and r.returncode == 0 and r.stdout.strip() == PW1, (len(ui.asks), r.returncode, r.stderr[:100]))

    # 4. refuse: the lookup fails at once, no hang
    ui.close(); time.sleep(0.3)
    ui = Ui(lambda m, n: {"reject": "refused"})
    lock_default()
    t0 = time.time(); r = tool("lookup", "k", "v")
    check("refused: lookup fails promptly", r.returncode != 0 and time.time() - t0 < 10, (r.returncode, time.time() - t0))

    # 5. cancelled sheet (reject cancelled) behaves the same
    ui.close(); time.sleep(0.3)
    ui = Ui(lambda m, n: {"reject": "cancelled"})
    lock_default()
    r = tool("lookup", "k", "v")
    check("cancelled: lookup fails", r.returncode != 0)

    # 6. the requester (secret-tool) dying while the sheet is open: the ask is withdrawn
    ui.close(); time.sleep(0.3)
    ui = Ui(lambda m, n: None)
    lock_default()
    p = subprocess.Popen(["secret-tool", "lookup", "k", "v"], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(2.5)
    check("pending ask reached the ui", len(ui.asks) >= 1)
    p.kill(); p.wait()
    # the daemon notices the caller left and stops the prompt: the bridge sends cancel to the ui
    time.sleep(2)
    # gnome-keyring does not withdraw a prompt when its caller dies (same with the stock dialog): informational only
    print("   cancels sent to the shell after the caller died:", len(ui.cancels))
    ui.close(); time.sleep(0.3)
    ui = Ui(lambda m, n: {"reply": "continue", "password": PW1, "choice": False})
    r = tool("lookup", "k", "v")
    check("after an abandoned ask, the next unlock still works", r.returncode == 0 and r.stdout.strip() == PW1, (r.returncode, r.stderr[:150]))
    ui.close()
finally:
    for p in reversed(procs):
        try: p.terminate()
        except Exception: pass
    for p in procs:
        try: p.wait(timeout=5)
        except Exception: p.kill()
print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)

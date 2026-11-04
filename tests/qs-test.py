#!/usr/bin/env python3
"""The real services/DialogBridge.qml under `qs -p` against bridge-daemon: late daemon (retry), a dialog waiting
while locked, answer, refuse, cancel, daemon death and return.
usage: qs-test.py <bridge-daemon> <path to DialogBridge.qml>"""
import json, os, shutil, signal, socket, subprocess, sys, time
daemon, qml = sys.argv[1], sys.argv[2]
base = os.path.expanduser("~/.cache/tacit-dialogs/qs-run")
shutil.rmtree(base, ignore_errors=True)
cfg = os.path.join(base, "cfg"); run = os.path.join(base, "run")
os.makedirs(cfg); os.makedirs(run, mode=0o700)
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qs-ui")
for f in ("GlobalStates.qml", "shell.qml"): shutil.copy(os.path.join(here, f), cfg)
shutil.copy(qml, cfg)
sock = os.path.join(base, "d.sock"); log = os.path.join(base, "qs.log")
env = dict(os.environ, TACIT_DIALOGS_SOCK=sock, XDG_RUNTIME_DIR=run, QT_QPA_PLATFORM="offscreen",
           QT_LOGGING_RULES="", QT_FORCE_STDERR_LOGGING="1")
fails = []
def check(n, c, d=""):
    print(("ok   " if c else "FAIL ") + n + ((" " + str(d)) if not c and d else "")); 
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
def start_daemon(): 
    p = subprocess.Popen(["nice", "-n", "10", daemon], env=env, stdout=subprocess.DEVNULL, stdin=subprocess.PIPE)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    return p
def ui_up(timeout=10):
    t = time.time()
    while time.time() - t < timeout:
        try:
            c = C("client")
            if c.hello["ui"]: return c
            c.s.close()
        except OSError: pass
        time.sleep(0.3)
def logtext():
    return open(log, errors="replace").read() if os.path.exists(log) else ""

q = subprocess.Popen(["nice", "-n", "10", "qs", "-p", cfg, "--no-duplicate"], env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
t0 = time.time()
d = None
try:
    time.sleep(1.5)
    d = start_daemon()          # the shell is already running and has failed to connect once
    c = ui_up()
    check("shell connects to a daemon that started after it", c is not None, logtext()[-600:])
    if c is None: raise SystemExit
    c.send(op="ask", id="e1", kind="test.echo", params={"x": "hello"})
    r = c.recv(); dt = time.time() - t0
    check("answered", r == {"op": "answer", "id": "e1", "result": {"v": "hello"}}, r)
    check("dialog waited for the lock (answered after ~4 s)", dt >= 3.8, round(dt, 1))
    t0 = time.time(); c.send(op="ask", id="u1", kind="test.unclaimed"); r = c.recv()
    check("unclaimed kind rejected unsupported, after the grace", r.get("reason") == "unsupported" and 2.5 < time.time() - t0 < 6, (r, time.time() - t0))
    c.send(op="ask", id="n1", kind="test.no"); r = c.recv()
    check("refuse carries its reason", r == {"op": "reject", "id": "n1", "reason": "refused"}, r)
    c.send(op="ask", id="h1", kind="test.hold"); time.sleep(0.5)
    c.send(op="cancel", id="h1"); r = c.recv()
    check("cancel", r == {"op": "reject", "id": "h1", "reason": "cancelled"}, r)
    time.sleep(0.3)
    c.send(op="ask", id="e2", kind="test.echo", params={"x": "again"}); r = c.recv()
    check("queue is clear after a cancel (next ask answered)", r["result"] == {"v": "again"}, r)
    c.send(op="ask", id="h2", kind="test.hold"); time.sleep(0.4)
    d.terminate(); d.wait()
    try: r = c.recv(3)
    except OSError: r = None
    check("daemon death closes requesters (nothing hangs)", r is None, r)
    os.path.exists(sock) and os.unlink(sock)
    d = start_daemon()
    c = ui_up()
    check("shell reconnects after the daemon returns", c is not None, logtext()[-600:])
    if c:
        c.send(op="ask", id="e3", kind="test.echo", params={"x": "back"})
        check("and the queue was emptied (e3 is answered, h2 is not shown first)", c.recv()["result"] == {"v": "back"})
    lt = logtext()
    check("no QML errors in the log", "ReferenceError" not in lt and "TypeError" not in lt and "Failed to load" not in lt, lt[-600:])
finally:
    q.send_signal(signal.SIGTERM)
    try: q.wait(5)
    except Exception: q.kill()
    if d: d.terminate()
print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)

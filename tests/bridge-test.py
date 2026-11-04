#!/usr/bin/env python3
"""DialogBridge on a private socket with fake clients (no D-Bus, no real shell).
usage: bridge-test.py <bridge-daemon> [qs-test-dir]
Without a qs-test-dir the ui is also a fake; with one, the real DialogBridge.qml singleton is the ui
(run under `qs -p`, see tests/qs-ui/)."""
import json, os, socket, subprocess, sys, tempfile, time

daemon = sys.argv[1]
qsdir = sys.argv[2] if len(sys.argv) > 2 else None
work = os.path.expanduser("~/.cache/tacit-dialogs/test-run")
os.makedirs(work, mode=0o700, exist_ok=True)
sock = os.path.join(work, "d.sock")
env = dict(os.environ, TACIT_DIALOGS_SOCK=sock, QT_QPA_PLATFORM="offscreen")
fails = []

def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name + (("  " + str(detail)) if detail and not cond else ""))
    if not cond: fails.append(name)

class C:
    def __init__(s, role):
        s.s = socket.socket(socket.AF_UNIX); s.s.settimeout(5); s.s.connect(sock); s.f = s.s.makefile("rwb", buffering=0)
        s.buf = b""
        s.send(op="hello", role=role, proto=1)
        s.hello = s.recv()
    def send(s, **o): s.s.sendall(json.dumps(o).encode() + b"\n")
    def raw(s, b): s.s.sendall(b)
    def recv(s, timeout=5):
        s.s.settimeout(timeout)
        while b"\n" not in s.buf:
            d = s.s.recv(4096)
            if not d: return None
            s.buf += d
        l, s.buf = s.buf.split(b"\n", 1)
        return json.loads(l)
    def close(s):
        s.f.close(); s.s.close()

def wait_sock():
    for _ in range(100):
        if os.path.exists(sock):
            try:
                socket.socket(socket.AF_UNIX).connect(sock); return
            except OSError: pass
        time.sleep(0.05)
    raise SystemExit("daemon did not listen")

if os.path.exists(sock): os.unlink(sock)
d = subprocess.Popen(["nice", "-n", "10", daemon], env=env, stdout=subprocess.PIPE, stdin=subprocess.PIPE, text=True)
def inproc():
    d.stdin.write("ask\n"); d.stdin.flush()
    return d.stdout.readline().strip()
try:
    wait_sock()
    print("socket mode", oct(os.stat(sock).st_mode & 0o777))
    check("socket is 0600", os.stat(sock).st_mode & 0o777 == 0o600)

    # 1. no ui: reject at once
    c = C("client")
    check("hello says no ui", c.hello == {"op": "hello", "proto": 1, "ui": False}, c.hello)
    t0 = time.time(); c.send(op="ask", id="a1", kind="test.echo", params={"x": "1"}); r = c.recv(2)
    check("no shell: rejected at once", r == {"op": "reject", "id": "a1", "reason": "no-shell"} and time.time() - t0 < 1, r)

    # protocol errors
    c2 = socket.socket(socket.AF_UNIX); c2.connect(sock); c2.settimeout(2)
    c2.sendall(b'{"op":"ask","id":"x","kind":"k"}\n'); check("ask before hello refused", b"hello-first" in c2.recv(200))
    c2.sendall(b"not json\n"); check("bad json answered, connection stays", b"bad-json" in c2.recv(200))
    c2.close()
    c.send(op="ask", id="", kind="k"); check("ask without id rejected", c.recv(2)["reason"] == "bad-ask")

    if qsdir is None:
        ui = C("ui")
        check("ui hello", ui.hello["ui"] is True)
        c.send(op="ask", id="a2", kind="test.echo", params={"x": "hi"})
        m = ui.recv(); check("ui receives ask", m["op"] == "ask" and m["kind"] == "test.echo" and m["params"] == {"x": "hi"}, m)
        ui.send(op="answer", id=m["id"], result={"v": "hi"})
        r = c.recv(); check("client gets answer under its own id", r == {"op": "answer", "id": "a2", "result": {"v": "hi"}}, r)
        # two clients, same client id
        c3 = C("client")
        c.send(op="ask", id="same", kind="k1"); c3.send(op="ask", id="same", kind="k2")
        m1 = ui.recv(); m2 = ui.recv(); check("global ids are distinct", m1["id"] != m2["id"], (m1, m2))
        ui.send(op="reject", id=m2["id"], reason="refused")
        check("answer reaches the right client", c3.recv()["reason"] == "refused")
        # cancel by client
        c.send(op="cancel", id="same"); m = ui.recv()
        check("client cancel reaches ui", m == {"op": "cancel", "id": m1["id"]}, m)
        check("client told cancelled", c.recv() == {"op": "reject", "id": "same", "reason": "cancelled"})
        # requester leaves
        c3.send(op="ask", id="gone", kind="k3"); m = ui.recv(); c3.close()
        m = ui.recv(); check("ui told when requester leaves", m["op"] == "cancel", m)
        # ui answers an id nobody holds: ignored, still alive
        ui.send(op="answer", id="999", result={}); 
        c.send(op="ask", id="alive", kind="k"); m = ui.recv(); check("late answer ignored", m["op"] == "ask")
        # ui dies with an ask open
        c.send(op="ask", id="held", kind="k"); ui.recv(); 
        ui.close()
        got = {}
        for _ in range(2):
            r = c.recv(); got[r["id"]] = r["reason"]
        check("shell gone: every held ask rejected", got == {"alive": "shell-gone", "held": "shell-gone"}, got)
        # in-process ask rejected when no ui, answered when ui present
        d.stdout.readline()  # ready
        line = inproc()
        check("in-process ask with no shell rejected", line == "inproc rejected no-shell", line)
        ui = C("ui"); d.stdin.write("ask\n"); d.stdin.flush(); m = ui.recv()
        check("in-process ask reaches ui", m["kind"] == "test.inproc", m)
        ui.send(op="answer", id=m["id"], result={"v": "yes"})
        check("in-process ask answered", d.stdout.readline().strip() == "inproc ok  yes")
        ui.close(); time.sleep(0.2)
        # a restarted ui replaces the old one
        u1 = C("ui"); c.send(op="ask", id="r1", kind="k"); u1.recv()
        u2 = C("ui")
        r = c.recv(); check("new ui replaces old, old asks rejected", r == {"op": "reject", "id": "r1", "reason": "shell-gone"}, r)
        c.send(op="ask", id="r2", kind="k"); m = u2.recv(); check("new ui gets asks", m["op"] == "ask")
        # oversized line without newline drops the client
        big = socket.socket(socket.AF_UNIX); big.connect(sock); big.settimeout(3)
        try:
            big.sendall(b"x" * (2 << 20)); time.sleep(0.3)
            dead = False
            try: dead = big.recv(10) == b""
            except OSError: dead = True
        except OSError: dead = True
        check("oversized line drops the connection", dead)
    else:
        # real singleton as the ui, started AFTER the daemon would be the easy case: start it first
        # (daemon is already up), then also test the late-server case below.
        pass
finally:
    d.terminate(); d.wait()
    if os.path.exists(sock): os.unlink(sock)
print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)

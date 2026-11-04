#!/usr/bin/env python3
"""AppChooser end to end on a private session bus: the real tacit-portal, a fake shell (ui client on the bridge socket),
a fake GTK backend for the fallback, fake desktop entries, the real xdg-mime on a private HOME. Nothing live is touched.
usage: dbus-run-session -- open-with-test.py <tacit-portal>   (open-with-test.sh does that)"""
import json, os, shutil, socket, subprocess, sys, time
portal = os.path.abspath(sys.argv[1])
work = os.path.expanduser("~/.cache/tacit-dialogs/openwith-test")
shutil.rmtree(work, ignore_errors=True)
share = work + "/share/applications"; home = work + "/home"
for d in (share, home + "/.local/share/applications", home + "/.config"): os.makedirs(d)
def desk(name, title, mime="", icon="x"):
    open(f"{share}/{name}.desktop", "w").write(f"[Desktop Entry]\nType=Application\nName={title}\nIcon={icon}\nExec=true\nMimeType={mime}\n")
desk("browser", "Opsis", "application/pdf;"); desk("code", "Graphis", "text/plain;"); desk("zeta", "Zeta", "application/pdf;application/x-7z-compressed;")
desk("alpha", "Alpha", "application/pdf;application/x-7z-compressed;"); desk("hidden", "Hidden", "application/pdf;")
open(f"{share}/hidden.desktop", "a").write("NoDisplay=true\n")
os.symlink("/usr/share/mime", work + "/share/mime")   # kind names come from the shared mime database
sock = work + "/d.sock"
env = dict(os.environ, HOME=home, XDG_DATA_HOME=home + "/.local/share", XDG_CONFIG_HOME=home + "/.config",
           XDG_DATA_DIRS=work + "/share", TACIT_DIALOGS_SOCK=sock, QT_QPA_PLATFORM="offscreen")
env.pop("HYPRLAND_INSTANCE_SIGNATURE", None)
fails = []
def check(n, c, d=""):
    print(("ok   " if c else "FAIL ") + n + ((" " + str(d)) if not c and d else ""))
    if not c: fails.append(n)
procs = []
def spawn(a, **k):
    p = subprocess.Popen(a, env=env, **k); procs.append(p); return p
class Ui:
    def __init__(s):
        s.s = socket.socket(socket.AF_UNIX); s.s.connect(sock); s.buf = b""
        s.send(op="hello", role="ui", proto=1); s.recv()
    def send(s, **o): s.s.sendall(json.dumps(o).encode() + b"\n")
    def recv(s, t=8):
        s.s.settimeout(t)
        while b"\n" not in s.buf:
            d = s.s.recv(4096)
            if not d: return None
            s.buf += d
        l, s.buf = s.buf.split(b"\n", 1); return json.loads(l)
n = 0
def call(choices, opts):
    global n; n += 1
    h = f"/org/freedesktop/portal/desktop/request/t/w{n}"
    return h, subprocess.Popen(["gdbus", "call", "--session", "--timeout", "20", "--dest", "org.freedesktop.impl.portal.desktop.tacit",
        "--object-path", "/org/freedesktop/portal/desktop", "--method", "org.freedesktop.impl.portal.AppChooser.ChooseApplication",
        h, "app", "", str(choices), opts], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
def out(p): o, e = p.communicate(timeout=20); return (o + e).strip()
try:
    spawn([portal], stdout=subprocess.DEVNULL, stderr=open(work + "/portal.log", "w"))
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.1)
    spawn([os.path.join(os.path.dirname(portal), "fake-gtk")])
    time.sleep(1)
    ui = Ui()
    PDF = "{'content_type': <'application/pdf'>, 'filename': <'/home/x/q3-report.pdf'>}"

    h, p = call(["zeta.desktop", "code.desktop", "browser.desktop", "gone.desktop"], PDF)
    a = ui.recv()
    check("ask kind", a and a["kind"] == "open.choose", a)
    pr = a["params"]
    check("tacit apps first (Opsis, Graphis), then the rest; unknown id dropped", [x["id"] for x in pr["apps"]] == ["browser.desktop", "code.desktop", "zeta.desktop"], pr["apps"])
    check("names and tacit flags", [(x["name"], x["tacit"]) for x in pr["apps"]] == [("Opsis", True), ("Graphis", True), ("Zeta", False)])
    check("recommended preselected, file and kind", pr["selected"] == 0 and pr["file"] == "q3-report.pdf" and pr["kind"] == "PDF" and pr["asker"] is None, pr)
    ui.send(op="answer", id=a["id"], result={"id": "code.desktop", "always": False})
    r = out(p)
    check("answer becomes choice, response 0", r == "(uint32 0, {'choice': <'code.desktop'>})", r)
    check("no default written without Always", not os.path.exists(home + "/.config/mimeapps.list"))

    h, p = call(["browser", "zeta"], PDF)   # ids without .desktop come back the same way
    a = ui.recv(); ui.send(op="answer", id=a["id"], result={"id": "zeta", "always": True})
    r = out(p)
    check("id form is mirrored", r == "(uint32 0, {'choice': <'zeta'>})", r)
    time.sleep(1)
    ml = open(home + "/.config/mimeapps.list").read() if os.path.exists(home + "/.config/mimeapps.list") else ""
    check("Always use sets the default for the type", "application/pdf=zeta.desktop" in ml, ml)

    h, p = call(["zeta.desktop", "alpha.desktop"], "{'content_type': <'application/x-7z-compressed'>, 'filename': <'backup.7z'>, 'last_choice': <'alpha'>}")
    a = ui.recv(); pr = a["params"]
    check("no Tacit app: last choice preselected, kind .7z", pr["selected"] == 1 and pr["kind"] == ".7z" and not any(x["tacit"] for x in pr["apps"]), pr)
    ui.send(op="reject", id=a["id"], reason="cancelled")
    r = out(p); check("cancel is response 1", r == "(uint32 1, @a{sv} {})", r)

    h, p = call(["alpha.desktop", "zeta.desktop"], "{'content_type': <'application/x-7z-compressed'>}")
    a = ui.recv(); pr = a["params"]
    check("no tacit, no last choice: system default or the first", pr["selected"] in (0, 1) and pr["kind"] == ".7z", pr)
    ui.send(op="answer", id=a["id"], result={"id": "nonsense", "always": False})
    r = out(p); check("an id that was not offered ends the call (response 2)", r.startswith("(uint32 2"), r)

    h, p = call([], "{'content_type': <'application/pdf'>, 'filename': <'a.pdf'>}")
    a = ui.recv(); ids = sorted(x["id"] for x in a["params"]["apps"])
    check("empty choices: scan by MimeType, hidden entries left out", ids == ["alpha.desktop", "browser.desktop", "zeta.desktop"] or ids == ["alpha.desktop", "browser.desktop", "zeta.desktop"], ids)
    ui.send(op="answer", id=a["id"], result={"id": "alpha.desktop", "always": False}); out(p)

    h, p = call(["zeta.desktop"], PDF)
    a = ui.recv()
    subprocess.run(["gdbus", "call", "--session", "--dest", "org.freedesktop.impl.portal.desktop.tacit", "--object-path", h,
                    "--method", "org.freedesktop.impl.portal.Request.Close"], env=env, capture_output=True)
    c = ui.recv(); check("Close tells the shell to drop the sheet", c and c["op"] == "cancel" and c["id"] == a["id"], c)
    r = out(p); check("Close answers response 2", r.startswith("(uint32 2"), r)

    h, p = call(["nothing.desktop"], PDF)
    r = out(p); check("no app opens the type: handed to the GTK backend", "gtkpick" in r, r)

    h, p = call(["zeta.desktop"], PDF)
    a = ui.recv(); ui.send(op="reject", id=a["id"], reason="unsupported")
    r = out(p); check("shell has no sheet for it (unsupported): GTK backend answers", "gtkpick" in r, r)

    h, p = call(["zeta.desktop"], PDF)
    a = ui.recv(); ui.send(op="reject", id=a["id"], reason="shell-gone")
    r = out(p); check("shell restarted under the ask (shell-gone): GTK backend answers", "gtkpick" in r, r)

    h, p = call(["browser.desktop", "zeta.desktop"], "{'content_type': <'application/pdf'>, 'filename': <'x.pdf'>, 'last_choice': <'zeta'>}")
    a = ui.recv(); pr = a["params"]
    check("last choice wins over the recommended Tacit app", pr["apps"][pr["selected"]]["id"] == "zeta.desktop", pr)
    ui.send(op="reject", id=a["id"], reason="cancelled"); out(p)

    h, p = call(["zeta.desktop"], "{'content_type': <'application/x-compressed-tar'>, 'filename': <'/home/x/photos.tar.gz'>}")
    a = ui.recv(); k = a["params"]["kind"]
    check("tarball: a plain kind name, not 'GZ' or a mime type", k not in ("GZ", ".gz", "application/x-compressed-tar") and "/" not in k, k)
    ui.send(op="reject", id=a["id"], reason="cancelled"); out(p)
    h, p = call(["zeta.desktop"], "{'content_type': <'application/x-foo-unknown'>, 'filename': <'/home/x/README'>}")
    a = ui.recv(); k = a["params"]["kind"]
    check("no extension, unknown type: falls back to the type", k == "application/x-foo-unknown", k)
    ui.send(op="reject", id=a["id"], reason="cancelled"); out(p)

    ui.s.close(); time.sleep(0.5)
    h, p = call(["zeta.desktop"], PDF)
    t = time.time(); r = out(p)
    check("no shell: GTK backend answers at once", "gtkpick" in r and time.time() - t < 5, r)
finally:
    for p in procs: p.kill()
print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)

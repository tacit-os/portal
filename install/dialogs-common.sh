# Shared by share-picker-install.sh, keyring-prompter-install.sh and open-with-install.sh (sourced; needs $src $live $family).
# The three installs share two shell files, so neither is restored whole on undo:
#  - the panel family file: each install adds only its own import and loader lines, and undo removes only those
#  - services/DialogBridge.qml: a user list; it is deleted when the last user leaves, and only if we put it there
state=$HOME/.local/share/tacit-portal/dialogbridge
bridge=$live/services/DialogBridge.qml

bridge_use() { # name
	mkdir -p "$state/users"
	if [ ! -f "$bridge" ]; then cp "$src/services/DialogBridge.qml" "$bridge"; : >"$state/added"; fi
	: >"$state/users/$1"
}
bridge_release() { # name
	rm -f "$state/users/$1"
	if [ -z "$(ls -A "$state/users" 2>/dev/null)" ]; then
		[ -e "$state/added" ] && rm -f "$bridge"
		rm -rf "$state"
	fi
}
# family_patch name import-line anchor-import loader-anchor loader-line comment ; idempotent, only adds missing lines
family_patch() {
	python3 - "$family" "$@" <<'PY'
import sys
p, imp, imp_after, ld_after, ld, cmt = sys.argv[1:7]
s = open(p).read()
if imp not in s:
    s = s.replace(imp_after + "\n", imp_after + "\n" + imp + "\n", 1)
if ld not in s:
    s = s.replace(ld_after + "\n", ld_after + "\n    // " + cmt + "\n" + ld + "\n", 1)
open(p, "w").write(s)
PY
	grep -qF "$4" "$family" && grep -qF "$1" "$family" || { echo "family file not patched: unexpected layout"; exit 1; }
}
# family_unpatch import-line loader-line : removes those lines and the comment line right above the loader
family_unpatch() {
	[ -f "$family" ] || return 0
	python3 - "$family" "$@" <<'PY'
import sys
p, imp, ld = sys.argv[1:4]
L = open(p).read().split("\n")
out = []
for i, l in enumerate(L):
    if l.strip() == imp.strip():
        continue
    if l.strip() == ld.strip():
        if out and out[-1].lstrip().startswith("//"):
            out.pop()
        continue
    out.append(l)
open(p, "w").write("\n".join(out))
PY
}
# unpatch_conf file key backup-file our-line [block] : takes out the one line an install added (key = ...), and puts back the
# line it replaced (from the backup of the original). Anything else in the file, changed since, stays. A block ("screencopy")
# or a file that the install created and that is now empty is removed. Only the line is ours; the file is not restored whole.
unpatch_conf() {
	[ -f "$1" ] || return 0
	python3 - "$@" <<'PY'
import os, re, sys
p, key, bak, ours = sys.argv[1:5]
block = sys.argv[5] if len(sys.argv) > 5 else ""
s = open(p).read()
had = os.path.exists(bak)
orig = open(bak).read() if had else ""
prior = re.search(r"^[ \t]*" + re.escape(key) + r"[ \t]*=.*$", orig, re.M)
pat = re.compile(r"^[ \t]*" + re.escape(ours.split("=")[0].strip()) + r"[ \t]*=[ \t]*" + re.escape(ours.split("=", 1)[1].strip()) + r"[ \t]*\n?", re.M)
if prior:
    s = pat.sub(lambda m: prior.group(0) + "\n", s, count=1)
else:
    s = pat.sub("", s, count=1)
    if block and (not had or not re.search(r"^[ \t]*" + block + r"[ \t]*\{", orig, re.M)):
        s = re.sub(r"^[ \t]*" + block + r"[ \t]*\{\s*\}[ \t]*\n?", "", s, count=1, flags=re.M)
if not had and not s.strip():
    os.remove(p)
elif not had and orig == "" and s.strip() == "[preferred]\ndefault = hyprland;gtk":
    os.remove(p)
else:
    open(p, "w").write(s)
PY
}

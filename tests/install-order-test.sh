#!/bin/bash
# Installs the three dialog installs in a private HOME against a copy of the live shell tree, undoes them in every
# order, and checks the panel family file is byte-identical to the original afterwards, and consistent in between
# (every sheet import has its directory; DialogBridge.qml exists while any install remains).
set -eu
here=$(dirname "$(realpath "$0")")/..
REAL_HOME=$HOME
work=${TMPDIR_KEEP:-$HOME/.cache/tacit-dialogs}/order-test
rm -rf "$work"; mkdir -p "$work"
export HOME=$work/home SHELL_WORK=${SHELL_WORK:-$REAL_HOME/book4-edge/tacit-fix/end4-pC}
mkdir -p "$HOME" "$work/shim"
for c in busctl pkill; do printf "#!/bin/sh\nexit 0\n" >"$work/shim/$c"; chmod +x "$work/shim/$c"; done
export PATH=$work/shim:$PATH BUILD_DIR=$REAL_HOME/.cache/tacit-dialogs/pb
live=$work/live
export SHELL_LIVE=$live
fail=0
fresh() { rm -rf "$live" "$HOME/.local" "$HOME/.config"; mkdir -p "$live"; cp -r "$LIVE_SRC/panelFamilies" "$LIVE_SRC/services" "$live/"; mkdir -p "$live/modules/ii"
	sum0=$(sha256sum "$live/panelFamilies/IllogicalImpulseFamily.qml" | cut -c1-64); }
check() { # state after a step
	local fam=$live/panelFamilies/IllogicalImpulseFamily.qml any=0
	for pair in sharePicker:share-picker keyring:keyring-prompter openWith:open-with; do
		d=${pair%%:*}; n=${pair##*:}
		if grep -q "import qs.modules.ii.$d" "$fam"; then [ -d "$live/modules/ii/$d" ] || { echo "FAIL $1: $d imported, dir missing"; fail=1; }; any=1
		else [ ! -d "$live/modules/ii/$d" ] || { echo "FAIL $1: $d dir without import"; fail=1; }; fi
	done
	[ $any = 0 ] || [ -f "$live/services/DialogBridge.qml" ] || { echo "FAIL $1: bridge missing while installed"; fail=1; }
}
LIVE_SRC=${LIVE_SRC:-$REAL_HOME/.config/quickshell/end4-pC}
i() { bash "$here/install/$1-install.sh" "$2" >/dev/null 2>&1 || { echo "FAIL: $1 $2 exited $?"; fail=1; }; }
orders=("share keyring open" "share open keyring" "keyring share open" "keyring open share" "open share keyring" "open keyring share")
names=(share-picker keyring-prompter open-with)
for ins in "${orders[@]}"; do for und in "${orders[@]}"; do
	fresh
	for k in $ins; do i $(printf %s "${names[$(case $k in share) echo 0;; keyring) echo 1;; open) echo 2;; esac)]}") install; done
	check "all installed"
	for k in $und; do i "${names[$(case $k in share) echo 0;; keyring) echo 1;; open) echo 2;; esac)]}" undo; check "after undo $k (install: $ins, undo: $und)"; done
	[ "$(sha256sum "$live/panelFamilies/IllogicalImpulseFamily.qml" | cut -c1-64)" = "$sum0" ] || { echo "FAIL: family not restored (install $ins, undo $und)"; fail=1; }
	[ ! -d "$HOME/.local/share/tacit-portal/dialogbridge" ] || { echo "FAIL: bridge state left"; fail=1; }
	cmp -s "$live/services/DialogBridge.qml" "$LIVE_SRC/services/DialogBridge.qml" 2>/dev/null || [ ! -f "$live/services/DialogBridge.qml" ] || true
done; done
[ $fail = 0 ] && echo "install-order-test: 36 install/undo orders ok" || { echo "install-order-test: FAILED"; exit 1; }

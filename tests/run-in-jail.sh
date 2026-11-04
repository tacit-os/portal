#!/bin/bash
# Host side: stages the build, install/ and the stand-ins into the out dir, runs tests/run.sh in the jail
# (~/book4-edge/tacit-fix/sweep/jail), writes the transcript to OUT/transcript.txt.
#   run-in-jail.sh OUT_DIR
set -eu
here=$(dirname "$(realpath "$0")")
root=$(dirname "$here")
out=$(realpath -m "$1")
mkdir -p "$out/stage"
rm -rf "$out/stage/install" "$out/stage/tests-bin"
cp "$root/build/tacit-portal" "$root/build/fake-kde" "$root/build/portal-call" "$out/stage/"
cp -r "$root/install" "$out/stage/install"
cp -r "$here/tests-bin" "$out/stage/tests-bin"
cp "$here/run.sh" "$out/stage/run.sh"
J=$HOME/book4-edge/tacit-fix/sweep/jail
systemd-run --user --scope -q --expand-environment=no -p MemoryMax=1000M -p MemorySwapMax=0 \
	nice -n 10 "$J/jail.sh" --out "$out" -- bash -c "bash /out/stage/run.sh >/out/transcript.txt 2>&1" || true
cat "$out/transcript.txt"

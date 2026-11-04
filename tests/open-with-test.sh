#!/bin/bash
# usage: open-with-test.sh [tacit-portal binary]   (default: the work build in ~/.cache/tacit-dialogs/pb)
exec nice -n 10 dbus-run-session -- python3 "$(dirname "$(realpath "$0")")/open-with-test.py" "${1:-$HOME/.cache/tacit-dialogs/pb/tacit-portal}"

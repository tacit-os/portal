#!/bin/bash
# usage: run.sh scene out.png
cd ~/.cache/tacit-dialogs/render
MOCK=$1 OUT=$2 XDG_STATE_HOME=$PWD/state env -u QT_LOGGING_RULES QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl QT_FORCE_STDERR_LOGGING=1 nice -n 10 timeout 90 qs -p $PWD/end4-pC/harness-share.qml 2>&1 | grep -iE "error|warn|TypeError|ReferenceError|not defined" | grep -v "WAYLAND\|QT_QPA" | head -15

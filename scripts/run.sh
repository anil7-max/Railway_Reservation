#!/usr/bin/env bash
set -e

if [ ! -f "./railway_v4" ]; then
    echo "Executable not found. Building first..."
    bash scripts/build.sh
fi

echo "Launching Railway Reservation System..."
env -i HOME="$HOME" PATH="/usr/bin:/bin" DISPLAY="$DISPLAY" \
XAUTHORITY="$XAUTHORITY" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
XDG_DATA_DIRS="/usr/local/share:/usr/share" \
XDG_CONFIG_DIRS="/etc/xdg" ./railway_v4

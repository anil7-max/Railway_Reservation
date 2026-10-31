#!/usr/bin/env bash
set -e

echo "Compiling Railway Reservation System..."
gcc -Wall -Wextra -Wpedantic src/railway_reservation_v4_responsive_no_top_bar.c -o railway_v4 $(pkg-config --cflags --libs gtk+-3.0)

echo "Build successful: ./railway_v4"

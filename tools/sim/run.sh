#!/bin/sh
# Builds and runs the Halo clock simulator. Run from the "OLED Code" folder:
#   tools/sim/run.sh                   every face
#   tools/sim/run.sh classic_sweep     only scenarios whose name contains the text
#   ONLY=learn tools/sim/run.sh        build just one face file (classic, learn, timers, zones, schedule)
set -e
cd "$(dirname "$0")/../.."
mkdir -p tools/sim/out
FLAGS=""
BIN=tools/sim/out/sim
if [ -n "$ONLY" ]; then
  UPPER=$(echo "$ONLY" | tr '[:lower:]' '[:upper:]')
  FLAGS="-DONLY_FACE -DONLY_$UPPER"
  BIN="tools/sim/out/sim_$ONLY"
fi
clang++ -std=c++17 -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter $FLAGS -o "$BIN" tools/sim/sim.cpp
"$BIN" "$@"

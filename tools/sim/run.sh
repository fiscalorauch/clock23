#!/bin/sh
# Builds and runs the Halo clock simulator. Run from the repo root:
#   tools/sim/run.sh                   everything
#   tools/sim/run.sh weather_rain      only scenarios whose name contains the text
set -e
cd "$(dirname "$0")/../.."
mkdir -p tools/sim/out
BIN=tools/sim/out/sim
clang++ -std=c++17 -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -o "$BIN" tools/sim/sim.cpp
"$BIN" "$@"

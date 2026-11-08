#!/bin/sh
# Compiles the firmware (and optionally uploads it over USB) with its own copy
# of the libraries, kept outside any cloud-synced folder. Synced folders such as
# iCloud Documents can leave "file 2.cpp" conflict copies in a library, which
# breaks linking with "multiple definition" errors.
#
#   tools/build.sh                         compile into build/
#   tools/build.sh upload /dev/cu.usbmodemXXXX
#
# Versions match the GitHub release build (.github/workflows/firmware.yml).
set -e
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"
GFX="GFX Library for Arduino@1.6.7"
GC9B72_COMMIT=e8ad9e65b921c70c95a6b2393deb5b58f0b8865c

BASE="$HOME/Library/Application Support/halo-arduino"
CFG="$BASE/arduino-cli.yaml"
LIBS="$BASE/sketchbook/libraries"
mkdir -p "$LIBS"
cat > "$CFG" <<YAML
directories:
  data: "$HOME/Library/Arduino15"
  user: "$BASE/sketchbook"
YAML

[ -d "$LIBS/GFX_Library_for_Arduino" ] || "$CLI" --config-file "$CFG" lib install "$GFX"
if [ ! -d "$LIBS/Arduino_GC9B72" ]; then
  git clone -q https://github.com/MaliosDark/Arduino_GC9B72 "$LIBS/Arduino_GC9B72"
  git -C "$LIBS/Arduino_GC9B72" checkout -q "$GC9B72_COMMIT"
fi

"$CLI" --config-file "$CFG" compile --fqbn "$FQBN" --output-dir build HaloClock

if [ "$1" = upload ]; then
  [ -n "$2" ] || { echo "usage: tools/build.sh upload /dev/cu.usbmodemXXXX"; exit 1; }
  "$CLI" --config-file "$CFG" upload --fqbn "$FQBN" --input-dir build -p "$2"
  echo "Done. Press RESET on the board."
fi

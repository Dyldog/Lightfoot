#!/bin/sh
# Wraps the toolkit's packer: tools/package.sh <device-binary> <out-dir>
# Writes <out-dir>/autorun/ and <out-dir>/lightfoot.zip. The zip boots directly
# from an SD card root and also drops into mpc-boot-thingy's apps folder.
set -eu
BIN=${1:?device binary}
OUT=${2:?output directory}
MANUAL=${3:-}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$OUT"
# docs/icon.png becomes autorun/icon.png, the tile mpc-boot-thingy draws for the app.
set -- --name lightfoot --bin "$BIN" --out "$OUT" --add "$ROOT/docs/icon.png"
# The PDF manual rides along on the card when it has been built.
if [ -n "$MANUAL" ] && [ -f "$MANUAL" ]; then set -- "$@" --add "$MANUAL"; fi
"$ROOT/deps/mpc-ui-toolkit/tools/pack-autorun.sh" "$@"
echo "package: $OUT/lightfoot.zip"

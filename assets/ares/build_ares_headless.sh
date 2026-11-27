#!/usr/bin/env bash
# Reproducibly build the headless ares Sega MD / 32X / MCD test harness and
# package it into artifacts/ares-headless.tar.gz.
#
# Usage:
#   ARES_SRC=build/ares-src bash assets/ares/build_ares_headless.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ARES_COMMIT="${ARES_COMMIT:-6e67c8bd4f0cd14e299047386d381d06ea5478c3}"
ARES_SRC="${ARES_SRC:-}"
CLEAN_SRC=0

if [ -z "$ARES_SRC" ]; then
  ARES_SRC="$(mktemp -d "${TMPDIR:-/tmp}/ares-src.XXXXXX")"
  CLEAN_SRC=1
  git clone --depth 1 https://github.com/ares-emulator/ares.git "$ARES_SRC"
fi

cleanup() {
  if [ "$CLEAN_SRC" = "1" ] && [ -d "$ARES_SRC" ]; then
    rm -rf "$ARES_SRC"
  fi
}
trap cleanup EXIT

ARES_SRC="$(cd "$ARES_SRC" && pwd)"
if [ ! -f "$ARES_SRC/CMakeLists.txt.orig" ]; then
  cp "$ARES_SRC/CMakeLists.txt" "$ARES_SRC/CMakeLists.txt.orig"
fi

mkdir -p "$ARES_SRC/headless"
cp "$SCRIPT_DIR/ares_harness.cpp" "$ARES_SRC/headless/ares_harness.cpp"
cp "$SCRIPT_DIR/ares_harness_mem.cpp" "$ARES_SRC/headless/ares_harness_mem.cpp"
cp "$SCRIPT_DIR/ares_harness_mem.hpp" "$ARES_SRC/headless/ares_harness_mem.hpp"
cp "$SCRIPT_DIR/CMakeLists.headless.txt" "$ARES_SRC/CMakeLists.txt"

BUILD_DIR="$ARES_SRC/build-headless"
cmake -S "$ARES_SRC" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DARES_BUILD_LOCAL=OFF \
  -DARES_SKIP_DEPS=ON \
  -DARES_PROFILE_ACCURACY=ON \
  -DARES_UNITY_CORES=ON \
  -DENABLE_IPO=OFF

cmake --build "$BUILD_DIR" --target ares-harness -j"$(nproc)"

BIN="$BUILD_DIR/rundir/bin/ares-harness"
if [ ! -x "$BIN" ]; then
  BIN="$(find "$BUILD_DIR" -maxdepth 3 -name ares-harness -type f -executable | head -n1)"
fi
test -x "$BIN"
strip "$BIN"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/ares-pkg.XXXXXX")"
cp "$BIN" "$STAGE/ares-harness"
cp "$ARES_SRC/LICENSE" "$STAGE/LICENSE"
echo "ares_commit=$(git -C "$ARES_SRC" rev-parse HEAD 2>/dev/null || echo "$ARES_COMMIT")" > "$STAGE/VERSION.txt"

mkdir -p "$SKILL_DIR/artifacts"
tar -czf "$SKILL_DIR/artifacts/ares-headless.tar.gz" -C "$STAGE" ./ares-harness ./LICENSE ./VERSION.txt
rm -rf "$STAGE"

# Restore upstream CMakeLists.txt if working inside an existing clone.
mv -f "$ARES_SRC/CMakeLists.txt.orig" "$ARES_SRC/CMakeLists.txt"
rm -rf "$ARES_SRC/headless"

echo "Built and packaged: $SKILL_DIR/artifacts/ares-headless.tar.gz"
ls -lh "$SKILL_DIR/artifacts/ares-headless.tar.gz"
ldd "$BUILD_DIR/rundir/bin/ares-harness" || true

#!/usr/bin/env bash
# Tier 2 — headless emulator. Boots the real out/rom.bin under Genesis Plus GX
#
# Copyright (c) 2026 Haroldo de Oliveira Pinheiro <haroldoop@gmail.com>
# SPDX-License-Identifier: MIT
#
# (libretro) with no display, feeds scripted input, dumps frames, and asserts
# the ROM is lit, colourful, and advances (i.e. NOT a black screen).
#
# Requires: GDK set, a native cc, git, make. No X server / GPU / audio device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "$ROOT/.." && pwd)"

# Auto-detect GDK if not explicitly set
if [[ -z "${GDK:-}" && -d "$PROJECT_ROOT/opt/m68k-elf" ]]; then
  export GDK="$PROJECT_ROOT/opt/m68k-elf"
fi
: "${GDK:?Set GDK to your SGDK dir (or run inside the doragasu docker image).}"
TMP="$ROOT/tests/.tmp"; ART="$ROOT/tests/artifacts"; mkdir -p "$TMP" "$ART"

# Check for prebuilt core in opt/libretro or environment override
if [[ -z "${GPGX_CORE:-}" ]]; then
  if [[ -f "$PROJECT_ROOT/opt/libretro/genesis_plus_gx_libretro.so" ]]; then
    CORE="$PROJECT_ROOT/opt/libretro/genesis_plus_gx_libretro.so"
  elif [[ -f "$ROOT/opt/libretro/genesis_plus_gx_libretro.so" ]]; then
    CORE="$ROOT/opt/libretro/genesis_plus_gx_libretro.so"
  elif [[ -f "$PROJECT_ROOT/artifacts/genesis-plus-gx-libretro.tar.gz" ]]; then
    mkdir -p "$PROJECT_ROOT/opt/libretro"
    tar -xzf "$PROJECT_ROOT/artifacts/genesis-plus-gx-libretro.tar.gz" -C "$PROJECT_ROOT/opt/libretro"
    CORE="$PROJECT_ROOT/opt/libretro/genesis_plus_gx_libretro.so"
  else
    CORE="$TMP/genesis_plus_gx_libretro.so"
  fi
else
  CORE="$GPGX_CORE"
fi

# 1. Build (or reuse) the Genesis Plus GX libretro core if not found.
if [[ ! -f "$CORE" ]]; then
  [[ -d "$TMP/gpgx" ]] || git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git "$TMP/gpgx"
  make -C "$TMP/gpgx" -f Makefile.libretro platform=unix -j"$(nproc)"
  cp "$TMP/gpgx/genesis_plus_gx_libretro.so" "$CORE"
fi

# 2. Build the headless harness.
cc -O2 -o "$TMP/harness" "$ROOT/tests/libretro_harness.c" -ldl -lm

# 3. Locate or build the ROM.
ROM="${ROM:-}"
if [[ -z "$ROM" ]]; then
  for candidate in "$PROJECT_ROOT/out/genesis_demo.bin" "$ROOT/out/rom.bin" "$PROJECT_ROOT/out/rom.bin" "$ROOT/rom/game.bin" "$PROJECT_ROOT/out_cd/main.bin" "$PROJECT_ROOT/out/game.bin"; do
    if [[ -f "$candidate" ]]; then
      ROM="$candidate"
      break
    fi
  done
fi

if [[ -z "$ROM" && -f "$ROOT/Makefile" ]]; then
  make -C "$ROOT" clean >/dev/null
  make -C "$ROOT"
  [[ -f "$ROOT/out/rom.bin" ]] && ROM="$ROOT/out/rom.bin"
fi

if [[ -z "$ROM" || ! -f "$ROM" ]]; then
  echo "FAIL: out/rom.bin not found (set ROM=/path/to/rom.bin or run build first)"
  exit 1
fi

# 4. Handle transient Sega CD BIOS download for automated CD disc tests
BIOS_TMP_DIR=""
cleanup_bios() {
  if [[ -n "${BIOS_TMP_DIR:-}" && -d "$BIOS_TMP_DIR" ]]; then
    rm -rf "$BIOS_TMP_DIR"
  fi
  rm -f ./bios_CD_U.bin ./bios_CD_E.bin ./bios_CD_J.bin ./scd_*.brm ./cart.brm
}

ROM_EXT="${ROM##*.}"
ROM_EXT="${ROM_EXT,,}"

if [[ "$ROM_EXT" == "chd" || "$ROM_EXT" == "cue" || "$ROM_EXT" == "iso" || "${REQUIRE_CD_BIOS:-0}" == "1" ]]; then
  echo ">> Sega CD disc image target detected ($ROM)."
  echo ">> Fetching Sega CD BIOS for automated testing from retrobios repository..."
  
  # Allocate temporary directory in /tmp (completely outside repository)
  BIOS_TMP_DIR="$(mktemp -d /tmp/segacd_bios.XXXXXX)"
  
  # Register cleanup trap to ensure BIOS files are wiped immediately on test exit/interrupt
  trap cleanup_bios EXIT INT TERM

  BIOS_BASE_URL="https://raw.githubusercontent.com/Abdess/retrobios/main/bios/Sega/Mega%20CD"
  
  # Download US Model 2 Sega CD BIOS (us_scd2_9306.bin)
  if command -v curl >/dev/null 2>&1; then
    curl -sL --retry 3 --connect-timeout 10 "$BIOS_BASE_URL/us_scd2_9306.bin" -o "$BIOS_TMP_DIR/bios_CD_U.bin"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$BIOS_TMP_DIR/bios_CD_U.bin" "$BIOS_BASE_URL/us_scd2_9306.bin"
  fi

  if [[ -f "$BIOS_TMP_DIR/bios_CD_U.bin" ]]; then
    ln -sf "$BIOS_TMP_DIR/bios_CD_U.bin" ./bios_CD_U.bin
  else
    echo "!! [WARNING] Could not download Sega CD BIOS (network unreachable or sandboxed)."
  fi
fi

# 5. Smoke playthrough: snapshot the title, press START, hold RIGHT, snapshot.
#    Tune the frame ranges/buttons to your game's real states.
"$TMP/harness" "$CORE" "$ROM" 300 \
  "shot@30,120-160:start,200-260:right,shot@250" \
  "$ART/frame_" | tee "$TMP/harness.out"

# 6. Assert on the dumped frames: lit, colourful, and changed over time.
python3 "$ROOT/tests/check_frames.py" "$ART/frame_30.ppm" "$ART/frame_250.ppm"

# 7. (Optional) on-target self-test marker in SRAM — see testing.md.
#    Build with -DSELFTEST, run, and grep the printed SRAM head for your marker,
#    matching BOTH the plain and 0xFF-interleaved forms (odd-byte SRAM).

echo "PASS: headless emulator boot + playthrough"

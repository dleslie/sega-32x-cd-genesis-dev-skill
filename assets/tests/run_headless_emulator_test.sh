#!/usr/bin/env bash
# Tier 2 — headless emulator. Boots the real out/rom.bin / .32x / .chd under ares
#
# Copyright (c) 2026 Haroldo de Oliveira Pinheiro <haroldoop@gmail.com>
# SPDX-License-Identifier: MIT
#
# (`ares-harness --inline`) with no display, feeds scripted input, dumps PPM
# frames, and asserts the ROM is lit, colourful, and advances (NOT black).
#
# Requires: GDK set (for build fallback). No X server / GPU / audio device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "$ROOT/.." && pwd)"

# Auto-detect GDK if not explicitly set
if [[ -z "${GDK:-}" && -d "$PROJECT_ROOT/opt/m68k-elf" ]]; then
  export GDK="$PROJECT_ROOT/opt/m68k-elf"
fi
: "${GDK:?Set GDK to your SGDK dir (or run inside the doragasu docker image).}"
TMP="$ROOT/tests/.tmp"; ART="$ROOT/tests/artifacts"; mkdir -p "$TMP" "$ART"

# Check for prebuilt ares-harness in opt/ares or environment override
HARNESS="${ARES_HARNESS:-}"
if [[ -z "$HARNESS" ]]; then
  if [[ -x "$PROJECT_ROOT/opt/ares/ares-harness" ]]; then
    HARNESS="$PROJECT_ROOT/opt/ares/ares-harness"
  elif [[ -x "$ROOT/opt/ares/ares-harness" ]]; then
    HARNESS="$ROOT/opt/ares/ares-harness"
  elif [[ -f "$PROJECT_ROOT/artifacts/ares-headless.tar.gz" ]]; then
    mkdir -p "$PROJECT_ROOT/opt/ares"
    tar -xzf "$PROJECT_ROOT/artifacts/ares-headless.tar.gz" -C "$PROJECT_ROOT/opt/ares" ./ares-harness
    chmod +x "$PROJECT_ROOT/opt/ares/ares-harness"
    HARNESS="$PROJECT_ROOT/opt/ares/ares-harness"
  fi
fi

# 1. Build ares-harness from source if no prebuilt artifact was found.
if [[ -z "$HARNESS" || ! -x "$HARNESS" ]]; then
  bash "$ROOT/ares/build_ares_headless.sh"
  HARNESS="$PROJECT_ROOT/opt/ares/ares-harness"
fi

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

# 3b. Static header checks for cartridge ROMs (ROM-end vs file size, checksum, SRAM fields).
if [[ "$ROM" == *.bin ]]; then
  python3 "$ROOT/tests/rom_sanity.py" "$ROM" || true
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
  
  # Use local cached BIOS if present, otherwise download
  if [[ -f "/tmp/us_scd2_9306.bin" ]]; then
    cp -f "/tmp/us_scd2_9306.bin" "$BIOS_TMP_DIR/bios_CD_U.bin"
  elif command -v curl >/dev/null 2>&1; then
    curl -sL --retry 3 --connect-timeout 5 "$BIOS_BASE_URL/us_scd2_9306.bin" -o "$BIOS_TMP_DIR/bios_CD_U.bin" || true
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$BIOS_TMP_DIR/bios_CD_U.bin" "$BIOS_BASE_URL/us_scd2_9306.bin" || true
  fi

  if [[ -f "$BIOS_TMP_DIR/bios_CD_U.bin" ]]; then
    ln -sf "$BIOS_TMP_DIR/bios_CD_U.bin" ./bios_CD_U.bin
  else
    echo "!! [WARNING] Could not download Sega CD BIOS (network unreachable or sandboxed)."
  fi
fi

# 5. Smoke playthrough: snapshot the title, press START, hold RIGHT, snapshot.
#    Tune the frame ranges/buttons to your game's real states. Also dumps work
#    RAM at the end (inspect with `tests/ramsym.py --raw`) and prints audio
#    energy at checkpoints; add HARNESS_WAV=file.wav to keep audio output.
BIOS_ARGS=()
if [[ -f "./bios_CD_U.bin" ]]; then
  BIOS_ARGS+=(--bios "./bios_CD_U.bin")
fi
"$HARNESS" --inline "${BIOS_ARGS[@]}" "$ROM" 300 \
  "shot@30,audiostat@100,120-160:start,200-260:right,shot@250,ram@250,audiostat@250" \
  "$ART/frame_" | tee "$TMP/harness.out"

# 6. Assert on the dumped frames: lit, colourful, changed over time, and not SGDK crash screen.
python3 "$ROOT/tests/check_frames.py" "$ART/frame_30.ppm" "$ART/frame_250.ppm"

# 6b. (Optional) prove sound actually plays, not just links:
#     grep -o 'audiostat@250.*avg_energy=[0-9.]*' "$TMP/harness.out"

# 7. (Optional) on-target self-test marker in SRAM — see testing.md.
#    Build with -DSELFTEST, run, and grep the printed SRAM head for your marker,
#    matching BOTH the plain and 0xFF-interleaved forms (odd-byte SRAM).

echo "PASS: headless emulator boot + playthrough"

#!/usr/bin/env bash
# ==============================================================================
# Standalone WebAssembly ROM Packaging Script
# Compiles any Sega Genesis, Sega CD, or Sega 32X ROM into a zero-dependency,
# self-contained single-file HTML application.
#
# Copyright (c) 2026 Dan Leslie
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

die() {
    echo "!! [WASM ERROR] $*" >&2
    exit 1
}

info() {
    echo ">> [WASM] $*"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OPT_DIR="${OPT_DIR:-$PROJECT_ROOT/opt}"

ROM_FILE=""
OUTPUT_FILE=""
GAME_TITLE=""
GAME_SLUG=""
SHELL_FILE="$SCRIPT_DIR/wasm_shell.html"

usage() {
    cat << EOF
Usage: $(basename "$0") -r <ROM_FILE> [OPTIONS]

Required:
  -r, --rom <FILE>          Path to cartridge ROM or CD image (.bin, .32x, .iso, .chd)

Options:
  -o, --output <FILE>       Output HTML destination [default: dist/<rom_basename>.html]
  -t, --title <TITLE>       Game title displayed in UI [default: derived from filename]
  -s, --slug <SLUG>         Browser localStorage persistence namespace slug [default: derived from title]
      --shell <FILE>        HTML shell template [default: assets/wasm/wasm_shell.html]
      --opt-dir <DIR>       Toolchain directory [default: opt]
  -h, --help                Show this help message

Examples:
  $(basename "$0") -r out/rom.bin -t "My Genesis Game"
  $(basename "$0") -r out/tower_game.32x -o dist/tower_demo.html -t "32X Tower Demo"
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -r|--rom)
            ROM_FILE="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -t|--title)
            GAME_TITLE="$2"
            shift 2
            ;;
        -s|--slug)
            GAME_SLUG="$2"
            shift 2
            ;;
        --shell)
            SHELL_FILE="$2"
            shift 2
            ;;
        --opt-dir)
            OPT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            die "Unknown option: $1 (run with -h for help)"
            ;;
    esac
done

if [[ -z "$ROM_FILE" ]]; then
    die "Missing required --rom argument. Run '$(basename "$0") -h' for usage."
fi

if [[ ! -f "$ROM_FILE" ]]; then
    die "Target ROM file not found: $ROM_FILE"
fi

# Locate Emscripten compiler (emcc)
EMCC="${EMCC:-}"
if [[ -z "$EMCC" ]]; then
    if command -v emcc >/dev/null 2>&1; then
        EMCC="$(command -v emcc)"
    elif [[ -x "${HOME}/.emsdk/upstream/emscripten/emcc" ]]; then
        EMCC="${HOME}/.emsdk/upstream/emscripten/emcc"
    else
        die "Emscripten compiler (emcc) not found in PATH or ~/.emsdk. Run: bash assets/setup.sh"
    fi
fi

# Ensure PicoDrive WebAssembly static library exists
PICODRIVE_DIR="$OPT_DIR/picodrive"
PICODRIVE_LIB="$PICODRIVE_DIR/libpicodrive_wasm.a"

if [[ ! -f "$PICODRIVE_LIB" ]]; then
    if [[ -f "$PROJECT_ROOT/artifacts/picodrive-wasm.tar.gz" ]]; then
        info "Unpacking prebuilt PicoDrive WebAssembly runtime from artifacts/..."
        mkdir -p "$OPT_DIR"
        tar -xzf "$PROJECT_ROOT/artifacts/picodrive-wasm.tar.gz" -C "$OPT_DIR"
    else
        die "PicoDrive WASM library ($PICODRIVE_LIB) not found and artifacts/picodrive-wasm.tar.gz is missing."
    fi
fi

# Derive defaults
ROM_BASENAME="$(basename "$ROM_FILE")"
ROM_STEM="${ROM_BASENAME%.*}"

if [[ -z "$GAME_TITLE" ]]; then
    GAME_TITLE="${ROM_STEM//_/ }"
fi

if [[ -z "$GAME_SLUG" ]]; then
    # Sanitize title for localStorage slug
    GAME_SLUG="$(echo "$GAME_TITLE" | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-' | sed 's/^-//;s/-$//')"
fi

if [[ -z "$OUTPUT_FILE" ]]; then
    OUTPUT_FILE="$PROJECT_ROOT/dist/${ROM_STEM}.html"
fi

OUT_DIR="$(dirname "$OUTPUT_FILE")"
mkdir -p "$OUT_DIR"

info "Target ROM:         $ROM_FILE"
info "Game Title:         $GAME_TITLE"
info "Persistence Slug:   $GAME_SLUG"
info "Output Destination: $OUTPUT_FILE"

# Prepare customized shell HTML in temporary file
TMP_SHELL="$(mktemp /tmp/wasm_shell.XXXXXX.html)"
sed \
    -e "s|<title>.*</title>|<title>${GAME_TITLE} (PicoDrive WASM)</title>|g" \
    -e "s|<h1>.*</h1>|<h1>${GAME_TITLE}</h1>|g" \
    "$SHELL_FILE" > "$TMP_SHELL"

VFS_PATH="/rom/$ROM_BASENAME"
WASM_MAIN="$SCRIPT_DIR/wasm_main.c"

EMCC_FLAGS=(
    -O2 -flto
    -I"$PICODRIVE_DIR"
    -I"$PICODRIVE_DIR/pico"
    -I"$PICODRIVE_DIR/cpu/sh2"
    -I"$PICODRIVE_DIR/platform/libretro/libretro-common/include"
    -DROM_VFS_PATH="\"$VFS_PATH\""
    -DPROJECT_SLUG="\"$GAME_SLUG\""
    -s SINGLE_FILE=1
    -s ALLOW_MEMORY_GROWTH=1
    -s INITIAL_MEMORY=33554432
    -s FORCE_FILESYSTEM=1
    -s "EXPORTED_FUNCTIONS=['_main','_malloc','_free','_wasm_set_pad_mask','_wasm_set_button','_wasm_pull_audio_f32','_wasm_set_paused','_wasm_reset_cartridge','_wasm_save_quick_state','_wasm_load_quick_state','_wasm_sync_sram_now','_wasm_get_telemetry_ptr','_wasm_attach_sega_cd']"
    -s "EXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','HEAPU8','HEAPU32','HEAPF32','callMain']"
    --embed-file "$ROM_FILE@$VFS_PATH"
    --shell-file "$TMP_SHELL"
    -o "$OUTPUT_FILE"
)

info "Compiling self-contained single-file WebAssembly application..."
"$EMCC" "${EMCC_FLAGS[@]}" "$WASM_MAIN" "$PICODRIVE_LIB"

rm -f "$TMP_SHELL"

HTML_SIZE="$(stat -c%s "$OUTPUT_FILE")"
HTML_SIZE_MB="$(awk "BEGIN {printf \"%.2f\", $HTML_SIZE / 1048576}")"

echo "====================================================================="
info "SUCCESS: Packaged standalone single-file WebAssembly application!"
info "Output: $OUTPUT_FILE (${HTML_SIZE_MB} MB)"
info "Open in browser: file://$OUTPUT_FILE"
echo "====================================================================="

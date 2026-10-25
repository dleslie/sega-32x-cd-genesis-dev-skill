#!/usr/bin/env bash
# ==============================================================================
# Sega CD / Mega-CD Disc Mastering Pipeline
# Generates bootable ISO-9660 images, CUE sheets, and compressed CHD disc images
#
# Copyright (c) 2026 Dan Leslie
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

# Helper: print error and exit
die() {
    echo "!! [DISC ERROR] $*" >&2
    exit 1
}

# Helper: print step info
info() {
    echo ">> [DISC] $*"
}

# Default parameters
OUTPUT_FILE="out/game.chd"
DATA_DIR=""
BOOT_HEADER=""
AUDIO_DIR=""
VOLUME_ID="SEGA_CD_GAME"
GAME_TITLE="SEGA CD HOMEBREW GAME"
CUE_ONLY=0

# Locate project root and opt directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPT_DIR="${OPT_DIR:-$PROJECT_ROOT/opt}"

usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  -o, --output <FILE>       Output disc image (.chd, .cue, or .iso) [default: out/game.chd]
  -d, --data-dir <DIR>      Directory of files to stage in ISO-9660 filesystem
  -b, --boot-header <FILE>  Sega CD boot header / IP binary (sectors 0-15, up to 32 KB)
  -a, --audio-dir <DIR>     Directory containing Redbook CD-DA .wav audio tracks
  -V, --volume <LABEL>      ISO-9660 volume label [default: SEGA_CD_GAME]
  -t, --title <TITLE>       Game title for generated boot header [default: SEGA CD HOMEBREW GAME]
      --cue-only            Stop after generating .iso and .cue (do not run chdman)
  -h, --help                Show this help message
EOF
    exit 0
}

# Parse command line options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -d|--data-dir)
            DATA_DIR="$2"
            shift 2
            ;;
        -b|--boot-header)
            BOOT_HEADER="$2"
            shift 2
            ;;
        -a|--audio-dir)
            AUDIO_DIR="$2"
            shift 2
            ;;
        -V|--volume)
            VOLUME_ID="$2"
            shift 2
            ;;
        -t|--title)
            GAME_TITLE="$2"
            shift 2
            ;;
        --cue-only)
            CUE_ONLY=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            die "Unknown argument: $1. Run with --help for options."
            ;;
    esac
done

# Resolve genisoimage / mkisofs
GENISOIMAGE=""
if [[ -x "$OPT_DIR/bin/genisoimage" ]]; then
    GENISOIMAGE="$OPT_DIR/bin/genisoimage"
elif command -v genisoimage >/dev/null 2>&1; then
    GENISOIMAGE="$(command -v genisoimage)"
elif command -v mkisofs >/dev/null 2>&1; then
    GENISOIMAGE="$(command -v mkisofs)"
else
    die "genisoimage or mkisofs not found in $OPT_DIR/bin or PATH. Run: bash assets/setup.sh"
fi

# Resolve chdman
CHDMAN=""
if [[ -x "$OPT_DIR/bin/chdman" ]]; then
    CHDMAN="$OPT_DIR/bin/chdman"
elif command -v chdman >/dev/null 2>&1; then
    CHDMAN="$(command -v chdman)"
fi

if [[ $CUE_ONLY -eq 0 && -z "$CHDMAN" ]]; then
    info "Warning: chdman not found; outputting .cue and .iso only."
    CUE_ONLY=1
fi

# Ensure staging / data directory exists
TMP_DATA_DIR=""
if [[ -z "$DATA_DIR" || ! -d "$DATA_DIR" ]]; then
    TMP_DATA_DIR="$(mktemp -d /tmp/segacd_data.XXXXXX)"
    DATA_DIR="$TMP_DATA_DIR"
    echo "SEGA CD DISC FILESYSTEM" > "$DATA_DIR/README.TXT"
fi

# Output directory preparation
OUT_DIR="$(dirname "$OUTPUT_FILE")"
mkdir -p "$OUT_DIR"
BASE_NAME="$(basename "$OUTPUT_FILE" .chd)"
BASE_NAME="$(basename "$BASE_NAME" .cue)"
BASE_NAME="$(basename "$BASE_NAME" .iso)"

TRACK01_ISO="$OUT_DIR/${BASE_NAME}_track01.iso"
CUE_FILE="$OUT_DIR/${BASE_NAME}.cue"

# Generate boot header if none was provided
TMP_BOOT_HEADER=""
if [[ -z "$BOOT_HEADER" || ! -f "$BOOT_HEADER" ]]; then
    TMP_BOOT_HEADER="$(mktemp /tmp/segacd_boot.XXXXXX)"
    BOOT_HEADER="$TMP_BOOT_HEADER"
    info "Generating standard Sega CD boot header for '$GAME_TITLE'..."

    # Create 32768 byte (16 sectors) standard Sega CD header with SEGADISCSYSTEM signature
    python3 -c "
title = '''$GAME_TITLE'''.encode('ascii')[:48].ljust(48, b' ')
header = bytearray(32768)

# 0x000: System ID
header[0:16] = b'SEGADISCSYSTEM  '
# 0x010: Copyright
header[16:32] = b'(C)SEGA 2026.JAN'
# 0x020: Domestic Title
header[32:80] = title
# 0x050: Overseas Title
header[80:128] = title
# 0x080: Serial Number
header[128:144] = b'GM 00000000-00  '
# 0x090: Checksum
header[144:146] = b'\x00\x00'
# 0x092: Device Support
header[146:160] = b'J             '
# 0x0A0: ROM start/end (512 KB PRG RAM bounds)
header[160:164] = (0).to_bytes(4, 'big')
header[164:168] = (0x0007FFFF).to_bytes(4, 'big')
# 0x0A8: RAM start/end (Genesis Work RAM)
header[168:172] = (0x00FF0000).to_bytes(4, 'big')
header[172:176] = (0x00FFFFFF).to_bytes(4, 'big')
# 0x0B0: Backup RAM (SRAM) ID & bounds
header[176:188] = b'RA\xf8 \x00 \x00\x01\x00 \x01\xff'
# 0x0F0: Country Codes
header[240:256] = b'JUE             '

with open('''$BOOT_HEADER''', 'wb') as f:
    f.write(header)
"
fi

# Step 1: Master ISO-9660 Track 01 with generic boot header
info "Mastering ISO-9660 Mode 1 data track..."
"$GENISOIMAGE" \
    -quiet \
    -iso-level 1 \
    -pad \
    -R \
    -V "$VOLUME_ID" \
    -G "$BOOT_HEADER" \
    -o "$TRACK01_ISO" \
    "$DATA_DIR"

ISO_SIZE="$(stat -c%s "$TRACK01_ISO")"
info "Data track created: $TRACK01_ISO ($ISO_SIZE bytes)"

# Step 2: Generate CUE sheet
info "Generating disc CUE sheet: $CUE_FILE..."
TRACK01_BASENAME="$(basename "$TRACK01_ISO")"

cat << EOF > "$CUE_FILE"
FILE "$TRACK01_BASENAME" BINARY
  TRACK 01 MODE1/2048
    INDEX 01 00:00:00
EOF

# Process optional CD-DA WAV tracks
TRACK_NUM=2
if [[ -n "$AUDIO_DIR" && -d "$AUDIO_DIR" ]]; then
    # Look for wav files sorted by filename
    shopt -s nullglob
    WAV_FILES=("$AUDIO_DIR"/*.wav "$AUDIO_DIR"/*.WAV)
    shopt -u nullglob

    for wav in "${WAV_FILES[@]}"; do
        WAV_BASENAME="$(basename "$wav")"
        # Copy or link WAV to output directory next to CUE sheet if not already there
        if [[ "$(dirname "$wav")" != "$OUT_DIR" ]]; then
            cp -f "$wav" "$OUT_DIR/$WAV_BASENAME"
        fi

        TRACK_STR="$(printf "%02d" "$TRACK_NUM")"
        cat << EOF >> "$CUE_FILE"
FILE "$WAV_BASENAME" WAVE
  TRACK $TRACK_STR AUDIO
EOF
        if [[ $TRACK_NUM -eq 2 ]]; then
            echo "    PREGAP 00:02:00" >> "$CUE_FILE"
        fi
        echo "    INDEX 01 00:00:00" >> "$CUE_FILE"

        info "Added CD-DA audio track $TRACK_STR: $WAV_BASENAME"
        TRACK_NUM=$((TRACK_NUM + 1))
    done
fi

# Step 3: Compress with chdman if requested
if [[ $CUE_ONLY -eq 0 ]]; then
    FINAL_CHD="${OUTPUT_FILE}"
    if [[ "$FINAL_CHD" != *.chd ]]; then
        FINAL_CHD="${OUT_DIR}/${BASE_NAME}.chd"
    fi

    info "Compressing disc image to CHD using chdman..."
    # chdman resolves relative filenames against CWD, so run inside OUT_DIR
    (
        cd "$OUT_DIR"
        "$CHDMAN" createcd \
            -i "$(basename "$CUE_FILE")" \
            -o "$(basename "$FINAL_CHD")" \
            -f
    )

    CHD_SIZE="$(stat -c%s "$FINAL_CHD")"
    info "Sega CD disc image successfully mastered: $FINAL_CHD ($CHD_SIZE bytes)"
else
    info "CUE/ISO generation complete: $CUE_FILE and $TRACK01_ISO"
fi

# Clean up temporary directories/files
if [[ -n "$TMP_DATA_DIR" && -d "$TMP_DATA_DIR" ]]; then
    rm -rf "$TMP_DATA_DIR"
fi
if [[ -n "$TMP_BOOT_HEADER" && -f "$TMP_BOOT_HEADER" ]]; then
    rm -f "$TMP_BOOT_HEADER"
fi

info "Disc mastering pipeline completed successfully."

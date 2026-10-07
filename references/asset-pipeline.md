# Decoupled Intermediate Asset Pipeline Reference

This guide details the architecture, implementation, and advantages of the
**Decoupled Two-Stage Intermediate Asset Pipeline** for retro console ports
and original homebrew on Sega Genesis, Sega CD, and Sega 32X.

---

## 1. Architectural Philosophy: Why Direct Extraction is an Anti-Pattern

In naive porting projects, developers frequently write build scripts that extract
assets directly from proprietary or legacy source archives (e.g. PCX, WAD, MPQ, PAK,
or raw binary containers) and immediately emit target-specific assembly or binary blobs
in a single monolithic compile step.

### Failures of Direct Extraction:
1. **Entangled Tooling**: Build systems require legacy unpackers, external DLLs, or proprietary
   archive parsers on every build machine, breaking cross-compilation and CI environments.
2. **Hostile to Art & Sound Revision**: Artists and sound designers cannot easily view, edit,
   or replace assets without reverse-engineering the archive format or modifying conversion scripts.
3. **Slow Build Times**: Rebuilding requires re-parsing large archives repeatedly.
4. **Fragile Modding**: Community mods or alternative asset packs cannot be dropped into the project.

---

## 2. The Two-Stage Decoupled Pipeline

To solve these issues, the asset pipeline is split into two completely decoupled stages
connected by **standard, uncompressed intermediate formats**:

```
[Raw Sources / Archive / Legacy Formats]
       │
       ▼  Stage 1: make extract-assets (Independent post-clone setup step)
[Intermediate Standard Assets: assets/]
  ├── assets/sprites/   -> Uncompressed PNG sheets + JSON metadata manifests
  ├── assets/tilesets/  -> Uncompressed PNG tilesets + tile definitions
  ├── assets/ui/        -> Uncompressed PNG interface elements & fonts
  ├── assets/audio/wav/ -> Uncompressed standard 16-bit 44.1 kHz PCM WAVs
  └── assets/audio/vgm/ -> Standard VGM v1.50+ chiptune music tracks
       │
       ▼  Stage 2: make (Standard build process)
[Target Baked Binaries]
  ├── Genesis: SGDK rescomp (.res -> C symbols, tilemaps, sprite definitions, XGM2)
  ├── 32X: Palettized 8bpp bitmaps, linear tile arrays, packed SSF bank pages
  └── Sega CD: Word RAM stamp sheets, Ricoh RF5C164 PCM blocks, ISO-9660 filesystem
```

### Stage 1: Asset Extraction & Normalization (`make extract-assets`)
- **Execution**: Run exactly once after cloning the repository, or whenever a new
  asset source is imported.
- **Independence**: The regular compilation build (`make`) never calls Stage 1 tools.
- **Output Formats**:
  - **Images**: Uncompressed PNG files. Palettes should be authored or quantized with
    transparent pixels placed at index 0.
  - **Metadata**: Human-readable JSON files accompanying PNG files to store frame bounding
    boxes, animation timings, origins, and collision bounds:
    ```json
    {
      "sprite": "player_walk",
      "frame_width": 32,
      "frame_height": 32,
      "frame_count": 8,
      "frame_duration_ticks": 6,
      "pivot_x": 16,
      "pivot_y": 30
    }
    ```
  - **Audio**: Standard uncompressed 16-bit 44.1 kHz or 22.05 kHz stereo/mono WAV files.
  - **Music**: Standard uncompressed `.vgm` files authored in Furnace Tracker or DefleMask.

### Stage 2: Hardware-Target Baking (`make`)
- **Input**: Reads exclusively from the `assets/` directory.
- **Conversion**:
  - Converts intermediate PNGs into target VRAM tile patterns, packed 8bpp bitmaps, or
    Mode-7 affine stamps.
  - Converts intermediate WAVs into hardware audio formats:
    - Genesis: SGDK `rescomp` 8-bit PCM samples (`WAV`).
    - 32X: 4-bit IMA-ADPCM compressed audio banks or 12-bit PWM raw sample tables.
    - Sega CD: 8-bit unsigned PCM for the Ricoh RF5C164 chip, or redbook audio tracks on CD.
  - Packs data into banked cartridge pages (e.g. 512 KiB pages via `bank_packer.py` for SSF cartridges).

---

## 3. Makefile Integration Pattern

Here is the canonical `Makefile` structure enforcing this decoupling:

```makefile
# ==============================================================================
# Decoupled Intermediate Asset Pipeline
# ==============================================================================

# Directory containing normalized intermediate assets
ASSET_DIR := assets
SPRITE_PNGS := $(wildcard $(ASSET_DIR)/sprites/*.png)
TILE_PNGS   := $(wildcard $(ASSET_DIR)/tilesets/*.png)
WAV_FILES   := $(wildcard $(ASSET_DIR)/audio/wav/*.wav)
VGM_FILES   := $(wildcard $(ASSET_DIR)/audio/vgm/*.vgm)

# Stage 2: Baking targets (Executed during standard 'make')
BAKED_DIR := gen/baked
BAKED_TILES   := $(BAKED_DIR)/tiles.bin
BAKED_SPRITES := $(BAKED_DIR)/sprites.bin
BAKED_AUDIO   := $(BAKED_DIR)/audio.bin

$(BAKED_TILES): tools/bake_tiles.py $(TILE_PNGS) | $(BAKED_DIR)
	python3 tools/bake_tiles.py --in $(ASSET_DIR)/tilesets --out $@

$(BAKED_SPRITES): tools/bake_sprites.py $(SPRITE_PNGS) | $(BAKED_DIR)
	python3 tools/bake_sprites.py --in $(ASSET_DIR)/sprites --out $@

$(BAKED_AUDIO): tools/bake_audio.py $(WAV_FILES) | $(BAKED_DIR)
	python3 tools/bake_audio.py --in $(ASSET_DIR)/audio/wav --out $@

$(BAKED_DIR):
	mkdir -p $@

# Stage 1: Independent post-clone extraction target
.PHONY: extract-assets
extract-assets:
	@echo "== Extracting and normalizing assets into uncompressed PNG / WAV =="
	python3 tools/extract_all_assets.py --source data/source_archive --out $(ASSET_DIR)
	@echo "Asset extraction complete. Intermediate files populated in $(ASSET_DIR)/"
```

---

## 4. Modern Artist & Modding Workflow

With this pipeline in place:
1. **Modding**: Any artist can open `assets/sprites/hero.png` in **Aseprite**, **Photoshop**,
   or **GIMP**, edit pixels, modify colors, or add animation frames, and immediately
   run `make` to test the new art on hardware.
2. **Audio Swapping**: Sound engineers can drop new `.wav` files into `assets/audio/wav/`
   or new `.vgm` tracks into `assets/audio/vgm/` created with **Furnace Tracker**, and
   the build automatically re-bakes the ROM.
3. **Zero Code Dependency**: Asset updates never require modifying C source code or linker scripts.

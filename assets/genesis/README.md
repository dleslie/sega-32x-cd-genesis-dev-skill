# Sega Genesis / Mega Drive Hardware Sprite Engine Demo

A pure Sega Genesis / Mega Drive cartridge project demonstrating the hardware VDP sprite engine, dynamic sprite scaling, and hardware priority sorting.

---

## Architecture Overview

This project runs strictly on the standalone Sega Genesis hardware without Sega CD or 32X coprocessors:

```
┌────────────────────────────────────────────────────────┐
│               SEGA GENESIS / MEGA DRIVE                │
│   • Motorola 68000 CPU @ 7.67 MHz                      │
│   • 64 KB Main RAM                                     │
│   • Yamaha YM2612 FM Synthesis + TI SN76489 PSG Sound │
│   • Yamaha VDP (Video Display Processor):              │
│       - 80 Hardware Sprites (H40 Mode, 320x224)        │
│       - 16 Sprite Sizes (1x1 up to 4x4 tiles)          │
│       - 4 Palettes (16 colors each = 64 colors)        │
│       - 2 Scrolling Background Planes (Plane A & B)    │
└────────────────────────────────────────────────────────┘
```

---

## Feature Demonstration

The demo provides two switchable interactive modes toggled by pressing <kbd>START</kbd>:

### 1. 3D DNA Double Helix
- Two intertwined helical strands rotating smoothly in 3D perspective.
- 16 rungs (base pairs) connecting Strand Alpha (Cyan) and Strand Beta (Ruby).
- 32 nucleotide sphere sprites dynamically scaling across all 4 VDP sprite sizes according to depth $Z$:
  - $Z \in [-48, -16)$: $8\times 8$ pixels (`SPRITE_SIZE(1, 1)`), dark shaded palette (`PAL0`)
  - $Z \in [-16, 16)$: $16\times 16$ pixels (`SPRITE_SIZE(2, 2)`), mid-depth palette (`PAL1`)
  - $Z \in [16, 36)$: $24\times 24$ pixels (`SPRITE_SIZE(3, 3)`), bright palette (`PAL2`)
  - $Z \in [36, 48]$: $32\times 32$ pixels (`SPRITE_SIZE(4, 4)`), glowing highlighted foreground sphere (`PAL3`)
- 16 horizontal base-pair connector rung sprites linking the two strands.
- Continuous sine wave undulation and twisting along the vertical axis.
- Depth-sorted so foreground spheres and rungs cleanly pass in front of background spheres!

### 2. Mode-7 Style Infinite Scaling Forest
- Pseudo-Mode 7 perspective ground plane rushing toward camera using line-by-line raster scrolling.
- 36 pine trees distributed across the infinite plane scaling up as they approach:
  - Horizon ($Z > 210$): $8\times 8$ (`SPRITE_SIZE(1, 1)`)
  - Distance ($140 < Z \le 210$): $16\times 16$ (`SPRITE_SIZE(2, 2)`)
  - Mid-range ($70 < Z \le 140$): $24\times 24$ (`SPRITE_SIZE(3, 3)`)
  - Near camera ($Z \le 70$): $32\times 32$ (`SPRITE_SIZE(4, 4)`)
- Responsive steering with D-Pad Left/Right and speed control / turbo boost with D-Pad Up/Down and Button A.

---

## Controls

| Button | 3D DNA Double Helix | Infinite Scaling Forest |
| :--- | :--- | :--- |
| **D-Pad Left / Right** | Manual spin rotation | Steer left / right |
| **D-Pad Up / Down** | Adjust helix radius / zoom | Accelerate / Decelerate |
| **Button A** | Toggle sine wave undulation | Turbo boost speed |
| **Button B** | Cycle helix radius preset | Brake / slow speed |
| **Button START** | Switch to Infinite Forest | Switch to 3D DNA Helix |

---

## Build & Run

```bash
# Compile cartridge ROM to out/genesis_demo.bin
make -f assets/Makefile.genesis

# Run headless emulator verification test (Genesis Plus GX)
make -f assets/Makefile.genesis check

# Package into standalone WebAssembly single-file HTML application
source ~/.emsdk/emsdk_env.sh
bash assets/wasm/package_wasm.sh -r out/genesis_demo.bin -o dist/genesis_demo.html -t "Sega Genesis Sprite Demo"
```

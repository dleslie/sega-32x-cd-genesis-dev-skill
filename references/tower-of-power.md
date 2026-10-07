# Sega "Tower of Power" Architecture & Development Guide
## (Genesis / Mega Drive + Sega CD / Mega-CD + Sega 32X + Mega EverDrive)

The **"Tower of Power"** is the pinnacle Sega 16/32-bit hardware stack:
- **Base Console**: Sega Genesis / Mega Drive (Motorola 68000 @ 7.67 MHz + Z80 @ 3.58 MHz)
- **CD Expansion**: Sega CD / Mega-CD (Sub-CPU Motorola 68000 @ 12.5 MHz + ASIC affine coprocessor + Ricoh RF5C164 8-ch PCM)
- **32-Bit Expansion**: Sega 32X / Mars (Dual Hitachi SH-2 @ 23 MHz + 12-bit Stereo PWM + Direct-Color Framebuffers)
- **Flash Cartridge Peripheral**: Mega EverDrive (Pro / X7 / CORE with MicroSD streaming & USB `edlink` host debugging)

When fully assembled, the system features **5 distinct microprocessors** running concurrently:
1. **Genesis Main-CPU**: Motorola 68000 @ 7.67 MHz
2. **Sega CD Sub-CPU**: Motorola 68000 @ 12.5 MHz
3. **32X Master SH-2**: Hitachi SH-2 (SH7604) @ 23 MHz
4. **32X Slave SH-2**: Hitachi SH-2 (SH7604) @ 23 MHz
5. **Genesis Sound Co-Processor**: Zilog Z80 @ 3.58 MHz

---

## 1. System Architecture & Memory Map

```
┌────────────────────────────────────────────────────────────────────────┐
│                         GENESIS MAIN 68000                             │
│       Controls VDP, Joypads, Bus Arbitration, Audio Co-Processing      │
└───────────┬───────────────────────────────┬────────────────────────────┘
            │                               │
            ▼                               ▼
┌───────────────────────────────┐   ┌────────────────────────────────────┐
│      SEGA CD (SUB-CPU 68K)    │   │        SEGA 32X (DUAL SH-2)        │
│   • 512 KB PRG RAM            │   │   • 256 KB SDRAM (Shared)          │
│   • 256 KB Word RAM (1M/2M)   │   │   • 2×128 KB Framebuffers          │
│   • ASIC Affine Coprocessor   │   │   • 12-bit Stereo PWM Audio FIFO   │
│   • Ricoh RF5C164 PCM (64 KB) │   │   • Direct-Color 15bpp Compositing │
│   • CD-ROM & CD-DA Streaming  │   │   • Cartridge Bank Mapper (SSF)    │
└───────────┬───────────────────┘   └─────────────────┬──────────────────┘
            │                                         │
            └────────────────────┬────────────────────┘
                                 │
                                 ▼
            ┌─────────────────────────────────────────┐
            │       MEGA EVERDRIVE PRO / X7 / CORE    │
            │   • MicroSD FAT Filesystem Streaming    │
            │   • USB Host Link (edlink) Debugging    │
            │   • Cartridge Save BRAM Management      │
            └─────────────────────────────────────────┘
```

### Memory Map Comparison Across Processors

| Memory Region | Genesis 68000 View | Sega CD Sub-68000 View | 32X SH-2 View (Cache-Through) |
| :--- | :--- | :--- | :--- |
| **Genesis Work RAM (64 KB)** | `0xFF0000..0xFFFFFF` | Inaccessible | Inaccessible |
| **Genesis VDP Registers** | `0xC00000..0xC0001F` | Inaccessible | Inaccessible |
| **CD BIOS ROM (128 KB)** | `0x000000..0x01FFFF` (at boot) | Inaccessible | Inaccessible |
| **CD Sub-CPU PRG RAM (512 KB)**| `0x020000..0x03FFFF` (banked window)| `0x000000..0x07FFFF` | Inaccessible |
| **CD Word RAM (256 KB)** | `0x200000..0x23FFFF` (2M) or `0x200000..0x21FFFF` (1M) | `0x080000..0x0BFFFF` (2M) or `0x0C0000..0x0DFFFF` (1M) | Inaccessible directly |
| **CD Communication Registers**| `0xA12000..0xA1202F` | `0xFF8000..0xFF802F` | Inaccessible |
| **32X SDRAM (256 KB)** | `0x840000..0x87FFFF` (or via bank) | Inaccessible | `0x06000000..0x0603FFFF` (`0x26000000` through) |
| **32X Framebuffers (2×128 KB)**| `0x840000` (overwriting window)| Inaccessible | `0x04000000..0x0401FFFF` (`0x24000000` through) |
| **32X Communication Regs** | `0xA15120..0xA1512F` | Inaccessible | `0x20004020..0x2000402F` |
| **Mega EverDrive EDIO Regs**| `0xA130D0..0xA130DF` | Inaccessible | Inaccessible |
| **Cartridge ROM (4–32 MiB)** | `0x000000..0x3FFFFF` | Inaccessible | `0x02000000..0x023FFFFF` |

---

## 2. Bootstrapping & Hardware Handshake Protocol

Because the Sega CD and 32X hardware cannot communicate directly over an internal bus, the **Genesis Main 68000 acts as the master coordinator and arbitrator**.

### Multi-Stage Boot Sequence

```
[Power On]
   │
   ▼
1. Genesis 68000 Boots (SGDK init, VDP setup, pad init)
   │
   ├─► Probes 32X: Reads 0xA130EC for 'MARS' identifier.
   ├─► Probes Sega CD: Reads 0xA12000 for Sub-CPU reset register.
   └─► Probes EverDrive: Unlocks 0xA130D0 with key 0x1234.
   │
   ▼
2. 32X Subsystem Initialization
   ├─► Genesis writes 0 to COMM0..COMM7.
   ├─► Genesis sets ADEN = 1 at 0xA15100 (enables Mars hardware).
   ├─► Genesis sets nRES = 1 at 0xA15102 (releases Master & Slave SH-2 reset).
   ├─► Master SH-2 boots (mars_crt0.s), sets VBR, stack (0x0603F800), enables cache.
   ├─► Slave SH-2 boots, sets VBR, stack (0x06040000), enables cache.
   ├─► Master SH-2 copies .data from ROM to SDRAM, clears .bss, writes 'MOK ' to COMM0.
   ├─► Slave SH-2 writes 'SOK ' to COMM2.
   ├─► Genesis confirms signatures in COMM0/COMM2, writes 'GO! ' to COMM4.
   └─► Master jumps to mars_master_main(), Slave jumps to mars_slave_main().
   │
   ▼
3. Sega CD Subsystem Initialization
   ├─► Genesis sets RESET = 1, BUSREQ = 0 at 0xA12001 (Sub-CPU starts running).
   ├─► Genesis configures Word RAM to 1M Mode (MODE = 1 at 0xA12003).
   ├─► Sub-CPU boots (sub_crt0.s), sets vectors, initializes Ricoh PCM.
   ├─► Sub-CPU writes STAT_READY (0x0001) to CD COMM status register.
   └─► Genesis acknowledges Sub-CPU ready.
   │
   ▼
4. Mega EverDrive Configuration
   ├─► Genesis configures EDIO registers at 0xA130D0.
   └─► Connects USB debug console (edlink) for live serial logging.
```

---

## 3. The 4-CPU Concurrency & Workload Distribution Model

| Processor | Primary Workload | Refresh Rate | Synchronization Primitive |
| :--- | :--- | :--- | :--- |
| **Genesis Main 68000** | Controller polling, SGDK sprite engine, tilemap HUD/UI, bus bridge | 60 Hz (VBlank) | `SYS_doVBlankProcess()` |
| **Sega CD Sub-68000** | CD-ROM file streaming, CD-DA track control, Ricoh PCM SFX, ASIC transforms | 60 Hz (Async) | Word RAM `RET`/`DMNA` toggle |
| **32X Master SH-2** | 3D scene graph, camera interpolation, direct-color framebuffer rasterization | 60 Hz | `MARS_VDP_FS ^= 1` |
| **32X Slave SH-2** | 3D vertex math, triangle rendering, 12-bit stereo PWM audio mixing | Continuous | PWM FIFO & COMM3 ticks |
| **Genesis Z80** | Yamaha YM2612 6-channel FM + SN76489 4-channel PSG (XGM2 driver) | 60 Hz | Z80 Bus Request |

---

## 4. The Word RAM to 32X SDRAM Graphics Bridge

The Sega CD ASIC can generate real-time affine scaled/rotated backgrounds and sprite stamps into Word RAM. In a 32X CD configuration:

1. **Word RAM 1M Ping-Pong Mode**:
   - Sub-CPU renders frame $N$ with the ASIC into Bank 0 (`0x0C0000`).
   - Genesis Main-CPU accesses previously rendered Bank 1 (`0x200000`).
2. **Display Strategies**:
   - **Genesis Underlay**: Genesis 68000 transfers Word RAM Bank 1 tiles via DMA directly into Genesis VRAM Plane B. The 32X VDP renders transparent pixels where the Genesis background should show through.
   - **SDRAM Texture Upload**: Genesis 68000 burst-copies rendered affine textures from Word RAM into 32X SDRAM (`0x840000`), allowing the Master/Slave SH-2s to map them onto 3D polygon meshes.
3. **VBlank Handshake**:
   - Both CPUs toggle the `RET` bit at VBlank to swap banks without bus stalls.

---

## 5. 4-Way Audio Engine Coordination

The Tower of Power offers the most versatile sound hardware combination of the 16-bit era:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        TOWER AUDIO HARDWARE MATRIX                     │
├───────────────────┬────────────────────────────────────────────────────┤
│ Yamaha YM2612     │ 6 FM channels (FM synthesis, melody, bass, rhythm) │
│ TI SN76489 (PSG)  │ 3 square wave channels + 1 white noise channel     │
│ Ricoh RF5C164     │ 8 PCM voice channels @ up to 32 kHz (stereo pan)   │
│ CD-DA Redbook     │ 16-bit 44.1 kHz uncompressed studio stereo audio   │
│ 32X Stereo PWM    │ 12-bit stereo PWM FIFO (~22.05 kHz)                │
└───────────────────┴────────────────────────────────────────────────────┘
```

### Mixing Strategy
- **Background Music**: Redbook CD-DA tracks streamed directly from disc (zero CPU/RAM overhead) OR 16-channel VGM v1.50+ tracks driving YM2612 FM + PSG.
- **Sound Effects & Voice**: Ricoh RF5C164 8-channel PCM on Sega CD for multi-voice ambient effects and speech.
- **Synthesizer / Dynamic Audio**: 32X PWM stereo audio mixed by Slave SH-2 for positional 3D sound effects or interactive music.

---

## 6. Mega EverDrive Fast Development Workflow

Developing on original hardware without burning CDs or reflashing slow flash chips:

### USB Debug Logging via `edlink`
```c
#include "everdrive/everdrive.h"

evd_init();
evd_puts(">> [DEBUG] Level 3 loaded, entities initialized\n");
```
Run `edlink` on your host PC to capture live `printf` style diagnostic messages directly from running hardware.

### MicroSD Asset Streaming
Load megabytes of level textures, voice lines, and 3D models straight from the FAT filesystem on the MicroSD card into Genesis Work RAM, Sega CD Word RAM, or 32X SDRAM using `evd_read_sectors()`.

---

## 7. Build Automation & Toolchain Setup

### Prebuilt Git-LFS Toolchains (Fastest)
The repository stores full, precompiled GCC 14.2.0 cross-compilers (`m68k-elf` and `sh-elf`) in `artifacts/`:
```bash
git lfs pull
bash assets/setup.sh
```
This unpacks the complete suite into `opt/` in under 5 seconds with zero dependencies.

### Building via Docker
```bash
# Build using the container runner script:
./assets/docker/run.sh make -f assets/Makefile.tower

# Or run interactively:
docker run --rm -v "$PWD":/work -w /work sega-tower-dev:latest make -f assets/Makefile.tower
```

### Build Outputs
- `out/tower_game.bin`: Genesis Main-CPU binary with SGDK runtime.
- `out/tower_game.32x`: Sega 32X dual SH-2 Master/Slave cartridge ROM.
- `out/tower_game_sub.bin`: Sega CD Sub-CPU 68000 PRG-RAM firmware.

# Sega CD & Mega-CD Development Guide

This reference provides a complete technical guide to programming the **Sega CD / Mega-CD**
hardware, including Sub-CPU co-processing, Word RAM bank modes, the ASIC graphics processor,
CD-DA and Ricoh PCM audio, CD-ROM streaming, and 32X CD integration.

---

## 1. System Architecture & Memory Map

The Sega CD adds a complete second 16-bit computer and graphics/sound expansion to the Genesis:

- **Sub-CPU**: Motorola 68000 running at **12.5 MHz** (versus the Genesis Main-CPU's 7.67 MHz).
- **Sub-CPU Program RAM (`PRG-RAM`)**: 512 KB (`0x000000..0x07FFFF` from Sub-CPU view).
  - Main-CPU can write to PRG-RAM when Sub-CPU is halted or in bus-request mode.
- **Word RAM**: 256 KB shared memory accessible by both Main-CPU and Sub-CPU.
- **PCM Wave RAM**: 64 KB dedicated sample memory for the Ricoh RF5C164 sound chip.
- **Backup RAM (`BRAM`)**: 16 KB battery-backed SRAM for saved games.
- **CD-ROM BIOS**: 128 KB ROM (`0x000000..0x01FFFF` from Main-CPU view on boot).

---

## 2. Word RAM Bank Modes: 2M Mode vs 1M Mode

Word RAM is the primary shared graphics buffer between the Sub-CPU/ASIC and the Genesis VDP.
It is controlled by bit 2 of the Memory Control Register (`0xA12003` on Main-CPU / `0xFF8003` on Sub-CPU):

### A. 2M Mode (`MODE = 0`)
- **Configuration**: Single unified **256 KB** memory block.
- **Ownership**: Assigned entirely to either the Main-CPU or the Sub-CPU via the priority bit (`RET` / `DMNA`).
- **Main-CPU Mapping**: `0x200000..0x23FFFF` (16-bit word access).
- **Sub-CPU Mapping**: `0x080000..0x0BFFFF` (16-bit word access).
- **Best For**: Loading large assets, streaming full-motion video (FMV), or preparing static scene data.

### B. 1M Mode (`MODE = 1`) — Ping-Pong Buffer
- **Configuration**: Split into two **128 KB** banks: Bank 0 and Bank 1.
- **Ownership**: One bank is assigned to the Main-CPU while the other is assigned to the Sub-CPU.
- **Sub-CPU Mapping**:
  - Bank 0 or 1 mapped at `0x0C0000..0x0DFFFF`.
- **Main-CPU Mapping**:
  - The alternate bank mapped at `0x200000..0x21FFFF`.
- **Bank Swap Execution**:
  - Main-CPU toggles `RET` bit (bit 0 of `0xA12003`).
  - Sub-CPU toggles `RET` bit (bit 0 of `0xFF8003`).
- **Best For**: 60 fps real-time 3D, Mode 7 affine scaling/rotation, or streaming backgrounds:
  - Frame *N*: Sub-CPU & ASIC render into Bank 0; Main-CPU DMAs Bank 1 into Genesis VRAM.
  - VBlank: Swap banks!
  - Frame *N+1*: Sub-CPU renders into Bank 1; Main-CPU DMAs Bank 0 into Genesis VRAM.

---

## 3. ASIC Graphics Co-Processor

The Sega CD hardware ASIC performs hardware-accelerated affine transformations:
- **Operations**: Arbitrary 2D rotation, scaling (zooming from 1/256 to 256×), and sprite/tile stamping.
- **Destination**: Writes rendered pixel data directly into Word RAM.

### ASIC Parameter Table Structure
The ASIC is configured by writing a parameter table to Sub-CPU Program RAM:
```c
typedef struct {
    uint16_t tile_data_offset;   /* Offset to source stamp tile data */
    uint16_t stamp_map_offset;   /* Offset to stamp map array */
    uint16_t screen_width;       /* Virtual screen width in pixels */
    uint16_t screen_height;      /* Virtual screen height in pixels */
    int16_t  x_center;           /* Center of rotation / scaling X */
    int16_t  y_center;           /* Center of rotation / scaling Y */
    int16_t  dx;                 /* Horizontal step vector (scaling / rotation) */
    int16_t  dxy;                /* Cross step vector */
    int16_t  dy;                 /* Vertical step vector */
    int16_t  dyx;                /* Cross vertical step vector */
} SegaCD_ASIC_Params;
```

---

## 4. CD-ROM Data Streaming & BIOS Services

The Sega CD BIOS provides high-level subroutines executed on the Sub-CPU:

### Key BIOS Calls
- `_CDBBOOT`: Initializes the CD-ROM drive and verifies disc presence.
- `_CDBSTAT`: Queries drive status (tray open, seeking, playing audio, reading data).
- `_CDBREAD`: Reads data sectors from the CD-ROM filesystem into Sub-CPU RAM.
- `_CDBPLAY`: Plays Redbook CD-DA audio tracks from specified sector start to end.
- `_CDBSTOP`: Halts CD-DA audio playback.
- `_CDBPAUSE`: Pauses CD-DA audio playback.

### CD-ROM Streaming Rules
- The CD-ROM drive is single-speed (**150 KB/s**).
- Avoid frequent seeks: Group level data, music tracks, and voice clips sequentially on the disc layout.
- Always buffer streaming sectors into Sub-CPU Program RAM before transferring into Word RAM or Genesis VRAM.

---

## 5. Ricoh RF5C164 PCM Sound Chip

- **Channels**: 8 independent mono voices mixed to stereo.
- **Sample Format**: 8-bit unsigned PCM.
- **Sample Rate**: Up to 32 kHz.
- **Wave RAM**: 64 KB memory mapped to Sub-CPU at `0xFF0000..0xFFFFFF` (banked).
- **Control**:
  - Each channel has dedicated registers for start address, loop address, frequency divider, and left/right stereo pan (16 volume levels each).
  - Perfect for multi-voice sound effects while CD-DA plays background music.

---

## 6. Sega 32X CD Combined Architecture

In a 32X CD setup (the Sega "Neptune CD" combination):
- **4 Microprocessors**:
  - Master SH-2 @ 23 MHz (32X)
  - Slave SH-2 @ 23 MHz (32X)
  - Main 68000 @ 7.67 MHz (Genesis)
  - Sub 68000 @ 12.5 MHz (Sega CD)
- **Role Distribution**:
  - Master SH-2: Main game logic, player input, 32X VDP direct color compositing.
  - Slave SH-2: High-speed 3D rendering or software voxel processing.
  - Genesis Main 68000: Controller I/O, VBlank synchronization, COMM arbitration.
  - Sega CD Sub-CPU 68000: CD-ROM sector streaming, CD-DA audio management, Ricoh PCM effects, and ASIC texture pre-processing.
- **Data Flow**:
  - Sub-CPU streams data sectors from CD-ROM into Word RAM.
  - Main 68000 copies data from Word RAM into 32X SDRAM or passes textures across the bus.

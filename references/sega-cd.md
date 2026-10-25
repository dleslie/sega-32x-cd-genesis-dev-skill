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

The Sega CD custom hardware ASIC performs real-time hardware-accelerated affine transformations:
- **Operations**: Arbitrary 2D rotation, scaling (zooming from 1/256 to 256×), and Mode 7-style perspective plane stamping.
- **Destination**: Rasterizes 4bpp Genesis-formatted VDP tiles directly into Word RAM, enabling immediate DMA to VRAM.

### ASIC Sub-CPU Hardware Registers (`0xFF8058`..`0xFF8066`)

| Register Address | Name | Description |
| :--- | :--- | :--- |
| `0xFF8058` | **Stamp Size & Status (`STAMP_SIZE`)** | Bit 15 (`GRON`): 1 = Busy rendering, 0 = Idle.<br>Bit 2 (`SMS`): Stamp Map Size (0 = 256×256 px, 1 = 4096×4096 px).<br>Bit 1 (`STS`): Stamp Size (0 = 16×16 px, 1 = 32×32 px).<br>Bit 0 (`RPT`): Repeat wrap when coordinates out of bounds. |
| `0xFF805A` | **Stamp Map Base Address** | Word RAM offset to stamp map divided by 4 (`addr >> 2`). |
| `0xFF805C` | **Image Buffer Vertical Cell Size** | Height of destination buffer in cells minus 1: `(height / 8) - 1`. |
| `0xFF805E` | **Image Buffer Start Address** | Word RAM destination offset divided by 4 (`addr >> 2`). |
| `0xFF8060` | **Image Buffer Offset** | Fine sub-cell pixel offsets (Bits 0–2: X offset, Bits 3–5: Y offset). |
| `0xFF8062` | **Image Buffer Horizontal Pixel Size** | Destination rendering width in pixels (e.g. 160 or 256). |
| `0xFF8064` | **Image Buffer Vertical Pixel Size** | Destination rendering height in pixels (e.g. 128 or 224; decrements to 0 during operation). |
| `0xFF8066` | **Trace Table Base Address** | Word RAM offset to trace table divided by 4 (`addr >> 2`). **Writing triggers hardware execution!** |

### Scanline Trace Table Structure
The ASIC processes the destination buffer line by line, reading one 8-byte entry per horizontal scanline from Word RAM:
```c
typedef struct {
    int16_t start_x;  /* Initial horizontal coordinate in 13.3 fixed-point (pixel * 8) */
    int16_t start_y;  /* Initial vertical coordinate in 13.3 fixed-point (pixel * 8) */
    int16_t delta_x;  /* Horizontal step increment per pixel in signed 5.11 fixed-point */
    int16_t delta_y;  /* Vertical cross step increment per pixel in signed 5.11 fixed-point */
} __attribute__((packed)) SegaCD_ASIC_TraceEntry;
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

The Sega CD incorporates a dedicated **Ricoh RF5C164** wavetable sound processor delivering 8 independent channels of 8-bit PCM audio playback with per-channel envelope volume and stereo panning.

### Hardware Architecture & Memory Alignment

- **8-Bit Bus Alignment**: Because the RF5C164 is an 8-bit peripheral attached to the 16-bit Motorola 68000 bus, registers and Wave RAM are mapped strictly to **odd byte addresses** (`0xFF0001`, `0xFF0003`, `0xFF0005`, etc.).
- **Wave RAM**: 64 KB total sample memory accessed through a 4 KB window mapped to `0xFF2001..0xFF3FFF` on odd byte offsets.
- **Sample Format**: 8-bit unsigned PCM (`128` = silence / 0 amplitude).
- **Hardware Loop Sentinel (`0xFF`)**: The RF5C164 treats the byte value `0xFF` strictly as the loop/stop marker. Audio sample bytes of `0xFF` must be converted to `0xFE` prior to staging into Wave RAM.

### Sub-CPU Hardware Register Aperture

| Register Address | Name | Description |
| :--- | :--- | :--- |
| `0xFF0001` | **`ENV` (Volume / Envelope)** | Channel master amplitude ($0..255$). |
| `0xFF0003` | **`PAN` (Stereo Panning)** | Left/Right volume (Bits 4–7: Left $0..15$, Bits 0–3: Right $0..15$). Center = `0xFF`. |
| `0xFF0005` | **`FDL` (Frequency Step Low)** | Low byte of 16-bit address increment ($5.11$ fixed-point format). |
| `0xFF0007` | **`FDH` (Frequency Step High)** | High byte of 16-bit address increment ($0x0800 = 1.0\times$ normal pitch). |
| `0xFF0009` | **`LSL` (Loop Address Low)** | Low byte of loop restart address in Wave RAM. |
| `0xFF000B` | **`LSH` (Loop Address High)** | High byte of loop restart address in Wave RAM. |
| `0xFF000D` | **`ST` (Sample Start Page)** | High byte of starting address in Wave RAM (`start_addr >> 8`). |
| `0xFF000F` | **`CTRL` (Master Control)** | Bit 7: Sound Enable ($1 = \text{ON}$).<br>Bit 6: Mode ($1 = \text{Channel Select}$, $0 = \text{RAM Bank Select}$).<br>Bits 0–2: Target channel ($0..7$) when Mode=1.<br>Bits 0–3: 4 KB RAM bank ($0..15$) when Mode=0. |
| `0xFF0011` | **`CHAN_ENABLE` (Channel Mute)** | 8-bit channel bitmask ($1 = \text{Muted}$, $0 = \text{Active / Playing}$). |
| `0xFF2001` | **Wave RAM Banking Window** | 4 KB window into 64 KB Wave RAM on odd byte offsets ($0xFF2001 + \text{offset} \times 2$). |

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

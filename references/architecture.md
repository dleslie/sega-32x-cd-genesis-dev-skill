# Sega Hardware Architecture Reference

This reference covers the complete hardware model, memory maps, inter-CPU communication,
register interfaces, and hardware constraints across the Sega Genesis, Sega CD, Sega 32X,
and 32X CD platforms.

---

## 1. System Overviews & Specifications

### A. Sega Genesis / Mega Drive
- **Main CPU**: Motorola 68000 @ 7.67 MHz (16-bit external bus, 32-bit internal registers).
- **Sound CPU**: Zilog Z80 @ 3.58 MHz (8-bit, dedicated to driving sound chips).
- **Video Display Processor (VDP)**:
  - 64 KB VRAM, 80 bytes CRAM (Color RAM), 40 bytes VSRAM (Vertical Scroll RAM).
  - Resolutions: 320×224 (H40, NTSC), 256×224 (H32, NTSC), 320×240 / 256×240 (PAL).
  - Two independent scroll planes (`BG_A`, `BG_B`) + 1 fixed overlay `WINDOW` plane.
  - Up to 80 hardware sprites (max 20 per scanline in H40 mode, 16 in H32 mode).
  - 4 palettes × 16 colors (64 total colors from 512 possible 9-bit BGR colors). Palette index 0 is transparent.
- **Audio Chips**:
  - Yamaha YM2612: 6 FM channels (channel 6 can function as an 8-bit DAC).
  - Texas Instruments SN76489 (PSG): 3 square wave channels + 1 white/periodic noise channel.
- **Memory**:
  - 64 KB Main Work RAM (`0xFF0000..0xFFFFFF`).
  - 8 KB Z80 RAM (`0xA00000..0xA01FFF`).
  - Standard Cartridge ROM window: 4 MiB (`0x000000..0x3FFFFF`).

### B. Sega CD / Mega-CD
- **Sub-CPU**: Motorola 68000 @ 12.5 MHz (1.6× the Genesis Main-CPU clock speed).
- **ASIC Graphics Co-Processor**:
  - Hardware sprite/texture rotation, scaling, line-stamping, and color calculation into Word RAM.
- **Audio Chips**:
  - CD-DA Redbook Audio (16-bit 44.1 kHz stereo audio directly from CD-ROM).
  - Ricoh RF5C164: 8-channel 8-bit PCM audio with independent stereo panning (16 levels) and loop flags.
- **Memory Expansion**:
  - 512 KB Sub-CPU Program RAM (`PRG-RAM`).
  - 256 KB Word RAM (can be configured in 2M mode or 1M ping-pong mode).
  - 64 KB PCM Wave RAM (stores 8-bit PCM sample data for the Ricoh chip).
  - 16 KB Backup RAM (save data).
  - 128 KB CD-ROM BIOS ROM.
- **CD-ROM Drive**: 1× speed (150 KB/s data transfer rate, ~400 ms average seek latency).

### C. Sega 32X (Sega Mars)
- **Primary CPUs**: Dual Hitachi SH-2 32-bit RISC processors @ 23.01 MHz (Master SH-2 and Slave SH-2).
  - 4 KB on-chip unified cache per SH-2.
  - 32-bit internal/external architecture.
- **Co-Processor**: Genesis 68000 @ 7.67 MHz handles controller polling, VBlank interrupts, and legacy sound.
- **32X VDP**:
  - Direct RGB rendering with double-buffered framebuffers: 2 banks of 128 KB Framebuffer VRAM.
  - Video Modes:
    - 8bpp Indexed Mode: 256 colors displayed simultaneously from a 256-entry CRAM (15-bit RGB palette).
    - 15bpp Direct Color Mode: 32,768 direct RGB colors (RGB555 format).
    - RLE Compressed Mode: Run-length compressed 8bpp bitmap decoding on the fly.
  - Overlay: The 32X video signal mixes directly over the Genesis VDP output.
- **Audio**: 32X Stereo PWM FIFO unit (12-bit D/A conversion, up to 22.05 kHz or 44.1 kHz).
- **Memory**:
  - 256 KB SDRAM shared between both SH-2s (`0x06000000..0x0603FFFF`).
  - 2 × 128 KB Framebuffer VRAM (`0x04000000`).
  - 512 bytes Palette CRAM (256 words at `0x20004200`).
  - Standard Cartridge ROM window: 4 MiB (`0x02000000..0x023FFFFF`).

---

## 2. Sega SSF Banked ROM Mapper (Up to 32 MiB Cartridges)

Standard Genesis/32X cartridge addressing provides a fixed 4 MiB ROM window.
To support large games, multi-level assets, and extensive audio banks, we employ the **Sega SSF Mapper**
(hardware mapper originally introduced in *Super Street Fighter II*):

### A. Mapper Registers & Apertures
The mapper divides the 4 MiB cartridge aperture into 8 slots of **512 KiB each**:
- Base registers on the 68000: `0xA130F1..0xA130FF` (slots 0..7). Writing a bank page byte (0..63) sets the 512 KiB page mapped into that slot.
- Max cartridge capacity: 64 pages × 512 KiB = **32 MiB (0x02000000 bytes)**.

| Slot | 68000 Address Range | SH-2 Address Range | Default Page | Usage Role |
|:---|:---|:---|:---|:---|
| **0** | `0x000000..0x07FFFF` | `0x02000000..0x0207FFFF` | Page 0 | Fixed Base ROM: Vectors, Genesis/Mars headers, boot code |
| **1** | `0x080000..0x0FFFFF` | `0x02080000..0x020FFFFF` | Page 1 | Fixed Base ROM: SH-2 startup, main engine code, core logic |
| **2** | `0x100000..0x17FFFF` | `0x02100000..0x0217FFFF` | Page 2 | Fixed Base ROM: Engine systems, static tables, font, core UI |
| **3** | `0x180000..0x1FFFFF` | `0x02180000..0x021FFFFF` | Page 3 | Fixed Base ROM: Common monster/actor animations & logic |
| **4** | `0x200000..0x27FFFF` | `0x02200000..0x0227FFFF` | Page 4 | Fixed Base ROM: Core audio tracks, VGM streams, SFX samples |
| **5** | `0x280000..0x2FFFFF` | `0x02280000..0x022FFFFF` | Page 5 | Fixed Base ROM: Primary environment / level geometry |
| **6** | `0x300000..0x37FFFF` | `0x02300000..0x0237FFFF` | Switchable | **Dynamic Aperture 1**: 512 KiB page for level-specific graphics |
| **7** | `0x380000..0x3FFFFF` | `0x02380000..0x023FFFFF` | Switchable | **Dynamic Aperture 2**: 512 KiB page for extended audio / animations |

### B. Emulator & Hardware Compatibility
- **PicoDrive**: Automatically activates SSF2 hardware mapping when the ROM image exceeds 4 MiB (`0x400000`), or when the cartridge header product string contains `"SEGA SSF"`.
- In 32X mode, PicoDrive's SH-2 ROM read router calculates:
  ```c
  bank_page = carthw_ssf2_banks[(sh2_addr >> 19) & 7];
  rom_byte  = rom_base[(bank_page << 19) | (sh2_addr & 0x7FFFF)];
  ```
- **Real Hardware / EverDrive**: Mega EverDrive Pro and standard SSF reproduction carts implement this exact register latching on `0xA130F1..0xA130FF`.

### C. 32X Cross-Core Banking Protocol
Because the SSF registers reside on the 68000 bus, the SH-2 commands the 68000 to perform bank switches:
1. SH-2 writes the target page (0..63) to `MARS_SYS_COMM2`.
2. SH-2 writes the bank switch command `(0x3000 | slot)` to `MARS_SYS_COMM0`.
3. 68000 resident ISR catches `COMM0 & 0xFF00 == 0x3000`, extracts `slot = COMM0 & 0x07`, and writes the page from `COMM2` to `0xA130F1 + slot * 2`.
4. 68000 clears `MARS_SYS_COMM0` to 0.
5. SH-2 waits until `MARS_SYS_COMM0 == 0`, then accesses the newly mapped aperture at `0x02300000` (slot 6) or `0x02380000` (slot 7).

---

## 3. Motorola 68000 Assembly Branch Range (`.w`) Constraint

When writing 68000 assembly (such as `md_start.s` or custom interrupt routines):
- **Short Branch (`.s`) Limitation**: Instructions like `bra.s`, `beq.s`, `bne.s`, `bcs.s` encode an 8-bit signed displacement (-128 to +127 bytes).
- If code grows or backward loops span beyond 128 bytes, the assembler fails with relocation out-of-range errors.
- **Rule**: In all resident service loops, backward branches, or routines spanning more than 20 instructions, always explicitly specify **word branches (`.w`)**:
  ```m68k
  sram_loop:
      /* Service routines, banking, audio stream updates */
      ...
      bra.w   sram_loop      /* Word displacement allows +/- 32768 bytes */
  ```

---

## 4. Sega 32X Dual SH-2 Communication & Synchronization

The Master and Slave SH-2s communicate primarily through the 8 hardware **COMM registers**:
- Master SH-2 address: `0x20004020..0x2000402E` (`COMM0..COMM7`, 16-bit words).
- Slave SH-2 address: Identical (`0x20004020..0x2000402E`).
- 68000 address: `0xA15120..0xA1512E`.

### Recommended Dual-Core Workload Split
- **Master SH-2**:
  - Handles game loop tick and deterministic entity updates.
  - Reads controller inputs passed by 68000.
  - Initiates 32X VDP framebuffer swap (`MARS_VDP_FS`) during VBlank.
  - Updates high-level audio playback state.
- **Slave SH-2**:
  - Offloaded rendering: 3D polygon rasterization, raycasting columns, voxel heightmap slices, or dirty-rect blitting.
  - Or dedicated audio mixing: Decoding streaming 4-bit IMA-ADPCM from ROM and feeding the stereo PWM FIFO at 22.05 kHz.

### Preventing Race Conditions in COMM Handshakes
- Never use simple polling without state transitions.
- Use an incrementing **sequence counter** in the high byte of command words (`seq << 8 | cmd`).
- The slave checks if `(reg >> 8) != last_seq`. When detected, it processes the request and mirrors the sequence counter back in a response register.
- This guarantees that repeated requests with identical payloads are never skipped.

### SDRAM Budget & Stack Isolation
Total SDRAM is **256 KiB** (`0x06000000..0x0603FFFF`):
```
0x06000000  .data section (initialized data copied from ROM)
            .bss section (zero-initialized static and global state)
            Heap (grows upward from __bss_end)
            ...
0x0603F800  Slave SH-2 Stack top (grows downward to 0x0603F000)
0x06040000  Master SH-2 Stack top (grows downward to 0x0603F800)
```
- **Critical Safety Guard**: Assert in build scripts (`verify_rom.py`) that `__bss_end < 0x0603E000`, leaving at least 6 KiB of safety headroom for runtime stack frames.

---

## 5. Sega CD / Mega-CD Architecture & Memory Model

### A. Sub-CPU & Communication Registers
The Genesis Main-CPU and Sega CD Sub-CPU communicate via the **Communication Registers** at `0xA12000` (Main-CPU) / `0xFF8000` (Sub-CPU):
- `COMM0..COMM15` (16 bytes / 8 words) for bidirectional message passing.
- Interrupt generation: Main-CPU can trigger Level 2 interrupts on the Sub-CPU; Sub-CPU can trigger Level 2 interrupts on the Main-CPU.

### B. Word RAM Modes
The 256 KB of Word RAM is the shared high-speed graphics canvas:
1. **2M Mode (Single 256 KB Bank)**:
   - All 256 KB is assigned to either the Main-CPU or the Sub-CPU.
   - Ideal for large data transfers, loading complete backgrounds or FMV frames.
2. **1M Mode (Two 128 KB Banks Ping-Pong)**:
   - Bank 0 and Bank 1 alternate between Main-CPU and Sub-CPU.
   - While the Sub-CPU and ASIC render a 3D frame or scaled sprite into Bank 0, the Main-CPU DMAs Bank 1 into Genesis VRAM for display.
   - At VBlank, the CPUs swap banks via register bit toggling.

### C. ASIC Graphics Hardware
The Sega CD contains dedicated hardware for affine transformations:
- **Operations**: Scaling (down to 1/256 or up to 256×), rotation (arbitrary angle 0..360°), stamping (tile/pattern repetition).
- **Execution**: Configured via a parameter table in Sub-CPU Program RAM. The ASIC reads source stamp data and writes transformed tiles directly into Word RAM.

---

## 6. Memory Coherency, Cache Architecture, & Data Types

### A. SH-2 Cache Architecture & Coherency
Each SH-2 core has a 4 KiB direct-mapped on-chip cache (16 bytes per cache line).
- **Cached vs Cache-Through Memory Spaces:**
  - `0x06000000..0x0603FFFF`: SDRAM **cached** access. Fast, but private to the core's cache.
  - `0x26000000..0x2603FFFF`: SDRAM **cache-through** (uncached). Bypasses cache entirely; writes are immediately visible to both cores and DMA.
  - `0x24000000..0x2403FFFF`: Framebuffer **uncached I/O** space. Two cores writing disjoint pixel regions need no cache synchronization.
- **Cache Synchronization Rules:**
  - When Master SH-2 writes a command packet, geometry buffer, or audio queue into cached SDRAM (`0x06xxxxxx`) that Slave SH-2 must read:
    1. Write via the cache-through mirror (`0x26xxxxxx`), OR
    2. Master flushes cache line (`Mars_ClearCacheLine`), and Slave invalidates its cache before reading.

### B. Integer Width Realities on 32X SH-2
On SH-2 GCC:
- `sizeof(short) == 2`
- `sizeof(int) == 4`
- `sizeof(long) == 4` (unlike 64-bit desktop x86_64 where `long` is 8 bytes!)
- `sizeof(long long) == 8`
- **Overflow Trap:** Computing distance-squared (`dx*dx + dy*dy`) in 16.16 fixed-point math will overflow a 32-bit `long` when distance exceeds ~181 units, silently wrapping to negative numbers. Always cast factors to `int64_t` / `long long` for distance-squared and high-precision accumulators.

---

## 7. Battery-Backed SRAM Saves: Robust Two-Phase Commit

Persisting player progress, high scores, or campaign data uses cartridge battery-backed SRAM (accessed via the 68000 save aperture at `0x200000`):
- **Bounded Fixed-Size Layout (≤2 KiB):**
  - Magic signature (`0x53415645` = `"SAVE"`).
  - Format version byte (prevents misreading incompatible older saves).
  - Compact state structs (player level, XP, inventory IDs, quest flags).
  - Checksum (CRC32 or additive word sum) over payload.
- **Two-Phase Commit Protocol:**
  - Maintain two save slots (Slot A and Slot B) with an active slot pointer.
  - Write new save data to the inactive slot.
  - Compute and write checksum for the new slot.
  - Verify written bytes match memory.
  - Atomically flip the active slot marker.
  - Prevents save corruption if power is cut during writing.
- **Defensive Boot Validation:**
  - Validate magic and checksum on boot before enabling "Continue" or "Load Game". If validation fails, fail safely to new game state.

---

## 8. COMM Slot Allocation Discipline

The 8 hardware `COMM` registers (`COMM0..COMM7` / `COMM0..COMM15`) are shared by boot firmware, the 68000 controller poller, telemetry, and slave dispatch. **Never overlap COMM slot assignments**:
- `COMM0` / `COMM2`: Controller inputs & 68000 telemetry.
- `COMM4`: Boot firmware handshake (`M_OK` / `S_OK`).
- `COMM6`: Slave core heartbeat / liveness watchdog.
- `COMM8`: Cartridge banking command / slot selection.
- `COMM10`: Slave job dispatch opcode (`seq << 8 | job_id`).
- `COMM12` / `COMM14`: Job parameters and return codes.
- Guard against collisions with a static grep check across codebases.


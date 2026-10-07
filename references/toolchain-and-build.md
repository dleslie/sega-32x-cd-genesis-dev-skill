# Toolchain, Build Pipeline, & Cross-Compilers Reference (Genesis, 32X, & Sega CD)

This reference covers the complete compiler toolchains, SDK requirements, build automation pipelines, linker scripts, and cross-platform installation routes across **Sega Genesis (Motorola 68000 / SGDK)**, **Sega 32X (Hitachi Dual SH-2 / MarsDev)**, and **Sega CD (Sub-CPU 68000)**.

---

## 1. SGDK Toolchain (Motorola 68000)

### A. Core Prerequisites
- **Cross Compiler:** `m68k-elf-gcc` (GCC 13.2 in SGDK 2.12) + GNU binutils.
- **SGDK Tree:** Environment variable **`GDK`** pointing to the SGDK root directory containing `makefile.gen`, `lib/libmd.a`, `inc/`, and `bin/`.
- **Java (JRE 8+):** Required for `rescomp.jar`, `xgmtool`, `xgm2tool`, and `sizebnd.jar`.
- Verification command:
  ```sh
  echo "$GDK" && ls "$GDK/makefile.gen" && m68k-elf-gcc --version && java -version
  ```

---

### B. Linux & CI Installation Routes

#### Route 0: Prebuilt Git-LFS Binary Artifacts (Recommended & Instant)
This repository stores prebuilt, hermetic GCC 14.2.0 cross-compilers for both `m68k-elf` and `sh-elf`, SGDK 2.x, and tools in `artifacts/` via Git LFS:
```sh
git lfs pull
bash assets/setup.sh    # Unpacks directly into $(PROJECT_ROOT)/opt in ~3 seconds
```
- Sets up `opt/m68k-elf`, `opt/sh-elf`, and `opt/bin`.
- Zero compiler build time, works offline, and requires no root/sudo privileges.

#### Route 1: Sega Tower of Power Docker Container
The repository includes a complete `Dockerfile` and runner script `assets/docker/run.sh` providing `m68k-elf-gcc`, `sh-elf-gcc`, SGDK, Java JRE, and mastering tools in a single container:
```sh
# Run any build inside the container:
./assets/docker/run.sh make -f assets/Makefile.tower

# Or run interactively:
docker run --rm -v "$PWD":/work -w /work sega-tower-dev:latest make -f assets/Makefile.tower
```

> [!IMPORTANT]
> **Toolchain Extraction Location:** All toolchains, SDKs, cross-compilers, and helper symlinks must be extracted or installed to **`$(PROJECT_ROOT)/opt/`** and never into the host system `/opt/`. This avoids requiring root/sudo privileges, keeps dependencies isolated, and ensures hermetic, sandbox-safe builds.

#### Route 2: MarsDev (Build from Source)
Builds `m68k-elf-gcc` and `sh-elf-gcc` from source into `$(PROJECT_ROOT)/opt/marsdev`:
```sh
mkdir -p opt/marsdev
git clone https://github.com/andwn/marsdev.git opt/marsdev
cd opt/marsdev && ./build.sh m68k && ./build.sh sh-elf
```

#### Route 3: Ubuntu/Debian Native APT Route (Fallback for Sandboxes)
When Docker is unavailable and building GCC from source is too slow:
```sh
apt-get install -y gcc-m68k-linux-gnu binutils-m68k-linux-gnu default-jre-headless make
```
**Critical Setup Steps for the APT Route:**
1. **Instruction Set Compatibility:** Pass `-m68000 -ffreestanding -nostdlib` so `m68k-linux-gnu-gcc` generates pure 68000 machine code.
2. **The 68020+ `libgcc` Trap:** Debian's default `-lgcc` contains 68020+ instructions (such as `bsr.l` in `__divsi3`) that trigger Illegal Instruction exceptions on the 68000. **Always link SGDK's shipped 68000-safe `$GDK/lib/libgcc.a` explicitly** instead of `-lgcc`.
3. **Tool Symlinks:** Create `$(PROJECT_ROOT)/opt/bin/m68k-elf-*` symlinks pointing to `m68k-linux-gnu-*`:
   ```sh
   mkdir -p opt/bin
   for t in gcc as ld objcopy nm ar ranlib objdump strip cpp; do
     src="$(command -v m68k-linux-gnu-$t || true)"
     [ -n "$src" ] && ln -sf "$src" opt/bin/m68k-elf-$t
   done
   export PATH=$PWD/opt/bin:$PATH
   ```

---

### C. ⚠️ The LTO-Version Trap (Why Games Freeze)

- **Symptom:** The ROM compiles and links cleanly, boots, and draws background tiles, but **sprites never move and game state appears completely frozen**; writes to RAM globals silently fail to persist while direct VDP register writes work.
- **Root Cause:** SGDK's prebuilt `lib/libmd.a` is compiled with `-flto -ffat-lto-objects`, embedding LTO bytecode tied to the exact GCC compiler build that produced it. If your compiler differs, the linker either fails with an LTO version mismatch or, if forced with `-fno-lto`, links object code where RAM initialization (`.data` copy / `.bss` zeroing) and DMA queue flushing fail.
- **The Permanent Fix: Rebuild `libmd.a` from Source With YOUR Compiler:**
  1. Build SGDK's native Z80 tools (`sjasm` and `bintos`) from C source:
     ```sh
     cd "$GDK/tools/sjasm/src" && make && cp sjasm "$GDK/bin/sjasm"
     cd "$GDK/tools/bintos" && gcc -O2 -o bintos src/*.c && cp bintos "$GDK/bin/bintos"
     ```
  2. Compile `libmd.a` natively:
     ```sh
     cd "$GDK" && make -f makelib.gen PREFIX=m68k-elf- release
     ```
  3. Rebuild your game project with full `-flto`.

---

### D. Manual SGDK Build Pipeline (Without `makefile.gen`)
When working in custom build systems, reproduce SGDK's exact sequence:
```sh
# 1. Compile assets with rescomp (emits assembly .s)
java -jar "$GDK/bin/rescomp.jar" res/resources.res build/resources.s
m68k-elf-gcc -m68000 -x assembler-with-cpp -c build/resources.s -o build/resources.o

# 2. Compile C sources
m68k-elf-gcc -m68000 -O3 -flto -Isrc -Iinc -I$GDK/inc -c src/main.c -o build/main.o

# 3. Compile cartridge header and convert to raw 256-byte binary
m68k-elf-gcc -m68000 -c src/rom_header.c -o build/rom_header.o
m68k-elf-objcopy -O binary build/rom_header.o build/rom_header.bin

# 4. Assemble Genesis startup code (sega.s incbin's rom_header.bin)
cp "$GDK/src/boot/sega.s" build/sega.s
m68k-elf-gcc -m68000 -x assembler-with-cpp -c build/sega.s -o build/sega.o

# 5. Link with md.ld, matching libmd.a, and 68000-safe libgcc.a
m68k-elf-gcc -m68000 -n -T "$GDK/md.ld" -nostdlib -Wl,--build-id=none \
  build/sega.o build/main.o build/resources.o \
  "$GDK/lib/libmd.a" "$GDK/lib/libgcc.a" -o build/rom.out -Wl,--gc-sections

# 6. Extract raw binary and patch checksum
m68k-elf-objcopy -O binary build/rom.out out/rom.bin
java -jar "$GDK/bin/sizebnd.jar" out/rom.bin -sizealign 131072 -checksum
```

---

## 2. Sega 32X Toolchain (Dual Hitachi SH-2 & Mars)

The 32X requires a **two-compiler setup**: `sh-elf-gcc` for the dual SH-2s and `m68k-elf-gcc` for the Genesis 68000 boot/co-processor program.

### A. Toolchain Setup
- **SH-2 Compiler:** `sh-elf-gcc` (Big-Endian Hitachi SH-2, architecture flags `-m2 -mb`).
- **68000 Compiler:** `m68k-elf-gcc` (Motorola 68000, architecture flags `-m68000`).
- Toolchains are extracted to `$(PROJECT_ROOT)/opt/toolchains/sega` (32XDK) or provisioned via MarsDev in `$(PROJECT_ROOT)/opt/marsdev` (`./build.sh sh-elf` and `./build.sh m68k`).

---

### B. Linker Scripts & Memory Apertures (`mars.ld`)

```ld
MEMORY
{
    rom (rx)   : ORIGIN = 0x02000000, LENGTH = 4M      /* Cartridge ROM window */
    sdram (rwx): ORIGIN = 0x06000000, LENGTH = 256K    /* Shared SH-2 SDRAM */
}

SECTIONS
{
    .text : {
        *(.header)           /* 32X Mars module header at 0x02000000 */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        __text_end = .;
    } > rom

    .data : AT(__text_end) {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > sdram

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > sdram
}
```

---

### C. The Two-Stage 32X Compilation Flow

```
   src/platform/32x/md_src/md_start.s
                   │
                   ▼  (m68k-elf-gcc)
           build/md_start.bin
                   │
                   ▼  (.incbin inside mars_start.s)
     src/platform/32x/mars_start.s  +  SH-2 C Sources
                   │
                   ▼  (sh-elf-gcc + mars.ld)
           build/game.elf
                   │
                   ▼  (sh-elf-objcopy)
           rom/game.32x
                   │
                   ▼  (python3 tools/romfix.py)
      ROM with valid checksum & Mars header
```

1. **68000 Stage:** `md_start.s` handles Genesis power-on, sets `ADEN` (Mars adapter enable), requests SH-2 reset release (`nRES`), and services 68000 interrupts. It is assembled into a raw binary `md_start.bin`.
2. **SH-2 Startup Stage:** `mars_start.s` embeds `md_start.bin` verbatim using `.incbin`. It sets up the SH-2 Master and Slave vector tables, initializes the stack pointers, copies `.data` to SDRAM, zeroes `.bss`, and branches Master SH-2 to `main()`.
3. **ROM Header Patching (`romfix.py`):**
   - Pads the binary to a valid power-of-two cartridge size.
   - Calculates the 16-bit big-endian checksum over `0x200..end` and writes it to offset `0x18E`.
   - Patches the cartridge end address at `0x1A4`.
   - Asserts Mars security vectors and entry points match ELF symbols.

---

## 3. Sega CD Toolchain & Disc Mastering Pipeline

Building for Sega CD requires generating an ISO-9660 filesystem image with CD-DA audio tracks:

### A. Toolchain Components
- `m68k-elf-gcc` builds two independent programs:
  1. `IP.BIN` (Initial Program): Boot code loaded by Main-CPU BIOS.
  2. `SP.BIN` (System Program): Sub-CPU firmware managing Word RAM, ASIC, and Ricoh PCM audio.
- Disc Mastering Tools:
  - `mkisofs`: Packages data tracks into ISO-9660 image.
  - `bchunk` / `chdman`: Packages ISO and WAV tracks into `.cue`/`.bin` or MAME CHD format.

### B. Master Layout
```
Track 01: data.iso (Mode 1 / 2048 bytes per sector, containing game assets and binaries)
Track 02: audio_01.wav (CD-DA Redbook Audio: 44.1 kHz, 16-bit stereo)
Track 03: audio_02.wav (CD-DA Redbook Audio)
...
```

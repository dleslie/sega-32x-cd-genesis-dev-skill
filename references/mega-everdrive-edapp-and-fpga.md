# Mega EverDrive EDAPP Plugins & Custom FPGA Mappers

This reference documents the **EDAPP** external application / file-association plugin system and custom **FPGA mapper architecture** available on Krikzz's **Mega EverDrive PRO** and **Mega EverDrive CORE** cartridges.

Reference SDK source: [krikzz/mega-ed-pub](https://github.com/krikzz/mega-ed-pub).

---

## 1. EDAPP File Association Architecture

Starting in firmware **v4.08**, the Mega EverDrive operating system supports binding arbitrary file extensions to external M68K applications.

When a user browses the SD card in the EverDrive OS and selects a file whose extension matches a subfolder in `MEGA/edapp/`, the OS automatically launches the associated `app.md` loader.

### A. Directory Structure on MicroSD

```
SD:/
└── MEGA/
    └── edapp/
        ├── mcv/                <-- Handler for .mcv (MegaColor Video) files
        │   ├── app.md          <-- 68000 binary executed by EverDrive OS
        │   ├── config.txt      <-- Loader configuration
        │   └── mapper.rbf      <-- (Optional) Custom FPGA mapper bitstream
        ├── nsf/                <-- Handler for NES Sound Format files
        │   ├── app.md
        │   └── config.txt
        └── vgm/                <-- Handler for raw VGM music logs
            ├── app.md
            └── config.txt
```

---

## 2. The `config.txt` Specification

Every application folder under `MEGA/edapp/<ext>/` must contain a `config.txt` declaring how the EverDrive OS passes target data to `app.md`.

```ini
INC_MODE=1
EXE_MODE=2
BRM_SIZE=0
```

### Directives Reference

| Directive | Values | Description |
|:---|:---|:---|
| `INC_MODE` | `0` | **Include Binary Data**: The OS loads `app.md` at `0x000000`, pads it, and appends the target file's raw binary data directly after `app.md` in cartridge ROM space. |
| | `1` | **Include Path String**: The OS loads `app.md` at `0x000000`, and passes only the null-terminated full path string to the target file immediately following `app.md` in ROM space. |
| `EXE_MODE` | `1` | Add application execution to the OS "Recently Played" history list. |
| | `2` | Do not record this application in the "Recently Played" list. |
| `BRM_SIZE` | `0` | Disable battery-backed backup RAM. |
| | `1..8` | Calculate backup memory size using: $\text{size} = 8192 \ll (\text{val} - 1)$ bytes. (e.g. `1` = 8 KB, `2` = 16 KB, `3` = 32 KB, `4` = 64 KB). |

---

## 3. Application Memory Layout & Binary Packaging

When `app.md` is compiled, it must be padded to a fixed alignment (typically **128 KiB** / `0x20000`) so that appended target data or paths land at a deterministic memory address:

### Linker & objcopy Alignment

In your project `Makefile`:

```makefile
# Pad app.md to exactly 128 KiB (0x20000)
$(OUTPUT).md: $(BUILD_DIR)/$(OUTPUT).elf
	m68k-elf-objcopy --pad-to 0x20000 -O binary $< $@
```

### Memory Map at Execution

```
M68K Address Space:
0x000000 .. 0x01FFFF : app.md executable code & constants (128 KiB)
0x020000 ..          : Target file payload (if INC_MODE=0)
                       OR null-terminated path string (if INC_MODE=1)
0xFF0000 .. 0xFFFFFF : Genesis Work RAM (64 KiB)
```

### Retrieving Target Information in C

```c
#include "everdrive.h"

#define TARGET_DATA_OFFSET  0x020000

int main(void) {
    ed_init();

#if INC_MODE == 0
    /* Target binary is already in ROM space */
    const uint8_t *file_data = (const uint8_t *)TARGET_DATA_OFFSET;
    process_data(file_data);
#else
    /* Target path string is passed at the offset */
    const char *target_path = (const char *)TARGET_DATA_OFFSET;
    
    /* Open and stream file via EDIO commands */
    ed_cmd_file_open((uint8_t *)target_path, FA_READ);
#endif

    /* Return cleanly to EverDrive OS */
    ed_cmd_reboot();
    return 0;
}
```

---

## 4. Custom FPGA Mappers

Both the Mega EverDrive PRO and CORE allow developers to implement custom FPGA logic for novel hardware mappers, specialized sound chips, or coprocessors.

### A. Hardware Platform Differences

| Platform | Target FPGA | Bitstream File Name |
|:---|:---|:---|
| **Mega EverDrive PRO** | Altera Cyclone IV (`EP4CE10` / `10CL025`) | `mega-pro.rbf` |
| **Mega EverDrive CORE** | Altera Cyclone 10 LP (`10CL016`) | `mega-core.x25` (renamed from `.rbf`) |

### B. Mapper Deployment

1. **Cartridge ROM Companion**: Place `mega-pro.rbf` or `mega-core.x25` in the same directory as your `.md` ROM on the SD card.
2. **EDAPP Companion**: Place `mapper.rbf` in `MEGA/edapp/<ext>/` alongside `app.md`.
3. **USB Live Testing**: Stream bitstreams directly to the FPGA over USB:
   ```bash
   python3 edlink.py run --file mygame.md --fpga path/to/mega-pro.rbf
   ```

---

## 5. FPGA Architecture & Shared Libraries (`mapper/`)

The Mega EverDrive SDK provides reusable SystemVerilog modules in `fpga/mapper/`:

```
fpga/mapper/
├── lib_base/           <-- Cartridge bus interface and system services
│   ├── defs.sv         <-- Bus constants and timing definitions
│   ├── base_io.sv      <-- M68K bus arbitration and register decoding
│   ├── dma.sv          <-- High-speed memory DMA controller
│   ├── audio_out.sv    <-- PWM DAC and audio mixer
│   ├── pi.sv           <-- Peripheral bus interface
│   └── structs.sv      <-- System data structures
├── lib_bram/           <-- Non-volatile backup RAM emulation
└── lib_mcd/            <-- Hardware Mega-CD simulation core
```

### A. Reference Mappers in the SDK

- **`/map_smd`**: Standard Genesis cartridge mapper with banked SRAM.
- **`/map_smd_cd`**: Standard Genesis cartridge mapper combined with the Mega-CD hardware core and **MD+** CD-DA audio streaming.
- **`/map_ssf`**: Super Street Fighter II 8× 512 KiB bank-switching mapper (supporting cartridges up to 32 MiB).
- **`/map_svp`**: Samsung SSP1601 DSP coprocessor implementation (Virtua Racing).
- **`/map_mcd`**: Complete standalone Mega-CD hardware simulation (Sub-68000, ASIC graphics, Word RAM).
- **`/SE`**: Simplified minimal mapper template without system layers, designed as a clean starting point for custom HDL coprocessors.

---

## 6. MD+ Enhanced Audio Support

The `map_smd_cd` and `map_ssf` mappers include built-in support for **MD+**, which allows standard Genesis cartridges to trigger CD-quality streaming audio tracks directly from the MicroSD card:

- Audio tracks are stored as standard WAV files alongside the ROM on the SD card.
- The cartridge FPGA hooks audio commands written by the Genesis 68000 and streams high-fidelity PCM audio into the console's audio mixer via cartridge audio pins.
- Avoids the processing overhead of software mixing on the Genesis 68000 or Z80.

---

## 7. Development & Synthesis Workflow

1. Open `fpga/fpga_pro/<mapper>/mega-pro.qpf` or `fpga/fpga_core/<mapper>/mega-core.qpf` in **Intel Quartus Prime Lite Edition**.
2. Modify or implement your custom address decoding in `map_<name>.sv`.
3. Run the Quartus compilation flow to produce `output_files/mega-pro.rbf`.
4. Test instantly on real hardware over USB:
   ```bash
   python3 edlink.py run --file test.md --fpga output_files/mega-pro.rbf
   ```

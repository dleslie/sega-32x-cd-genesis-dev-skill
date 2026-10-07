# Mega EverDrive Developer Reference & Hardware Guide

This reference provides a complete technical guide to developing software for and interfacing with Krikzz's **Mega EverDrive PRO** and **Mega EverDrive CORE** flash cartridges on Sega Genesis / Mega Drive hardware.

It covers the low-level **EDIO** cartridge register interface (`0xA130D0`), the MCU command and FIFO protocol, FAT MicroSD file system access, high-speed USB development with `edlink`, memory-mapped telemetry via the hardware mailbox, and integration into SGDK and custom C toolchains.

Reference SDK source: [krikzz/mega-ed-pub](https://github.com/krikzz/mega-ed-pub).

---

## 1. Hardware Models & Architecture

The Mega EverDrive PRO and CORE expand standard Genesis cartridge capabilities with onboard FPGAs, high-capacity PSRAM, high-speed USB interfaces, and microcontrollers:

| Specification | Mega EverDrive PRO (`DEVID = 0x18`) | Mega EverDrive CORE (`DEVID = 0x25`) |
|:---|:---|:---|
| **FPGA** | Altera Cyclone IV (high capacity) | Altera Cyclone 10 LP |
| **ROM Memory** | 16 MiB (2× 8 MiB PSRAM) | 16 MiB (2× 8 MiB PSRAM) |
| **Mega-CD Emulation** | Full hardware core (`map_mcd` / Sub-CPU / ASIC) | No (requires physical Mega-CD unit) |
| **MD+ Audio / CD-DA** | Supported (WAV streaming from SD) | Supported |
| **SSF Bank-Switching** | Supported (up to 32 MiB) | Supported |
| **SVP DSP (Virtua Racing)**| Supported (hardware SVP emulation core) | Supported |
| **USB Link Port** | Micro-USB (FTDI high-speed serial) | Micro-USB (FTDI high-speed serial) |
| **MicroSD Interface** | SDHC / SDXC (FAT32 / exFAT) | SDHC / SDXC (FAT32 / exFAT) |
| **Real-Time Clock (RTC)** | Onboard battery-backed RTC | Onboard battery-backed RTC |
| **32X Compatibility** | Pass-through in 32X cartridge slot | Pass-through in 32X cartridge slot |

---

## 2. Memory Map & Hardware Registers (`EDIO`)

The cartridge communicates with the Genesis Motorola 68000 via a 16-byte register block mapped at `0xA130D0..0xA130DF`.

### A. EDIO Register Map

```c
#include <stdint.h>

typedef volatile uint8_t  vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;

typedef struct {
    vu8  reserved0;
    vu8  FIFODATA;   /* 0xA130D1 (R/W): 8-bit bidirectional FIFO data port */
    vu16 FIFOSTAT;   /* 0xA130D2 (R):   FIFO status and byte counter */
    vu16 SYSSTAT;    /* 0xA130D4 (R/W): Command status and execution strobe */
    vu16 TIMER;      /* 0xA130D6 (R):   16-bit hardware timer (1 ms per tick) */
    vu8  reserved1;
    vu8  MEMDATA;    /* 0xA130D9 (R/W): Direct cartridge memory data port */
    vu8  reserved2;
    vu8  MEMADDR;    /* 0xA130DB (W):   Serial cartridge address setup */
    vu16 SSTCTRL;    /* 0xA130DC (W):   Save state and IGM controls */
    vu16 MBX;        /* 0xA130DE (R/W): 16-bit general-purpose Mailbox register */
} Edio;

#define EDIO_BASE    0xA130D0
#define EDIO         ((Edio *)EDIO_BASE)
```

### B. Bitfield Definitions & Status Flags

```c
/* FIFOSTAT (0xA130D2) flags */
#define FIFO_CPU_RXF    0x8000  /* Set when FIFO contains data CPU can read */
#define FIFO_ARM_RXF    0x4000  /* Set when cartridge MCU can read FIFO */
#define FIFO_RXF_MSK    0x07FF  /* Available byte count in FIFO (up to 2048) */

/* SYSSTAT (0xA130D4) status bits */
#define STATUS_CFG_OK   0x01    /* MCU completed configuration; system ready */
#define STATUS_CMD_OK   0x02    /* MCU completed command execution */
#define STATUS_FPG_OK   0x04    /* FPGA reconfiguration complete */
#define STATUS_STROBE   0x08    /* Toggled after each status read */
#define STATUS_REBOOT   0x10    /* Request reset to OS after CPU halt */

/* Device IDs returned by CMD_STATUS2 */
#define DEVID_MEGAPRO   0x18
#define DEVID_MEGACORE  0x25

/* Status handshake keys */
#define STATUS_KEY      0x5A
#define STATUS_KEY_OLD  0xA5
```

### C. Internal Cartridge FCI Bus Address Space

The cartridge FPGA bridges Genesis memory to internal FCI memory zones:

- `0x00000000`: Cartridge ROM memory (2× 8 MiB PSRAM).
- `0x00F00000`: IO Data Buffer (`DBUF`, 256 KB).
- `0x00F40000`: Save State Buffer (`SST_BF`, 256 KB).
- `0x00F80000`: Menu Program space (`MENU`, 256 KB).
- `0x01000000`: Fast 10ns SRAM.
- `0x01080000`: Battery-backed RAM (`BRAM`).
- `0x01800000`: System Configuration registers (`CFG`).
- `0x01800300`: Mailbox mirror (`ADDR_FCI_MBX`).
- `0x01810000`: Bidirectional FIFO buffer (2 KB).
- `0x01830000`: Mapper configuration registers.
- `0x01840000`: Mega-CD hardware registers.
- `0x01850000`: MD+ enhanced CD audio registers.

---

## 3. Communication Protocol & Command Reference

Commands are sent to the onboard microcontroller by framing a 4-byte command header through the FIFO:

### A. Command Framing

Each command packet begins with four bytes:
1. `'+'` (`0x2B`)
2. `'+' ^ 0xFF` (`0xD4`)
3. `cmd` (command code)
4. `cmd ^ 0xFF` (inverted command code)

```c
void ed_cmd_tx(uint8_t cmd) {
    uint8_t buf[4];
    buf[0] = '+';
    buf[1] = '+' ^ 0xFF;
    buf[2] = cmd;
    buf[3] = cmd ^ 0xFF;
    ed_fifo_wr(buf, 4);
}
```

### B. Standard Command Codes

| Command Code | Identifier | Description |
|:---|:---|:---|
| `0x10` | `CMD_STATUS` | Read legacy status word (`0xA500 | code`). |
| `0x14` | `CMD_RTC_GET` | Read Real-Time Clock registers into `RtcTime` struct. |
| `0x15` | `CMD_RTC_SET` | Write Real-Time Clock registers. |
| `0x19` | `CMD_MEM_RD` | Read block from cartridge memory to system RAM. |
| `0x1A` | `CMD_MEM_WR` | Write block from system RAM to cartridge memory. |
| `0x1B` | `CMD_MEM_SET` | Fill cartridge memory range with constant byte. |
| `0x22` | `CMD_USB_WR` | Write data to USB virtual COM port. |
| `0x23` | `CMD_FIFO_WR` | Push data into internal FIFO buffer. |
| `0x25` | `CMD_REINIT` | Reinitialize cartridge hardware and reboot. |
| `0x26` | `CMD_SYS_INF` | Query cartridge serial, firmware, and voltage telemetry. |
| `0x31` | `CMD_ROM_PATH`| Return null-terminated path of currently loaded ROM file. |
| `0x40` | `CMD_STATUS2` | Query modern 4-byte status (Protocol ID, Device ID, Status). |
| `0xC0` | `CMD_DISK_INIT`| Mount and initialize the MicroSD card filesystem. |
| `0xC5` | `CMD_F_DIR_LD` | Open directory on MicroSD (`DIR_OPT_SORTED`). |
| `0xC6` | `CMD_F_DIR_SIZE`| Get number of entries in loaded directory. |
| `0xC8` | `CMD_F_DIR_GET` | Fetch directory entry records into buffer. |
| `0xC9` | `CMD_F_FOPN` | Open file on MicroSD (`FA_READ`, `FA_WRITE`, etc.). |
| `0xCA` | `CMD_F_FRD` | Read file stream to Genesis system RAM. |
| `0xCB` | `CMD_F_FRD_MEM`| DMA stream file directly into cartridge PSRAM / ROM memory. |
| `0xCC` | `CMD_F_FWR` | Write file stream from Genesis system RAM. |
| `0xCD` | `CMD_F_FWR_MEM`| Write file stream directly from cartridge PSRAM. |
| `0xCE` | `CMD_F_FCLOSE` | Close open file handle. |
| `0xD0` | `CMD_F_FINFO` | Query file size, creation date, and attributes. |

---

## 4. The WRAM Trampoline & DMA Execution

When the cartridge MCU performs direct memory access (DMA) into PSRAM, flash operations, or FPGA re-mapping, the M68K CPU cannot safely access the cartridge bus.

The 68000 must copy a small halt trampoline to Genesis Work RAM (`0xFF0000..0xFFFFFF`) and execute it until the MCU signals completion via `SYSSTAT`.

```c
/* Trampoline copied into Genesis WRAM at startup */
void ed_halt_app(uint8_t stat_req) {
    vu16 stat;

    EDIO->SYSSTAT = stat_req;
    EDIO->FIFODATA = 0; /* Kick off execution */

    /* Spin in internal RAM until cartridge hardware finishes */
    while (1) {
        stat = EDIO->SYSSTAT;
        if ((stat & (0xFFF0 | STATUS_STROBE)) != (0x55A0 | STATUS_STROBE)) {
            continue;
        }
        stat = EDIO->SYSSTAT;
        if ((stat & (0xFFF0 | STATUS_STROBE)) != 0x55A0) {
            continue;
        }
        if ((stat & stat_req) != 0) {
            continue;
        }
        break;
    }

    /* Jump to reset vector if reboot was requested */
    if (stat_req & STATUS_REBOOT) {
        __asm__ __volatile__(
            "move.l 4, %%a0\n\t"
            "jmp (%%a0)"
            : : : "a0"
        );
    }
}
```

---

## 5. MicroSD File System Access (C API)

Games can read configuration files, custom campaign maps, tracker audio, or voice acting clips directly from the SD card at runtime.

### A. Initialization

```c
uint8_t ed_init(void) {
    uint8_t resp;

    /* Flush leftover FIFO bytes */
    ed_fifo_flush();

    /* Check if filesystem is ready by probing root directory */
    resp = ed_cmd_dir_load("/", 0);
    if (resp != 0) {
        resp = ed_cmd_disk_init();
        if (resp != 0) {
            return resp; /* Failed to initialize SD card */
        }
    }

    return 0;
}
```

### B. Reading Files to Genesis Work RAM

To read small assets or configuration files directly into 68K Work RAM:

```c
uint8_t ed_read_file_to_wram(const char *path, void *dst, uint32_t len) {
    uint8_t resp;
    uint32_t block;
    uint8_t *ptr = (uint8_t *)dst;

    resp = ed_cmd_file_open((uint8_t *)path, 0x01 /* FA_READ */);
    if (resp != 0) {
        return resp;
    }

    while (len > 0) {
        /* Read in blocks up to 512 bytes to prevent FIFO saturation */
        block = (len > 512) ? 512 : len;

        ed_cmd_tx(0xCA /* CMD_F_FRD */);
        ed_fifo_wr(&block, 4);

        ed_fifo_rd(&resp, 1);
        if (resp != 0) {
            ed_cmd_file_close();
            return resp;
        }

        ed_fifo_rd(ptr, block);

        len -= block;
        ptr += block;
    }

    return ed_cmd_file_close();
}
```

### C. Zero-CPU Streaming to Cartridge PSRAM (ROM Space)

Large assets (full audio tracks, uncompressed 3D level meshes, or prerendered animation frames) can be loaded directly from the SD card into cartridge PSRAM via hardware DMA. The 68000 does zero byte copying:

```c
uint8_t ed_stream_to_rom_memory(const char *path, uint32_t fci_rom_addr, uint32_t len) {
    uint8_t resp;

    resp = ed_cmd_file_open((uint8_t *)path, 0x01 /* FA_READ */);
    if (resp != 0) {
        return resp;
    }

    /* Instruct MCU to stream directly to PSRAM (e.g. ADDR_FCI_ROM + 0x200000) */
    ed_cmd_tx(0xCB /* CMD_F_FRD_MEM */);
    ed_fifo_wr(&fci_rom_addr, 4);
    ed_fifo_wr(&len, 4);

    /* Spin in WRAM trampoline while hardware copies data */
    ed_run_dma();

    resp = ed_check_status();
    ed_cmd_file_close();
    return resp;
}
```

---

## 6. Host USB Development & Debugging (`edlink`)

The onboard USB port allows instant flashing, bi-directional serial terminal debugging, hardware resets, and live memory inspection from the development PC without removing the SD card.

### A. Toolchain Setup (`edlink.py`)

`edlink.py` wraps `edlink.exe` and runs under Windows, Linux, and macOS:

```bash
# Linux / macOS prerequisite
sudo apt-get install mono-runtime

# Verify connection and display cartridge hardware status
python3 edlink.py
```

### B. Common CLI Operations

```bash
# 1. Flash and run a ROM instantly on real hardware
python3 edlink.py run --file out/rom.bin

# 2. Flash and run a ROM with a custom FPGA mapper bitstream
python3 edlink.py run --file out/rom.bin --fpga mapper/mega-pro.rbf

# 3. Trigger a soft console reset (equivalent to pressing Genesis reset button)
python3 edlink.py reset --mode soft

# 4. Trigger a hard cold reset (power cycle reset via cartridge bus pin B2)
python3 edlink.py reset --mode hard

# 5. Read incoming debug text sent from Genesis game
python3 edlink.py usbrd --print

# 6. Push a test packet or script into cartridge FIFO
python3 edlink.py fifowr --file payload.bin

# 7. Read 2 bytes from the Mailbox register live while the game runs
python3 edlink.py memrd --addr 0x01800300 --len 2 --print
```

---

## 7. Real-Time Debugging & Telemetry

### A. Bidirectional USB Serial Logging (`kprintf`)

Send non-blocking debug text over the USB connection during gameplay:

```c
void ed_usb_puts(const char *str) {
    uint16_t len = 0;
    const char *p = str;
    while (*p++) len++;

    if (len == 0) return;

    ed_cmd_tx(0x22 /* CMD_USB_WR */);
    ed_fifo_wr(&len, 2);
    ed_fifo_wr((void *)str, len);
}

/* Example formatted log wrapper */
void ed_log(const char *msg) {
    ed_usb_puts("[GAME] ");
    ed_usb_puts(msg);
    ed_usb_puts("\n");
}
```

Run `python3 edlink.py usbrd --print` on your host PC to view messages in real time.

### B. Hardware Mailbox Register (`EDIO->MBX`)

The Mailbox is a 16-bit register at `0xA130DE` on the 68K side and mirrored at `0x01800300` on the FCI bus. It allows atomic, lock-free communication between the Genesis and the PC:

- **Frame Rate & Performance Counter**: Write the current frame render time in microseconds or scanlines every VBlank.
- **Crash Diagnostic Probe**: Write state identifiers before dangerous hardware operations. If the Genesis crashes, run `edlink.py memrd --addr 0x01800300 --len 2 --print` to see the last state reached before death.

```c
/* Write heartbeat state from Genesis */
void report_state(uint16_t state_id) {
    EDIO->MBX = state_id;
}
```

---

## 8. SGDK Integration Example

A clean, non-intrusive pattern for integrating Mega EverDrive features into an SGDK project:

```c
#include <genesis.h>
#include "everdrive.h"

static bool s_everdrive_present = false;

void dev_io_init(void) {
    /* Probe EverDrive presence safely */
    uint16_t status = 0;
    ed_cmd_status(&status);

    if ((status & 0xFF00) == 0xA500) {
        s_everdrive_present = true;
        ed_init();
        ed_usb_puts("[INIT] Mega EverDrive initialized successfully\n");
    }
}

void dev_io_heartbeat(uint16_t vblank_count) {
    if (s_everdrive_present) {
        /* Update mailbox telemetry */
        EDIO->MBX = vblank_count;
    }
}
```

# Sega 32X Dual SH-2 IPC & Multiprocessing Architecture Demo

This directory contains the reference dual-core Hitachi SH-2 synchronization, atomic spinlock, and inter-processor communication (IPC) implementation for the Sega 32X.

---

## 1. Directory Structure

```
assets/32x/
├── README.md               # Architecture and testing guide
└── src/
    └── sh2/
        ├── mars_crt0.s     # Dual-core boot vectors, stack init, cache enable & handshake
        ├── mars.ld         # Linker script mapping ROM and SDRAM (0x06000000)
        ├── sh2_ipc.h       # Register defines, TAS.B spinlocks, barriers & queue types
        ├── sh2_ipc.c       # Phase barrier and ring-buffer FIFO implementations
        ├── sh2_test.h      # Test definitions, assertions and shared state struct
        ├── sh2_test.c      # 5-stage verification test runner (Master & Slave)
        ├── mars_master.c   # Master SH-2 orchestrator and 15bpp direct-color VDP renderer
        └── mars_slave.c    # Slave SH-2 worker and PWM audio generator
```

---

## 2. Key Synchronization Primitives

### A. Cache-Through Memory Aperture (`SH2_UNCACHED`)
Dual SH-2 CPUs contain private 4 KB L1 caches. Shared inter-core variables (spinlocks, barriers, queues) must be accessed via the **cache-through uncached aperture** (`0x26000000..0x2603FFFF` / `addr | 0x20000000`) so writes by one core are immediately coherent across both cores.

### B. Hardware Atomic Spinlock (`TAS.B`)
The Hitachi SH-2 hardware supports atomic `TAS.B @Rn` (Test and Set Byte) with bus locking:
```c
static inline bool sh2_tas(volatile uint8_t *lock_byte)
{
    uint32_t result;
    __asm__ volatile (
        "tas.b  @%1\n\t"
        "movt   %0"
        : "=r" (result)
        : "r" (lock_byte)
        : "t", "memory"
    );
    return result != 0;
}
```
Requires the `-mtas` flag with `sh-elf-gcc`.

### C. Ring-Buffer FIFO Message Queue
Circular queue (`Sh2Queue`) in uncached SDRAM supporting bidirectional job dispatches (`master_to_slave_q` and `slave_to_master_q`) with packet payload validation and checksum calculation (`sh2_packet_calc_checksum`).

---

## 3. Five-Stage Hardware Verification Test Suite

1. **Test 1: Spinlock Mutual Exclusion Under Contention**
   - Both Master and Slave concurrently increment a shared counter 5,000 times each (10,000 total).
   - Validates that no updates are lost due to race conditions.
2. **Test 2: Barrier Phase Synchronization**
   - Both cores step through 4 successive barrier phases in lockstep.
3. **Test 3: Hardware COMM Register Semaphore Ping-Pong**
   - Sequence-stamped request/response handshake across `MARS_COMM4` and `MARS_COMM5`.
4. **Test 4: Ring-Buffer FIFO Message Queues**
   - Master dispatches 8 3D vector transformation requests. Slave dequeues, executes 3D vector math, calculates checksum, and enqueues responses.
5. **Test 5: Hardware PWM Audio FIFO Status & Burst**
   - Verifies PWM control register empty/full flag status and sample pushes to `MARS_PWM_MONO_REG`.

---

## 4. Building & Running

To build the standalone 32X test cartridge image:
```bash
make -f assets/Makefile.32x
```

To run build verification:
```bash
make -f assets/Makefile.32x check
```

Artifacts produced:
- `build_32x/test_32x.elf`: Linked SH-2 ELF binary.
- `out_32x/test_32x.32x`: Padded 8 KB 32X cartridge ROM ready for emulation or hardware flashcarts.

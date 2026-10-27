| ==============================================================================
| Sega CD Sub-CPU 68000 Firmware Payload Container
| Embeds out_cd/sub.bin into Genesis Main-CPU binary for turnkey staging
|
| Copyright (c) 2026 Dan Leslie
| SPDX-License-Identifier: MIT
| ==============================================================================

    .section .rodata
    .global sub_bin_start, sub_bin_end, sub_bin_size
    .even
sub_bin_start:
    .incbin "out_cd/sub.bin"
sub_bin_end:
    .even
sub_bin_size:
    .long sub_bin_end - sub_bin_start

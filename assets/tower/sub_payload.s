| ==============================================================================
| Sega "Tower of Power" Sub-CPU Firmware Payload
| Embeds out/tower_game_sub.bin into the Genesis Main-CPU binary
|
| Copyright (c) 2026 Dan Leslie
| SPDX-License-Identifier: MIT
| ==============================================================================

    .section .rodata
    .global tower_sub_bin_start, tower_sub_bin_end, tower_sub_bin_size
    .even
tower_sub_bin_start:
    .incbin "out/tower_game_sub.bin"
tower_sub_bin_end:
    .even
tower_sub_bin_size:
    .long tower_sub_bin_end - tower_sub_bin_start

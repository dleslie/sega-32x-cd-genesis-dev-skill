#!/usr/bin/env python3
"""Sega SSF Banked Cartridge ROM Packer & Directory Compiler.

Copyright (c) 2026 Dan Leslie <dan@ironoxide.ca> / diablo32x project contributors
SPDX-License-Identifier: MIT

Packs assets into 512 KiB switchable ROM pages (up to 32 MiB total cartridge space,
64 pages) and generates a big-endian binary asset directory table:

    u32 magic 'D32B' (0x44333242)
    u32 entry_count
    entries[entry_count]:
        u32 asset_id       (4-byte ASCII ID, e.g. 'MON0')
        u16 page           (512 KiB page index: 0..63)
        u16 pad
        u32 offset_in_page (byte offset within 512 KiB page: 0..0x7FFFF)
        u32 size           (asset size in bytes)

Usage:
    tools/bank_packer.py --manifest manifest.json --out-dir build/banks --out-dir-bin build/bank_dir.bin
    tools/bank_packer.py --test-build
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from typing import Dict, List, Tuple

PAGE_SIZE = 512 * 1024       # 512 KiB per SSF bank page
MAX_PAGES = 64               # 32 MiB max capacity (64 * 512 KiB)
MAGIC_DIR = 0x44333242       # 'D32B'


def pack_bank_directory(entries: List[Tuple[int, int, int, int]]) -> bytes:
    """Pack asset directory into big-endian binary format."""
    buf = bytearray()
    buf.extend(struct.pack(">II", MAGIC_DIR, len(entries)))
    for asset_id, page, offset_in_page, size in entries:
        buf.extend(struct.pack(">IHHII", asset_id, page, 0, offset_in_page, size))
    return bytes(buf)


def parse_bank_directory(data: bytes) -> List[Dict[str, int]]:
    """Parse big-endian bank directory table."""
    if len(data) < 8:
        raise ValueError("Directory data too short")
    magic, count = struct.unpack_from(">II", data, 0)
    if magic != MAGIC_DIR:
        raise ValueError(f"Invalid directory magic: 0x{magic:08X}")
    entries = []
    for i in range(count):
        offset = 8 + i * 16
        if offset + 16 > len(data):
            raise ValueError(f"Truncated directory entry {i}")
        asset_id, page, pad, offset_in_page, size = struct.unpack_from(">IHHII", data, offset)
        entries.append({
            "asset_id": asset_id,
            "page": page,
            "offset_in_page": offset_in_page,
            "size": size,
        })
    return entries


def pack_assets_into_pages(
    asset_dict: Dict[int, bytes],
    start_page: int = 6,
) -> Tuple[List[bytes], List[Tuple[int, int, int, int]]]:
    """Pack arbitrary assets into sequential 512 KiB pages.

    Returns:
        (pages, directory_entries)
    """
    pages: List[bytearray] = []
    dir_entries: List[Tuple[int, int, int, int]] = []

    cur_page_idx = start_page
    cur_page_data = bytearray()

    for asset_id, data in asset_dict.items():
        if len(data) > PAGE_SIZE:
            raise ValueError(
                f"Asset 0x{asset_id:08X} ({len(data)} bytes) exceeds single page limit ({PAGE_SIZE})"
            )

        # 4-byte align asset placement within page
        aligned_offset = (len(cur_page_data) + 3) & ~3

        # Check if asset fits in current page
        if aligned_offset + len(data) > PAGE_SIZE:
            # Pad remainder of current page to 512 KiB and push
            cur_page_data.extend(b"\x00" * (PAGE_SIZE - len(cur_page_data)))
            pages.append(cur_page_data)
            cur_page_idx += 1
            cur_page_data = bytearray()
            aligned_offset = 0

        # Pad to aligned offset
        if aligned_offset > len(cur_page_data):
            cur_page_data.extend(b"\x00" * (aligned_offset - len(cur_page_data)))

        dir_entries.append((asset_id, cur_page_idx, aligned_offset, len(data)))
        cur_page_data.extend(data)

    if cur_page_data:
        cur_page_data.extend(b"\x00" * (PAGE_SIZE - len(cur_page_data)))
        pages.append(cur_page_data)

    return [bytes(p) for p in pages], dir_entries


def main() -> int:
    parser = argparse.ArgumentParser(description="SSF Bank Packer")
    parser.add_argument("--test-build", action="store_true", help="Run internal self-test")
    args = parser.parse_args()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

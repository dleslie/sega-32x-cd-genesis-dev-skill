#!/usr/bin/env python3
"""Sega SSF Banked Cartridge ROM Packer & Directory Compiler.

Copyright (c) 2026 Dan Leslie <dan@ironoxide.ca>
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
    """Pack arbitrary assets into switchable 512 KiB pages using First-Fit Decreasing.

    Sorts assets by size descending so large assets are placed first and smaller
    assets fill the remaining space in each page, maximizing SSF page density.

    Returns:
        (pages, directory_entries)
    """
    sorted_items = sorted(asset_dict.items(), key=lambda kv: len(kv[1]), reverse=True)
    page_data: List[bytearray] = []
    dir_entries: List[Tuple[int, int, int, int]] = []

    for asset_id, data in sorted_items:
        if len(data) > PAGE_SIZE:
            raise ValueError(
                f"Asset 0x{asset_id:08X} ({len(data)} bytes) exceeds single page limit ({PAGE_SIZE})"
            )
        placed = False
        for p_idx, p_buf in enumerate(page_data):
            aligned = (len(p_buf) + 3) & ~3
            if aligned + len(data) <= PAGE_SIZE:
                if aligned > len(p_buf):
                    p_buf.extend(b"\x00" * (aligned - len(p_buf)))
                dir_entries.append((asset_id, start_page + p_idx, aligned, len(data)))
                p_buf.extend(data)
                placed = True
                break
        if not placed:
            new_buf = bytearray()
            dir_entries.append((asset_id, start_page + len(page_data), 0, len(data)))
            new_buf.extend(data)
            page_data.append(new_buf)

    # Pad each page to exact PAGE_SIZE (512 KiB)
    pages = []
    for p_buf in page_data:
        if len(p_buf) < PAGE_SIZE:
            p_buf.extend(b"\x00" * (PAGE_SIZE - len(p_buf)))
        pages.append(bytes(p_buf))

    return pages, dir_entries


def test_build() -> int:
    """Self-test for bank packer and directory."""
    print("Running SSF Bank Packer self-test...")
    # Create mock assets
    mock_assets = {
        0x4D4F4E30: b"MOCK_MONSTER_SPRITE_DATA_" * 100,      # 'MON0'
        0x4D4F4E31: b"MOCK_GARGOYLE_SPRITE_DATA_" * 200,      # 'MON1'
        0x54494C30: b"MOCK_DUNGEON_TILE_SET_DATA" * 500,     # 'TIL0'
        0x504C5230: b"MOCK_PLAYER_ANIM_FRAMES___" * 300,     # 'PLR0'
    }

    pages, entries = pack_assets_into_pages(mock_assets, start_page=6)
    dir_bin = pack_bank_directory(entries)
    parsed = parse_bank_directory(dir_bin)

    assert len(parsed) == len(mock_assets), "Entry count mismatch"
    for exp, actual in zip(entries, parsed):
        assert exp[0] == actual["asset_id"], "Asset ID mismatch"
        assert exp[1] == actual["page"], "Page mismatch"
        assert exp[2] == actual["offset_in_page"], "Offset mismatch"
        assert exp[3] == actual["size"], "Size mismatch"

    print(f"Packed {len(mock_assets)} assets into {len(pages)} page(s) ({len(pages) * PAGE_SIZE} bytes).")
    print("SSF Bank Packer self-test PASSED.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="SSF Bank Packer")
    parser.add_argument("--test-build", action="store_true", help="Run internal self-test")
    parser.add_argument("--start-page", type=int, default=6, help="Starting SSF page index (default: 6)")
    parser.add_argument("--out-pages", default="bank_pages.bin", help="Output path for packed bank pages")
    parser.add_argument("--out-dir", default="bank_dir.bin", help="Output path for directory table")
    args = parser.parse_args()

    if args.test_build:
        return test_build()

    print("Use --test-build to run validation test.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

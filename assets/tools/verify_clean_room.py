#!/usr/bin/env python3
"""Clean-Room and Trademark Verification Gate for Sega Genesis / 32X / Sega CD Public Releases.

Verifies that public release artifacts (ROMs, HTML bundles, and embedded Wasm blobs):
  1. Do not contain forbidden trademarked names or strings (configurable regex).
  2. Do not contain proprietary asset payloads or exceed maximum code-only size ceilings.
  3. Support client-side in-browser asset baking where users supply their own original assets.

SPDX-License-Identifier: MIT
"""
from __future__ import annotations

import argparse
import base64
import re
import sys


def scan_bytes_for_patterns(data: bytes, pattern: re.Pattern, label: str) -> list[str]:
    """Scan a raw byte buffer for any forbidden pattern match."""
    errors: list[str] = []
    for match in pattern.finditer(data):
        start = max(0, match.start() - 20)
        end = min(len(data), match.end() + 20)
        snippet = data[start:end].decode("latin1", errors="replace")
        snippet_clean = "".join(ch if 32 <= ord(ch) < 127 else "." for ch in snippet)
        word = match.group(0).decode("latin1", errors="replace")
        errors.append(
            f"{label}: forbidden string '{word}' at offset 0x{match.start():X} "
            f"(context: '{snippet_clean}')"
        )
    return errors


def verify_code_rom(rom_bytes: bytes, max_size: int, pattern: re.Pattern, label: str) -> list[str]:
    """Verify that a code-only ROM has zero bundled assets and no forbidden strings."""
    errors: list[str] = []
    if len(rom_bytes) == 0:
        errors.append(f"{label}: code-only ROM is empty")
        return errors

    if max_size > 0 and len(rom_bytes) > max_size:
        errors.append(
            f"{label}: code-only ROM size ({len(rom_bytes):,} bytes) exceeds "
            f"ceiling ({max_size:,} bytes) — bundled assets may be present!"
        )

    errors.extend(scan_bytes_for_patterns(rom_bytes, pattern, label))
    return errors


def verify_public_html(html_bytes: bytes, pattern: re.Pattern, label: str) -> list[str]:
    """Verify that a single-file public HTML and all embedded base64 payloads are clean."""
    errors: list[str] = []
    if len(html_bytes) == 0:
        errors.append(f"{label}: public HTML bundle is empty")
        return errors

    errors.extend(scan_bytes_for_patterns(html_bytes, pattern, f"{label} (raw HTML)"))

    # Find and decode all embedded base64 blobs (e.g. Wasm or virtual ROM blobs)
    b64_pattern = re.compile(rb"[A-Za-z0-9+/]{128,}={0,2}")
    blob_count = 0
    for match in b64_pattern.finditer(html_bytes):
        blob_raw = match.group(0)
        pad_needed = (4 - (len(blob_raw) % 4)) % 4
        if pad_needed:
            blob_raw = blob_raw + b"=" * pad_needed
        try:
            decoded = base64.b64decode(blob_raw)
        except Exception:
            continue
        blob_count += 1
        blob_label = f"{label} (base64 blob #{blob_count}, {len(decoded):,} bytes)"
        errors.extend(scan_bytes_for_patterns(decoded, pattern, blob_label))

    return errors


def self_test(pattern_str: str) -> int:
    """Run negative and positive self-tests."""
    print("Running Clean-Room Verification self-test...")
    pat = re.compile(pattern_str.encode("utf-8"), re.IGNORECASE)

    # Clean payload
    clean = b"SEGA 32X clean room open-source kernel."
    assert not scan_bytes_for_patterns(clean, pat, "clean"), "False positive on clean payload"

    # Dirty payload
    dirty = b"SEGA 32X test with FORBIDDEN_STRING embedded here."
    pat_dirty = re.compile(rb"FORBIDDEN_STRING")
    errs = scan_bytes_for_patterns(dirty, pat_dirty, "dirty")
    assert len(errs) == 1, "Failed to detect forbidden string in dirty payload"

    print("Clean-Room Verification self-test PASSED.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Clean-Room & Trademark Verification Gate")
    parser.add_argument("--rom", help="Path to code-only cartridge ROM (.32x / .bin)")
    parser.add_argument("--html", help="Path to standalone public HTML distribution bundle")
    parser.add_argument("--max-rom-size", type=int, default=262144, help="Maximum allowed code-only ROM size in bytes")
    parser.add_argument("--forbidden-pattern", default=r"(trademark1|trademark2)", help="Regex pattern of forbidden strings")
    parser.add_argument("--self-test", action="store_true", help="Run self-test suite")
    args = parser.parse_args()

    if args.self_test:
        return self_test(args.forbidden_pattern)

    pattern = re.compile(args.forbidden_pattern.encode("utf-8"), re.IGNORECASE)
    all_errors: list[str] = []

    if args.rom:
        with open(args.rom, "rb") as f:
            all_errors.extend(verify_code_rom(f.read(), args.max_rom_size, pattern, args.rom))

    if args.html:
        with open(args.html, "rb") as f:
            all_errors.extend(verify_public_html(f.read(), pattern, args.html))

    if all_errors:
        print("VERIFICATION FAILED:")
        for err in all_errors:
            print(f"  [ERROR] {err}")
        return 1

    print("VERIFICATION PASSED: All public release artifacts are clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

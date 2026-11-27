#!/usr/bin/env python3
"""Assert a Genesis frame dump is NOT a black screen.

Copyright (c) 2026 Haroldo de Oliveira Pinheiro <haroldoop@gmail.com>
SPDX-License-Identifier: MIT

Reads PPM (P6) or BMP snapshots produced by ares-harness and checks:
  * lit:       a healthy fraction of pixels are non-black
  * colourful: more than a few distinct colours (catches palette-never-loaded,
               where every pixel maps to index 0 = black)
  * advanced:  the last frame differs from the first (the game moved on)
  * no crash:  neither frame is SGDK's exception screen (ADDRESS ERROR, BUS
               ERROR, ILLEGAL INSTRUCTION, ... register dump)

Usage: check_frames.py FIRST.{ppm,bmp} LAST.{ppm,bmp}
Exit non-zero (with a reason) on failure so CI fails loudly.
"""
import sys


def read_frame(path):
    """Read an RGB image in binary P6 PPM or uncompressed 24-bit BMP format."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] == b"BM":
        off = int.from_bytes(data[10:14], "little")
        w = int.from_bytes(data[18:22], "little", signed=True)
        h_raw = int.from_bytes(data[22:26], "little", signed=True)
        bpp = int.from_bytes(data[28:30], "little")
        if w <= 0 or h_raw == 0 or bpp != 24:
            sys.exit(f"FAIL: {path} unsupported BMP ({w}x{h_raw}, {bpp}bpp)")
        h = abs(h_raw)
        rows = range(h) if h_raw < 0 else range(h - 1, -1, -1)
        rowbytes = (w * 3 + 3) & ~3
        px = bytearray(w * h * 3)
        for yi, y in enumerate(rows):
            src = off + y * rowbytes
            for x in range(w):
                b, g, r = data[src + x * 3:src + x * 3 + 3]
                o = (yi * w + x) * 3
                px[o], px[o + 1], px[o + 2] = r, g, b
        return w, h, bytes(px)
    if not data.startswith(b"P6"):
        sys.exit(f"FAIL: {path} is neither a P6 PPM nor a 24-bit BMP")
    # parse header: P6 <w> <h> <maxval> then raw RGB bytes
    idx, fields = 2, []
    while len(fields) < 3:
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if idx < len(data) and data[idx:idx + 1] == b"#":     # comment line
            while idx < len(data) and data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n":
            idx += 1
        fields.append(int(data[start:idx]))
    idx += 1  # single whitespace after maxval
    w, h, _maxval = fields
    return w, h, data[idx:idx + w * h * 3]


def stats(px):
    n = len(px) // 3
    nonblack, colours = 0, set()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        if r > 8 or g > 8 or b > 8:
            nonblack += 1
        colours.add((r >> 3, g >> 3, b >> 3))   # quantize to Genesis-ish depth
    return n, nonblack, len(colours)


def crash_screen(px):
    """True if the frame looks like SGDK 2.x's default exception screen.

    Measured from a real address-error ROM under Genesis Plus GX: exactly four
    colours — black background (~78%), cyan register labels (168,236,232)
    (~18%), white values, and a yellow title line (232,204,0) (~1%). Keying on
    the cyan + yellow pair (not on "mostly black, few colours") avoids false
    positives on ordinary text screens, which are also mostly black.
    """
    n = len(px) // 3
    black = cyan = yellow = 0
    colours = set()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        colours.add((r, g, b))
        if r < 16 and g < 16 and b < 16:
            black += 1
        elif 140 <= r <= 196 and g >= 216 and b >= 208:
            cyan += 1
        elif r >= 200 and 180 <= g <= 228 and b <= 48:
            yellow += 1
    return (len(colours) <= 8 and black >= 0.5 * n and
            cyan >= 0.03 * n and yellow >= 0.002 * n)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: check_frames.py FIRST.{ppm,bmp} LAST.{ppm,bmp}")
    first, last = sys.argv[1], sys.argv[2]
    w0, h0, p0 = read_frame(first)
    w1, h1, p1 = read_frame(last)

    for name, px in ((first, p0), (last, p1)):
        if crash_screen(px):
            sys.exit(f"FAIL: {name} is SGDK's exception screen (crash). Read its "
                     "'Offset:' line (the faulting PC) and look it up: "
                     "m68k-elf-nm -n out/release/rom.out, or m68k-elf-addr2line -f -e "
                     "out/release/rom.out 0x<offset> on a debug build")

    n, nonblack, colours = stats(p1)
    lit_ratio = nonblack / n if n else 0.0
    print(f"last frame {w1}x{h1}: lit={lit_ratio:.2%} colours={colours}")

    if lit_ratio < 0.02:
        sys.exit(f"FAIL: last frame is essentially black (lit={lit_ratio:.2%})")
    if colours < 4:
        sys.exit(f"FAIL: too few colours ({colours}) — palette likely not loaded")
    if (w0, h0) == (w1, h1) and p0 == p1:
        sys.exit("FAIL: last frame identical to first — game did not advance")

    print("PASS: frame is lit, colourful, advanced, and not a crash screen")


if __name__ == "__main__":
    main()

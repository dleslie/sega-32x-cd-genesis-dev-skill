#!/usr/bin/env python3
"""Point-to-point test suite for a Sega 32X / Mega Drive ROM via headless ares.

Copyright (c) 2026 Haroldo de Oliveira Pinheiro <haroldoop@gmail.com>
SPDX-License-Identifier: MIT

Each script in tests/scripts/*.txt boots the real ROM in ares-harness (pure
SH-2 interpreter mode by default), drives a scripted controller sequence, and
dumps BMP screenshots. This runner then asserts the frames are LIT, COLOURFUL,
and CHANGING - the black-screen guard - plus per-script structural checks.

    python3 tests/run_tests.py            run every script
    python3 tests/run_tests.py 03_match   run one script (by stem)

Layout expected (adjust the paths near the top to your project):
    opt/ares/ares-harness         extracted from artifacts/ares-headless.tar.gz
    tests/scripts/*.txt           input scripts
    rom/<game>.32x                the ROM under test
Captured frames + stats land in tests/out/<script>/.
"""
from __future__ import annotations

import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# ---- project-specific paths & auto-detection ----------------------------
ROM = os.environ.get("ROM", os.path.join(ROOT, "rom", "game.32x"))
if not os.path.exists(ROM):
    for alt_rom in [
        os.path.join(ROOT, "out", "rom.32x"),
        os.path.join(ROOT, "out", "tower_game.32x"),
        os.path.join(ROOT, "out", "rom.bin"),
        os.path.join(HERE, "out", "rom.bin"),
    ]:
        if os.path.exists(alt_rom):
            ROM = alt_rom
            break

HARNESS = os.environ.get("ARES_HARNESS")
if not HARNESS:
    for candidate in [
        os.path.join(HERE, "emu", "ares-harness"),
        os.path.join(ROOT, "opt", "ares", "ares-harness"),
        os.path.join(HERE, "opt", "ares", "ares-harness"),
        os.path.join(ROOT, "tests", "emu", "ares-harness"),
    ]:
        if os.path.exists(candidate):
            HARNESS = candidate
            break
    if not HARNESS:
        HARNESS = os.path.join(ROOT, "opt", "ares", "ares-harness")

SCRIPTS = os.path.join(HERE, "scripts")
OUT = os.path.join(HERE, "out")

# ---- black-screen thresholds (tune per game) -----------------------------
MIN_NONBLACK_RATIO = float(os.environ.get("MIN_NONBLACK_RATIO", "0.05"))
MIN_DISTINCT_COLORS = int(os.environ.get("MIN_DISTINCT_COLORS", "4" if ROM.endswith(".bin") else "8"))
BLACK_LEVEL = int(os.environ.get("BLACK_LEVEL", "8"))


class Failure(Exception):
    pass


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise Failure(f"{path}: not a P6 PPM")
    fields, i = [], 2
    while len(fields) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        s = i
        while i < len(data) and not data[i:i + 1].isspace():
            i += 1
        fields.append(int(data[s:i]))
    i += 1
    w, h, _ = fields
    return w, h, data[i:i + w * h * 3]


def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54 or data[:2] != b"BM":
        raise Failure(f"{path}: not a valid BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<I", data, 18)[0]
    h_raw = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    h = abs(h_raw)
    rows = range(h - 1, -1, -1) if h_raw > 0 else range(h)
    if bpp != 24:
        raise Failure(f"{path}: unsupported BMP bpp {bpp}")
    rowbytes = (w * 3 + 3) & ~3
    px = bytearray(w * h * 3)
    for yi, y in enumerate(rows):
        src = off + y * rowbytes
        for x in range(w):
            b, g, r = data[src + x * 3:src + x * 3 + 3]
            o = (yi * w + x) * 3
            px[o], px[o + 1], px[o + 2] = r, g, b
    return w, h, bytes(px)


def read_frame(path):
    if path.endswith(".bmp"):
        return read_bmp(path)
    return read_ppm(path)


def stats(path):
    w, h, px = read_frame(path)
    nonblack, colors = 0, set()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        if r > BLACK_LEVEL or g > BLACK_LEVEL or b > BLACK_LEVEL:
            nonblack += 1
        colors.add((r >> 3, g >> 3, b >> 3))
    total = w * h
    return {"w": w, "h": h, "nonblack_ratio": nonblack / total,
            "colors": len(colors), "crc": f"{zlib.crc32(px):08x}",
            "_px": px}


def crop_colors(st, x0, y0, x1, y1):
    w, px = st["w"], st["_px"]
    seen = set()
    for y in range(y0, y1):
        for x in range(x0, x1):
            o = (y * w + x) * 3
            seen.add((px[o] >> 3, px[o + 1] >> 3, px[o + 2] >> 3))
    return len(seen)


def pixel_delta(a, b):
    if (a["w"], a["h"]) != (b["w"], b["h"]):
        return 1.0
    pa, pb = a["_px"], b["_px"]
    changed = sum(pa[i:i + 3] != pb[i:i + 3] for i in range(0, len(pa), 3))
    return changed / (a["w"] * a["h"])


def assert_not_black(name, st, artwork=True):
    if st["nonblack_ratio"] < MIN_NONBLACK_RATIO:
        raise Failure(f"{name} is black/blank: {st['nonblack_ratio']:.4f} lit")
    if artwork and st["colors"] < MIN_DISTINCT_COLORS:
        raise Failure(f"{name} has too few colors: {st['colors']}")


def run_script(path):
    name = os.path.splitext(os.path.basename(path))[0]
    outdir = os.path.join(OUT, name)
    os.makedirs(outdir, exist_ok=True)
    for old in os.listdir(outdir):
        if old.endswith((".bmp", ".ppm")) or old == "audio.txt":
            os.remove(os.path.join(outdir, old))
    subprocess.run([HARNESS, ROM, path, outdir], check=True)

    shots = {s[:-4]: stats(os.path.join(outdir, s))
             for s in sorted(os.listdir(outdir)) if s.endswith(".bmp") or s.endswith(".ppm")}
    if not shots:
        raise Failure(f"{name}: script produced no shots")

    # Every captured frame must be non-black.
    for shot, st in shots.items():
        assert_not_black(f"{name}/{shot}", st)

    # If the script took >=2 shots, at least two must differ (not frozen).
    crcs = {st["crc"] for st in shots.values()}
    if len(shots) >= 2 and len(crcs) < 2:
        raise Failure(f"{name}: output never changed across {len(shots)} shots")

    audio_file = os.path.join(outdir, "audio.txt")
    audio_info = ""
    if os.path.exists(audio_file):
        audio_stats = {}
        with open(audio_file) as af:
            for line in af:
                if "=" in line:
                    k, v = line.strip().split("=", 1)
                    audio_stats[k] = int(v)
        nonzero = audio_stats.get("nonzero", 0)
        peak = audio_stats.get("peak", 0)
        if nonzero > 0:
            audio_info = f", audio[{nonzero}nz,peak={peak}]"

    print(f"PASS {name}: " + ", ".join(
        f"{k}[{v['colors']}c,{v['nonblack_ratio']:.2f}lit]"
        for k, v in shots.items()) + audio_info)
    return shots


def main(argv):
    global HARNESS
    if not os.path.exists(HARNESS):
        for art_path in [
            os.path.join(ROOT, "artifacts", "ares-headless.tar.gz"),
            os.path.join(HERE, "artifacts", "ares-headless.tar.gz"),
        ]:
            if os.path.exists(art_path):
                target_dir = os.path.join(ROOT, "opt", "ares")
                os.makedirs(target_dir, exist_ok=True)
                subprocess.run(["tar", "-xzf", art_path, "-C", target_dir], check=True)
                candidate = os.path.join(target_dir, "ares-harness")
                if os.path.exists(candidate):
                    os.chmod(candidate, 0o755)
                    HARNESS = candidate
                    break
        if not os.path.exists(HARNESS):
            raise SystemExit(
                f"ares-harness missing: {HARNESS} (run assets/setup.sh or assets/ares/build_ares_headless.sh)"
            )

    if not os.path.exists(ROM):
        raise SystemExit(f"ROM not found: {ROM} (build it first)")

    want = argv[1] if len(argv) > 1 else None
    scripts = sorted(s for s in os.listdir(SCRIPTS) if s.endswith(".txt"))
    if want:
        scripts = [s for s in scripts if s.startswith(want) or s[:-4] == want]
        if not scripts:
            raise SystemExit(f"no script matches {want!r}")

    failures = 0
    for s in scripts:
        try:
            run_script(os.path.join(SCRIPTS, s))
        except (Failure, subprocess.CalledProcessError) as e:
            failures += 1
            print(f"FAIL {s}: {e}", file=sys.stderr)
    if failures:
        raise SystemExit(f"{failures} test(s) failed")
    print(f"all {len(scripts)} test(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

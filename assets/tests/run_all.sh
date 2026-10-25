#!/usr/bin/env bash
# Run both test tiers; exit non-zero on any failure. Wire this into CI.
#
# Copyright (c) 2026 Haroldo de Oliveira Pinheiro <haroldoop@gmail.com>
# SPDX-License-Identifier: MIT
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== Tier 1: host-core oracle =="
bash "$ROOT/tests/run_host_tests.sh"

echo "== Tier 2: headless emulator (Genesis Plus GX) =="
bash "$ROOT/tests/run_headless_emulator_test.sh"

PROJECT_ROOT="$(cd "$ROOT/.." && pwd)"
if [[ -f "$PROJECT_ROOT/rom/game.32x" || -f "$PROJECT_ROOT/out/rom.32x" || -f "$ROOT/out/rom.32x" || -n "${ROM:-}" ]]; then
  echo "== Tier 2: headless emulator (PicoDrive 32X) =="
  python3 "$ROOT/run_tests.py"
fi

echo "ALL TESTS PASSED"

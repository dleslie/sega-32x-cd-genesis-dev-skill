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

echo "== Tier 2: headless emulator =="
bash "$ROOT/tests/run_headless_emulator_test.sh"

echo "ALL TESTS PASSED"

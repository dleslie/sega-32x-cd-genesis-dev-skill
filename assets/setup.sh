#!/usr/bin/env bash
# Idempotent Sega Tower of Power toolchain setup for Linux / CI.
# Supports Genesis / Mega Drive (SGDK), Sega 32X (SH-2), Sega CD, Mega EverDrive.
#
# Copyright (c) 2026 Stéphane Dallongeville, Haroldo de Oliveira Pinheiro, Dan Leslie
# SPDX-License-Identifier: MIT
#
# Safe to re-run: each step is guarded by a capability check.
#
# Routes (pick with TOWER_ROUTE or SGDK_ROUTE=artifacts|docker|marsdev|apt):
#   artifacts (default if artifacts/ exists) — unpack prebuilt GCC 14.2.0 (m68k + sh-elf + SGDK)
#                                               directly from Git-LFS into $(PROJECT_ROOT)/opt.
#   docker                                  — build/run via sega-tower-dev container image.
#   marsdev                                 — build m68k-elf-gcc + sh-elf-gcc + SGDK from source.
#   apt                                     — Ubuntu/Debian native toolchain (Genesis only fallback).
#
# After running, opt/ contains m68k-elf, sh-elf, and helper tools.
set -euo pipefail

PROJECT_ROOT="${PROJECT_ROOT:-$(pwd)}"
OPT_DIR="$PROJECT_ROOT/opt"

# Auto-select artifacts route if artifacts/ directory exists and contains toolchains
if [ -z "${TOWER_ROUTE:-${SGDK_ROUTE:-}}" ]; then
  if [ -f "$PROJECT_ROOT/artifacts/m68k-elf-toolchain.tar.gz" ]; then
    ROUTE="artifacts"
  else
    ROUTE="docker"
  fi
else
  ROUTE="${TOWER_ROUTE:-${SGDK_ROUTE}}"
fi

have() { command -v "$1" >/dev/null 2>&1; }

echo ">> Sega Tower of Power setup route: $ROUTE"

# Java is required for SGDK rescomp/xgmtool on the host when running native tools
if [ "$ROUTE" != "docker" ] && ! have java; then
  echo ">> Installing a JRE (needed by SGDK rescomp)…"
  if have apt-get; then sudo apt-get update && sudo apt-get install -y default-jre-headless
  elif have dnf;   then sudo dnf install -y java-latest-openjdk-headless
  else echo "!! Install a Java JRE 8+ manually."; fi
fi

case "$ROUTE" in
  artifacts|lfs)
    echo ">> Unpacking prebuilt Tower of Power toolchains from artifacts/ into $OPT_DIR..."
    mkdir -p "$OPT_DIR"

    if [ ! -f "$PROJECT_ROOT/artifacts/m68k-elf-toolchain.tar.gz" ]; then
      echo "!! Toolchain archive artifacts/m68k-elf-toolchain.tar.gz not found."
      echo "   Run 'git lfs pull' to fetch binary artifacts."
      exit 1
    fi

    tar -xzf "$PROJECT_ROOT/artifacts/m68k-elf-toolchain.tar.gz" -C "$OPT_DIR"
    tar -xzf "$PROJECT_ROOT/artifacts/sh-elf-toolchain.tar.gz" -C "$OPT_DIR"
    if [ -f "$PROJECT_ROOT/artifacts/marsdev-tools.tar.gz" ]; then
      tar -xzf "$PROJECT_ROOT/artifacts/marsdev-tools.tar.gz" -C "$OPT_DIR"
    fi

    echo ">> Toolchains successfully unpacked into $OPT_DIR!"
    echo ">> Add to your environment:"
    echo "   export MARSDEV=$OPT_DIR"
    echo "   export GDK=$OPT_DIR/m68k-elf"
    echo "   export GENDEV=$OPT_DIR"
    echo "   export PATH=\$OPT_DIR/bin:\$OPT_DIR/m68k-elf/bin:\$OPT_DIR/sh-elf/bin:\$PATH"
    ;;

  docker)
    if ! have docker; then
      echo "!! Docker not found. Install Docker, or use SGDK_ROUTE=artifacts."; exit 1
    fi
    IMAGE_NAME="${TOWER_IMAGE:-sega-tower-dev:latest}"
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
      echo ">> Building Docker image $IMAGE_NAME..."
      docker build -t "$IMAGE_NAME" -f "$PROJECT_ROOT/Dockerfile" "$PROJECT_ROOT"
    fi
    cat <<EOF

>> Docker route ready. Build your project with:
     ./assets/docker/run.sh make -f assets/Makefile.tower
   or:
     docker run --rm -v "\$PWD":/work -w /work $IMAGE_NAME make

EOF
    ;;

  marsdev)
    : "${MARSDEV:=$OPT_DIR/marsdev}"
    export MARSDEV
    if [ -f "$MARSDEV/m68k-elf/bin/m68k-elf-gcc" ] && [ -f "$MARSDEV/sh-elf/bin/sh-elf-gcc" ]; then
      echo ">> MarsDev already present at $MARSDEV."
    else
      echo ">> Building MarsDev into $MARSDEV (compiling GCC m68k + sh2)..."
      if have apt-get; then
        sudo apt-get update
        sudo apt-get install -y build-essential git wget texinfo \
          libgmp-dev libmpfr-dev libmpc-dev default-jre-headless zip unzip
      fi
      mkdir -p "$OPT_DIR"
      [ -d "$MARSDEV/.git" ] || git clone --depth 1 https://github.com/andwn/marsdev.git "$MARSDEV"
      make -C "$MARSDEV" m68k-toolchain-newlib
      make -C "$MARSDEV" sh-toolchain-newlib
      make -C "$MARSDEV" sgdk
    fi
    echo ">> Add to your environment: export MARSDEV=$MARSDEV; export GDK=\$MARSDEV/m68k-elf; export PATH=\$MARSDEV/m68k-elf/bin:\$MARSDEV/sh-elf/bin:\$PATH"
    ;;

  apt)
    if ! have apt-get; then
      echo "!! apt route needs apt-get (Debian/Ubuntu)."; exit 1
    fi
    OPT_BIN="$OPT_DIR/bin"
    echo ">> Installing gcc-m68k-linux-gnu + binutils + JRE…"
    sudo apt-get update
    sudo apt-get install -y gcc-m68k-linux-gnu binutils-m68k-linux-gnu \
      default-jre-headless make
    mkdir -p "$OPT_BIN"
    for t in gcc as ld objcopy nm ar ranlib objdump strip cpp; do
      src="$(command -v m68k-linux-gnu-$t || true)"
      [ -n "$src" ] && ln -sf "$src" "$OPT_BIN/m68k-elf-$t"
    done
    export PATH="$OPT_BIN:$PATH"
    ;;

  *) echo "!! Unknown route '$ROUTE' (use artifacts|docker|marsdev|apt)"; exit 1 ;;
esac

echo ">> Setup complete."

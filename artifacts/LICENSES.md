# Artifacts License & Third-Party Attribution Reference

This directory contains prebuilt toolchains, emulation cores, and binary utilities tracked via **Git LFS** to enable hermetic, zero-dependency development and testing across Sega Genesis, Sega CD, and Sega 32X targets.

Every archive bundled in this directory includes a copy of its corresponding `LICENSE` file. Below is the comprehensive copyright attribution, origin repository, and licensing breakdown for each package.

---

## 1. Summary of Bundled Artifacts

| Archive | Component(s) | Primary License | Upstream Author / Project | Upstream Repository |
| :--- | :--- | :--- | :--- | :--- |
| **`m68k-elf-toolchain.tar.gz`** | GCC 14.2.0, GNU Binutils, Newlib, SGDK 2.x | GPLv3 w/ Runtime Exception, MIT, BSD | Free Software Foundation, Stéphane Dallongeville, Red Hat | [gcc.gnu.org](https://gcc.gnu.org), [SGDK](https://github.com/Stephane-D/SGDK) |
| **`sh-elf-toolchain.tar.gz`** | GCC 14.2.0, GNU Binutils, Newlib, 32XDK | GPLv3 w/ Runtime Exception, MIT, BSD | Free Software Foundation, Joseph Groff, Victor Luchits | [32XDK](https://github.com/viciious/32XDK) |
| **`marsdev-tools.tar.gz`** | MarsDev, Sik's MD Tools, rescomp, xgm2tool | MIT License | Andrew Andrianov, Gabriel R. ("Sik"), Stéphane Dallongeville | [marsdev](https://github.com/andwn/marsdev), [mdtools](https://github.com/Sik/mdtools) |
| **`ares-headless.tar.gz`** | Headless `ares` MD/32X/MCD CLI test harness (`ares-harness`) | ISC License | Near, ares Contributors | [ares](https://github.com/ares-emulator/ares) |
| **`picodrive-wasm.tar.gz`** | PicoDrive WebAssembly static library (`.a`) & headers | Non-Commercial / MAME / GPL | Dave, notaz, Stéphane Dallongeville, Libretro | [picodrive](https://github.com/libretro/picodrive) |
| **`cd-mastering-tools.tar.gz`** | `chdman` (MAME), `genisoimage` (cdrkit) | BSD-3-Clause, GNU GPLv2 | MAMEDev and contributors, Jörg Schilling, cdrkit team | [mame](https://github.com/mamedev/mame), [cdrkit](https://repo.or.cz/cdrkit.git) |
| **`edlink-tools.tar.gz`** | `edlink.py`, `edlink.exe` | Hardware Dev Utility (Author Reserved) | Igor Golubovskiy ("Krikzz") | [mega-ed-pub](https://github.com/krikzz/mega-ed-pub) |

---

## 2. Detailed Component Attribution & Terms

### A. Motorola 68000 Cross-Compiler & SGDK (`m68k-elf-toolchain.tar.gz`)
- **GNU Compiler Collection (GCC) & GNU Binutils (`m68k-elf-gcc`, `as`, `ld`, etc.)**:
  - Copyright (C) 1988-2026 Free Software Foundation, Inc.
  - License: **GNU General Public License Version 3 with the GCC Runtime Library Exception (GPL-3.0-with-GCC-exception)**.
  - Grants unrestricted permission to compile and distribute target binaries (such as Sega Genesis `.bin` ROMs) without subjecting the target ROM to the copyleft provisions of the GPL.
- **SGDK (Sega Genesis Development Kit) 2.x**:
  - Copyright (C) Stéphane Dallongeville `<stephane-d@numericable.fr>`.
  - License: **MIT License**.
  - Components included: `libmd.a` C runtime library, hardware headers, vector tables, standard math and graphics routines.
- **Newlib C Runtime**:
  - Copyright (C) Red Hat Inc., Cygnus Solutions, and contributors.
  - License: **Permissive BSD / MIT style terms**.

### B. Hitachi SH-2 Cross-Compiler & 32XDK (`sh-elf-toolchain.tar.gz`)
- **GNU Compiler Collection (GCC) & GNU Binutils (`sh-elf-gcc`, `as`, `ld`, etc.)**:
  - Copyright (C) 1988-2026 Free Software Foundation, Inc.
  - License: **GNU GPLv3 with GCC Runtime Library Exception**.
- **32XDK (Chilly Willy's 32X Devkit)**:
  - Copyright (C) Joseph Groff ("Chilly Willy") and Victor Luchits ("Vic" / `viciious`).
  - License: **MIT License**.
  - Note: DOOM 32X: Resurrection components derived from Jaguar DOOM (subject to id Software source license) are explicitly excluded. Only clean MIT build and linker infrastructure (`mars.ld`, startup vectors) are included.
- **Newlib C Runtime**:
  - Copyright (C) Red Hat Inc., Cygnus Solutions, and contributors.
  - License: **Permissive BSD / MIT style terms**.

### C. MarsDev Tools & Sik's MD Tools (`marsdev-tools.tar.gz`)
- **MarsDev Build System**:
  - Copyright (C) 2020-2026 Andrew Andrianov ("andwn").
  - License: **MIT License**.
- **Sik's Mega Drive Tools**:
  - Copyright (C) 2017-2026 Gabriel R. ("Sik The hedgehog").
  - License: **MIT License**.
  - Included binaries: `mdtiler`, `midi2esf`, `pcm2ewf`, `vgi2eif`, `echo2vgm`, `uftc`, `slz`, `eif2tfi`, `romfix`, `tfi2eif`, `mml2esf`, `headgen`.
- **SGDK Resource Compiler (`rescomp`) & Audio Drivers**:
  - Copyright (C) Stéphane Dallongeville `<stephane-d@numericable.fr>`.
  - License: **MIT License**.

### D. Headless `ares` Verification Binary (`ares-headless.tar.gz`)
- **Upstream Project (`ares`, MD / Mega 32X / Mega CD cores + `mia` built-in firmware)**:
  - Copyright (C) 2004-2026 Near, ares Contributors.
  - Upstream Commit: `6e67c8bd4f0cd14e299047386d381d06ea5478c3` (`https://github.com/ares-emulator/ares`).
  - License: **ISC License** (permissive; includes built-in clean `sh2.boot.mrom`, `sh2.boot.srom`, and `vector.rom`).
  - Defaults to pure SH-2 interpreter mode (`"Recompiler" = false`) via `assets/ares/ares_harness.cpp` for accurate SH7604 cache, `RV` bus, CRAM, and SSF hardware verification.

### E. PicoDrive WebAssembly Runtime (`picodrive-wasm.tar.gz`)
- **Core Developers & Contributors**:
  - Copyright (C) 2004 Dave / FinalDave
  - Copyright (C) 2006-2015 notaz
  - Copyright (C) 2015-2026 The Libretro Team
- **Subsystems & CPU Emulators**:
  - Portions Copyright Nicola Salmoria and the MAME Team
  - FAME / C68k / CZ80: Copyright Stéphane Dallongeville
  - Cyclone 68000: Copyright fDave, Reesy, notaz
  - dr_libs: Copyright David Reid (Public Domain / MIT-0)
- **License Terms (Non-Commercial)**:
  - Redistribution and use in source and binary forms are permitted under the **PicoDrive / MAME Non-Commercial License**:
    - Redistributions may not be sold or utilized in commercial activities.
    - Full source code must accompany binary redistributions.
    - Copyright and disclaimer notices must be preserved.

### F. CD Mastering Tools (`cd-mastering-tools.tar.gz`)
- **`chdman` (MAME Compressed Hunks of Data Manager)**:
  - Copyright (C) 1997-2026 MAMEDev and contributors.
  - License: **BSD 3-Clause License**.
- **`genisoimage` & `mkisofs` (cdrkit)**:
  - Copyright (C) 1995-2006 Jörg Schilling.
  - Copyright (C) 2006-2011 The cdrkit team.
  - License: **GNU General Public License Version 2 (GPL-2.0)**.

### G. Mega EverDrive USB Link Utility (`edlink-tools.tar.gz`)
- **Host Tools (`edlink.py`, `edlink.exe`)**:
  - Copyright (C) 2020-2026 Igor Golubovskiy ("Krikzz").
  - Source: [krikzz/mega-ed-pub](https://github.com/krikzz/mega-ed-pub).
  - Provided by author for homebrew development, USB communication, and live hardware debugging with Mega EverDrive PRO and CORE flash cartridges.

---

## 3. Commercial Use Notice

> [!WARNING]
> While the main repository codebase is licensed under the permissive **MIT License** and `ares-headless.tar.gz` is licensed under the **ISC License**, the WebAssembly runtime artifact (`picodrive-wasm.tar.gz`) is strictly governed by the **PicoDrive / MAME Non-Commercial License**.
>
> It is provided exclusively for personal WebAssembly deployment and homebrew emulation, and **may not** be sold, bundled into commercial applications, or used for commercial activities.

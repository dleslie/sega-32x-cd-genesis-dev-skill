// SPDX-License-Identifier: ISC
/*
 * ares_harness_mem.hpp - Internal bridge isolating <md/md.hpp> from <mia/mia.hpp>.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <ares/ares.hpp>

/** Forward MegaDrive core option ("Recompiler", "TMSS") without re-including mmi.hpp. */
bool md_set_option(const char *name, const char *value);

/** Instantiate a MegaDrive/MegaCD/Mega32X system tree via ares::MegaDrive::load. */
bool md_load_system(ares::Node::System &root, const char *name);

/** Return true when the Genesis VDP active line width is H40 (320 pixels). */
bool md_vdp_is_h40();

/** Return true when the loaded system has Mega 32X hardware active. */
bool md_is_32x();

/** Copy the 8 shared 32X COMM registers (COMM0..COMM14) into host uint16_t[8]. */
void md_read_comm(uint16_t out[8]);

/** Dump 64 KiB Motorola 68000 work RAM in big-endian byte order. */
bool md_dump_m68k_ram(const char *path);

/** Write <len> bytes from 32X SDRAM starting at <off> in big-endian SH-2 byte order. */
bool md_write_sdram_slice(std::FILE *f, uint32_t off, uint32_t len);

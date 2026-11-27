// SPDX-License-Identifier: ISC
/*
 * ares_harness_mem.cpp - Direct MegaDrive / 32X memory & VDP state bridge.
 * Kept separate from ares_harness.cpp so <mia/mia.hpp> and <md/md.hpp>
 * never double-include nall headers lacking include guards.
 */

#include <cstdint>
#include <cstdio>
#include <vector>

#include <md/md.hpp>
#undef register

#include "ares_harness_mem.hpp"

/** Forward MegaDrive core option ("Recompiler", "TMSS"). */
bool md_set_option(const char *name, const char *value) {
  return ares::MegaDrive::option(string{name}, string{value});
}

/** Instantiate a MegaDrive/MegaCD/Mega32X system tree via ares::MegaDrive::load. */
bool md_load_system(ares::Node::System &root, const char *name) {
  return ares::MegaDrive::load(root, string{name});
}

/** Return true when the Genesis VDP active line width is H40 (320 pixels). */
bool md_vdp_is_h40() {
  return ares::MegaDrive::vdp.h40();
}

/** Return true when the loaded system has Mega 32X hardware active. */
bool md_is_32x() {
  return ares::MegaDrive::Mega32X();
}

/** Copy the 8 shared 32X COMM registers (COMM0..COMM14) into host uint16_t[8]. */
void md_read_comm(uint16_t out[8]) {
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<uint16_t>(ares::MegaDrive::m32x.communication[i]);
  }
}

/** Dump 64 KiB Motorola 68000 work RAM in big-endian byte order. */
bool md_dump_m68k_ram(const char *path) {
  std::FILE *f = std::fopen(path, "wb");
  if (!f) {
    std::perror(path);
    return false;
  }
  uint32_t words = ares::MegaDrive::cpu.ram.size();
  std::vector<uint8_t> buf(static_cast<size_t>(words) * 2u);
  for (uint32_t i = 0; i < words; ++i) {
    n16 w = ares::MegaDrive::cpu.ram[i];
    // ares stores 16-bit big-endian bus words as n16 (upper=byte(1), lower=byte(0)).
    buf[i * 2u + 0u] = static_cast<uint8_t>(w.byte(1));
    buf[i * 2u + 1u] = static_cast<uint8_t>(w.byte(0));
  }
  std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  return true;
}

/** Write <len> bytes from 32X SDRAM starting at <off> in big-endian SH-2 byte order. */
bool md_write_sdram_slice(std::FILE *f, uint32_t off, uint32_t len) {
  if (!ares::MegaDrive::Mega32X()) {
    std::fprintf(stderr, "write_sdram_slice: active system is not Mega 32X\n");
    return false;
  }
  const uint32_t sdram_bytes = ares::MegaDrive::m32x.sdram.size() * 2u;
  if (off > sdram_bytes || len > sdram_bytes - off) {
    std::fprintf(stderr, "write_sdram_slice: out of range off=0x%x len=%u (total=%u)\n",
                 off, len, sdram_bytes);
    return false;
  }
  std::vector<uint8_t> buf(len);
  for (uint32_t i = 0; i < len; ++i) {
    uint32_t addr = off + i;
    n16 w = ares::MegaDrive::m32x.sdram[addr >> 1];
    // Big-endian SH-2 order: even offset holds high byte (byte(1)), odd holds low byte (byte(0)).
    buf[i] = static_cast<uint8_t>((addr & 1u) ? w.byte(0) : w.byte(1));
  }
  if (std::fwrite(buf.data(), 1, len, f) != len) {
    std::perror("fwrite sdram");
    return false;
  }
  return true;
}

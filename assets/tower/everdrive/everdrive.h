/**
 * @file everdrive.h
 * @brief Mega EverDrive PRO / X7 EDIO Driver & USB Debug Interface.
 *
 * Provides MicroSD card streaming, USB edlink host communications,
 * and hardware save state management for the Tower of Power.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _EVERDRIVE_H_
#define _EVERDRIVE_H_

#if defined(SGDK_GCC)
#include <genesis.h>
#else
#include <stdint.h>
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* EDIO Register Offsets (Cartridge Port aperture 0xA130D0) */
#define EVD_REG_CFG           (*(volatile uint16_t *)0xA130D0)
#define EVD_REG_STATUS        (*(volatile uint16_t *)0xA130D0)
#define EVD_REG_CMD           (*(volatile uint16_t *)0xA130D2)
#define EVD_REG_DATA          (*(volatile uint16_t *)0xA130D4)
#define EVD_REG_USB_DATA      (*(volatile uint16_t *)0xA130D6)
#define EVD_REG_SPI_DATA      (*(volatile uint16_t *)0xA130D8)

#define EVD_STAT_SPI_BUSY     (1 << 0)
#define EVD_STAT_FIFO_RXF     (1 << 1)
#define EVD_STAT_FIFO_TXE     (1 << 2)

/**
 * @brief Initialize EverDrive EDIO registers and test communication.
 * @return true if EverDrive unlocked successfully.
 */
bool evd_init(void);

/**
 * @brief Send character over USB link to host computer (edlink).
 */
void evd_put_char(char c);

/**
 * @brief Send null-terminated string over USB link to host computer.
 */
void evd_puts(const char *str);

/**
 * @brief Stream 512-byte sectors from MicroSD card directly into RAM buffer.
 * @param sector_lba Logical Block Address (LBA) on MicroSD.
 * @param dst_buffer Destination memory address (WRAM, Word RAM, or SDRAM).
 * @param sector_count Number of 512-byte sectors to read.
 * @return true on success.
 */
bool evd_read_sectors(uint32_t sector_lba, void *dst_buffer, uint16_t sector_count);

#ifdef __cplusplus
}
#endif

#endif /* _EVERDRIVE_H_ */

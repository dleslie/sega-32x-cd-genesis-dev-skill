/**
 * @file tower.h
 * @brief Sega "Tower of Power" Architecture Definitions & Bootstrapping Interface.
 *
 * Integrates:
 *   - Genesis / Mega Drive: Motorola 68000 @ 7.67 MHz (Main-CPU / SGDK)
 *   - Sega 32X (Mars): Dual Hitachi SH-2 @ 23 MHz (Master & Slave)
 *   - Sega CD / Mega-CD: Motorola 68000 @ 12.5 MHz (Sub-CPU) + ASIC + RF5C164 PCM
 *   - Mega EverDrive: Cartridge expansion, MicroSD streaming, USB edlink logging
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _TOWER_H_
#define _TOWER_H_

#if defined(SGDK_GCC)
#include <genesis.h>
#else
#include <stdint.h>
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Hardware Detection Flags                                                   */
/* ========================================================================== */

#define TOWER_FLAG_MD         (1 << 0)  /**< Mega Drive / Genesis Base (always 1) */
#define TOWER_FLAG_32X        (1 << 1)  /**< Sega 32X (Hitachi Dual SH-2)         */
#define TOWER_FLAG_CD         (1 << 2)  /**< Sega CD / Mega-CD (Sub-CPU + ASIC)   */
#define TOWER_FLAG_EVERDRIVE  (1 << 3)  /**< Mega EverDrive (EDIO Registers)      */

/* ========================================================================== */
/* 32X (Mars) Memory-Mapped Registers (From 68000 View)                       */
/* ========================================================================== */

#define MARS_SYS_INTMSK       (*(volatile uint16_t *)0xA15100) /**< Adapter Enable & Interrupt Mask */
#define MARS_SYS_RES          (*(volatile uint16_t *)0xA15102) /**< SH-2 Reset Release Control        */
#define MARS_SYS_BANK         (*(volatile uint16_t *)0xA15104) /**< 32X ROM Bank Register            */
#define MARS_SYS_DMAC         (*(volatile uint16_t *)0xA15106) /**< DREQ Control                     */

#define MARS_SYS_COMM0        (*(volatile uint32_t *)0xA15120) /**< Inter-CPU COMM0/1 (32-bit)       */
#define MARS_SYS_COMM2        (*(volatile uint32_t *)0xA15124) /**< Inter-CPU COMM2/3 (32-bit)       */
#define MARS_SYS_COMM4        (*(volatile uint32_t *)0xA15128) /**< Inter-CPU COMM4/5 (32-bit)       */
#define MARS_SYS_COMM6        (*(volatile uint32_t *)0xA1512C) /**< Inter-CPU COMM6/7 (32-bit)       */

#define MARS_SYS_COMM0_W      (*(volatile uint16_t *)0xA15120) /**< COMM0 (16-bit word)              */
#define MARS_SYS_COMM1_W      (*(volatile uint16_t *)0xA15122) /**< COMM1 (16-bit word)              */
#define MARS_SYS_COMM2_W      (*(volatile uint16_t *)0xA15124) /**< COMM2 (16-bit word)              */
#define MARS_SYS_COMM3_W      (*(volatile uint16_t *)0xA15126) /**< COMM3 (16-bit word)              */
#define MARS_SYS_COMM4_W      (*(volatile uint16_t *)0xA15128) /**< COMM4 (16-bit word)              */
#define MARS_SYS_COMM5_W      (*(volatile uint16_t *)0xA1512A) /**< COMM5 (16-bit word)              */
#define MARS_SYS_COMM6_W      (*(volatile uint16_t *)0xA1512C) /**< COMM6 (16-bit word)              */
#define MARS_SYS_COMM7_W      (*(volatile uint16_t *)0xA1512E) /**< COMM7 (16-bit word)              */

#define MARS_VDP_MODE         (*(volatile uint16_t *)0xA15180) /**< 32X VDP Bitmap Mode               */
#define MARS_VDP_SHIFT        (*(volatile uint16_t *)0xA15182) /**< 32X VDP Shift / Scroll           */
#define MARS_VDP_FILLLEN      (*(volatile uint16_t *)0xA15184) /**< 32X VDP Auto-fill Length         */
#define MARS_VDP_FILLADDR     (*(volatile uint16_t *)0xA15186) /**< 32X VDP Auto-fill Start Address  */
#define MARS_VDP_FILLDATA     (*(volatile uint16_t *)0xA15188) /**< 32X VDP Auto-fill Data           */
#define MARS_VDP_FS           (*(volatile uint16_t *)0xA1518A) /**< 32X Framebuffer Select (Flip)    */

/* 32X Magic Handshake Constants */
#define MARS_MAGIC_MOK        0x4D4F4B20U /**< 'MOK ' (Master SH-2 Initialized) */
#define MARS_MAGIC_SOK        0x534F4B20U /**< 'SOK ' (Slave SH-2 Initialized)  */
#define MARS_MAGIC_GO         0x474F2120U /**< 'GO! ' (Genesis Dispatch Go)      */

/* ========================================================================== */
/* Sega CD Memory-Mapped Registers (From 68000 Main View)                     */
/* ========================================================================== */

#define MCD_SYS_RESET         (*(volatile uint16_t *)0xA12000) /**< Sub-CPU Reset / Busreq           */
#define MCD_SYS_MEMMODE       (*(volatile uint16_t *)0xA12002) /**< Word RAM 1M/2M Mode & Priority   */
#define MCD_SYS_CDCMODE       (*(volatile uint16_t *)0xA12004) /**< CDC Mode & Device Destination    */
#define MCD_SYS_HINT          (*(volatile uint16_t *)0xA12006) /**< H-INT Vector Register            */

#define MCD_COM_FLAGS         (*(volatile uint16_t *)0xA1200E) /**< Communication Flags (Main/Sub)   */
#define MCD_COM_CMD0          (*(volatile uint16_t *)0xA12010) /**< Main-to-Sub Command 0            */
#define MCD_COM_CMD1          (*(volatile uint16_t *)0xA12012) /**< Main-to-Sub Command 1            */
#define MCD_COM_CMD2          (*(volatile uint16_t *)0xA12014) /**< Main-to-Sub Command 2            */
#define MCD_COM_CMD3          (*(volatile uint16_t *)0xA12016) /**< Main-to-Sub Command 3            */

#define MCD_COM_STAT0         (*(volatile uint16_t *)0xA12020) /**< Sub-to-Main Status 0             */
#define MCD_COM_STAT1         (*(volatile uint16_t *)0xA12022) /**< Sub-to-Main Status 1             */
#define MCD_COM_STAT2         (*(volatile uint16_t *)0xA12024) /**< Sub-to-Main Status 2             */
#define MCD_COM_STAT3         (*(volatile uint16_t *)0xA12026) /**< Sub-to-Main Status 3             */

/* Word RAM Base Pointers */
#define MCD_WORD_RAM_2M       ((volatile uint16_t *)0x200000) /**< 256 KB unified Word RAM           */
#define MCD_WORD_RAM_1M       ((volatile uint16_t *)0x200000) /**< 128 KB active bank in 1M Mode     */

/* ========================================================================== */
/* Mega EverDrive EDIO Registers                                              */
/* ========================================================================== */

#define EVD_REG_CFG           (*(volatile uint16_t *)0xA130D0) /**< Config / Unlock Key Register     */
#define EVD_REG_STATUS        (*(volatile uint16_t *)0xA130D0) /**< Status flags                     */
#define EVD_REG_CMD           (*(volatile uint16_t *)0xA130D2) /**< Command dispatch                 */
#define EVD_REG_DATA          (*(volatile uint16_t *)0xA130D4) /**< Data FIFO (read/write)           */
#define EVD_REG_USB_DATA      (*(volatile uint16_t *)0xA130D6) /**< USB FIFO interface               */

#define EVD_STAT_SPI_BUSY     (1 << 0)
#define EVD_STAT_FIFO_RXF     (1 << 1)
#define EVD_STAT_FIFO_TXE     (1 << 2)

/* ========================================================================== */
/* Tower Bootstrapping & Control API                                          */
/* ========================================================================== */

/**
 * @brief Probe the console hardware stack for all connected add-ons.
 * @return Bitmask of TOWER_FLAG_* indicating present hardware.
 */
uint16_t tower_probe_hardware(void);

/**
 * @brief Initialize all detected hardware in the Tower of Power.
 * @return Active hardware bitmask.
 */
uint16_t tower_init(void);

/**
 * @brief Initialize 32X subsystem: unlock Mars adapter, release SH-2 reset, and handshake.
 * @return true if both Master and Slave SH-2s successfully report ready.
 */
bool tower_init_32x(void);

/**
 * @brief Initialize Sega CD subsystem: bring Sub-CPU online and configure Word RAM.
 * @param ping_pong_1m If true, configures 1M ping-pong mode (60 FPS); else 2M mode.
 * @return true if Sub-CPU handshake succeeded.
 */
bool tower_init_segacd(bool ping_pong_1m);

/**
 * @brief Initialize Mega EverDrive EDIO registers and USB logging.
 * @return true if EverDrive unlocked and responded.
 */
bool tower_init_everdrive(void);

/**
 * @brief Perform 1M Word RAM bank swap with the Sega CD Sub-CPU.
 * Toggles the RET bit and waits for ownership confirmation.
 */
void tower_swap_word_ram(void);

/**
 * @brief Dispatch command to 32X Master SH-2.
 */
void tower_send_sh2_cmd(uint16_t cmd, uint16_t arg0, uint16_t arg1);

/**
 * @brief Dispatch command to Sega CD Sub-CPU.
 */
void tower_send_cd_cmd(uint16_t cmd, uint16_t arg0, uint16_t arg1);

/**
 * @brief Write log string to Mega EverDrive USB link (edlink) for host debugging.
 * Has zero effect if EverDrive is not connected.
 */
void tower_log(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* _TOWER_H_ */

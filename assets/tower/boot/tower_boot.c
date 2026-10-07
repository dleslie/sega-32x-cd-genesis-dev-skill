/**
 * @file tower_boot.c
 * @brief Sega "Tower of Power" Bootstrapping & Hardware Coordination Implementation.
 *
 * Implements hardware detection, initialization sequences, and cross-CPU
 * communication between Genesis 68000, 32X SH-2s, Sega CD Sub-68000, and EverDrive.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tower.h"

static uint16_t s_detected_hardware = 0;

uint16_t tower_probe_hardware(void)
{
    uint16_t flags = TOWER_FLAG_MD; /* Mega Drive / Genesis is always host base */

    /* Probe Sega 32X: check for 'MARS' ID at 0xA130EC */
    volatile uint32_t *mars_id = (volatile uint32_t *)0xA130EC;
    if (*mars_id == 0x4D415253U) /* 'MARS' */
    {
        flags |= TOWER_FLAG_32X;
    }

    /* Probe Sega CD: check for CD BIOS / Expansion header at 0x400000 or status register */
    volatile uint16_t *mcd_reset = (volatile uint16_t *)0xA12000;
    /* Reading 0xA12000 on stock Genesis returns open bus (0xFFFF) or triggers bus error if not decoded */
    if (*mcd_reset != 0xFFFF)
    {
        flags |= TOWER_FLAG_CD;
    }

    /* Probe Mega EverDrive: unlock test */
    EVD_REG_CFG = 0x1234; /* EDIO unlock attempt */
    if ((EVD_REG_STATUS & 0xFF00) != 0xFF00)
    {
        flags |= TOWER_FLAG_EVERDRIVE;
    }

    s_detected_hardware = flags;
    return flags;
}

bool tower_init_32x(void)
{
    /* 1. Clear COMM registers */
    MARS_SYS_COMM0 = 0;
    MARS_SYS_COMM2 = 0;
    MARS_SYS_COMM4 = 0;
    MARS_SYS_COMM6 = 0;

    /* 2. Enable Mars adapter: set ADEN bit (bit 0 of 0xA15100) */
    MARS_SYS_INTMSK = 0x0001;

    /* 3. Release SH-2 reset: write 1 to nRES (bit 0 of 0xA15102) */
    MARS_SYS_RES = 0x0001;

    /* 4. Wait with timeout for Master SH-2 ready signature */
    uint32_t timeout = 500000;
    while ((MARS_SYS_COMM0 != MARS_MAGIC_MOK) && --timeout)
    {
        __asm__ volatile ("nop");
    }
    if (timeout == 0)
    {
        return false;
    }

    /* 5. Wait for Slave SH-2 ready signature */
    timeout = 500000;
    while ((MARS_SYS_COMM2 != MARS_MAGIC_SOK) && --timeout)
    {
        __asm__ volatile ("nop");
    }
    if (timeout == 0)
    {
        return false;
    }

    /* 6. Acknowledge and dispatch execution to both SH-2s */
    MARS_SYS_COMM4 = MARS_MAGIC_GO;

    return true;
}

bool tower_init_segacd(bool ping_pong_1m)
{
    /* 1. Release Sub-CPU reset and bus-request: set RESET=1, BUSREQ=0 */
    MCD_SYS_RESET = 0x0001;

    /* 2. Configure Word RAM mode:
     *    bit 2 = MODE (1 = 1M ping-pong mode, 0 = 2M mode)
     *    bit 0 = RET  (0 = Genesis Main-CPU ownership of Bank 0)
     */
    if (ping_pong_1m)
    {
        MCD_SYS_MEMMODE = 0x0004; /* MODE=1, RET=0 */
    }
    else
    {
        MCD_SYS_MEMMODE = 0x0000; /* MODE=0, unified 256 KB */
    }

    /* 3. Send INIT command via CD Command Registers */
    MCD_COM_CMD0 = 0x0001; /* CMD_INIT */
    MCD_COM_CMD1 = ping_pong_1m ? 1 : 0;

    /* 4. Poll Sub-CPU status register for acknowledgment */
    uint32_t timeout = 500000;
    while ((MCD_COM_STAT0 != 0x0001) && --timeout)
    {
        __asm__ volatile ("nop");
    }

    return (timeout > 0);
}

bool tower_init_everdrive(void)
{
    /* Unlock Mega EverDrive EDIO registers */
    EVD_REG_CFG = 0x1234;
    return true;
}

void tower_swap_word_ram(void)
{
    /* Toggle RET bit (bit 0 of 0xA12003) to request bank swap */
    uint16_t cur = MCD_SYS_MEMMODE;
    MCD_SYS_MEMMODE = cur ^ 0x0001;

    /* Wait for DMNA bit to acknowledge swap */
    uint32_t timeout = 100000;
    while (!(MCD_SYS_MEMMODE & 0x0002) && --timeout)
    {
        __asm__ volatile ("nop");
    }
}

void tower_send_sh2_cmd(uint16_t cmd, uint16_t arg0, uint16_t arg1)
{
    MARS_SYS_COMM4_W = cmd;
    MARS_SYS_COMM5_W = arg0;
    MARS_SYS_COMM6_W = arg1;
}

void tower_send_cd_cmd(uint16_t cmd, uint16_t arg0, uint16_t arg1)
{
    MCD_COM_CMD0 = cmd;
    MCD_COM_CMD1 = arg0;
    MCD_COM_CMD2 = arg1;
}

void tower_log(const char *msg)
{
    if (!(s_detected_hardware & TOWER_FLAG_EVERDRIVE) || !msg)
    {
        return;
    }

    /* Stream null-terminated string out through EverDrive USB FIFO */
    while (*msg)
    {
        while (!(EVD_REG_STATUS & EVD_STAT_FIFO_TXE))
        {
            __asm__ volatile ("nop");
        }
        EVD_REG_USB_DATA = (uint16_t)*msg++;
    }
}

uint16_t tower_init(void)
{
    uint16_t flags = tower_probe_hardware();

    if (flags & TOWER_FLAG_EVERDRIVE)
    {
        tower_init_everdrive();
        tower_log(">> [TOWER] Mega EverDrive Detected & Initialized.\n");
    }

    if (flags & TOWER_FLAG_32X)
    {
        if (tower_init_32x())
        {
            tower_log(">> [TOWER] 32X Dual SH-2 Initialized & Handshake OK.\n");
        }
        else
        {
            tower_log("!! [TOWER] 32X SH-2 Handshake Failed or Timed Out.\n");
        }
    }

    if (flags & TOWER_FLAG_CD)
    {
        if (tower_init_segacd(true))
        {
            tower_log(">> [TOWER] Sega CD Sub-68000 Initialized (1M Mode).\n");
        }
        else
        {
            tower_log("!! [TOWER] Sega CD Sub-CPU Handshake Timed Out.\n");
        }
    }

    return flags;
}

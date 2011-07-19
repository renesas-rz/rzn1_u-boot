/*
 * include/configs/bockw.h
 *
 * Copyright(c) 2011 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef	__CONFIG_H
#define	__CONFIG_H

/* commands to include */
#define	CONFIG_CMD_EDITENV
#define	CONFIG_CMD_SAVEENV
#define	CONFIG_CMD_FLASH
#define	CONFIG_CMD_MEMORY
#define	CONFIG_CMD_MISC
// SGL: Temp disable networking
#if 1
#define	CONFIG_CMD_NET
#define	CONFIG_CMD_DHCP
#define	CONFIG_CMD_PING
#endif
#define	CONFIG_CMD_RUN

#define	CONFIG_CMDLINE_TAG
#define	CONFIG_SETUP_MEMORY_TAGS
#define	CONFIG_INITRD_TAG
#define	CONFIG_CMDLINE_EDITING

/* autoboot */
#define CONFIG_BOOTDELAY	3
#define CONFIG_BOOTCOMMAND	"bootp; bootm"
#define CONFIG_ZERO_BOOTDELAY_CHECK

/* high level configuration options */
#define	CONFIG_ARMV7	1
#define	CONFIG_MILAN

/* keep l2 cache disabled */
#define	CONFIG_L2_OFF	1

/* ram memory map */
// SGL: Milan: Milan has no SRAM
//#define	CONFIG_SYS_SRAM_BASE	0x04000000
//#define	CONFIG_SYS_SRAM_SIZE	0x00800000
// SGL: End

#define	CONFIG_NR_DRAM_BANKS	1
// SGL: Milan: Milan starts at 0x4000 0000, not like BockW at 0x6000 0000
//#define	CONFIG_SYS_SDRAM_BASE	0x60000000
#define	CONFIG_SYS_SDRAM_BASE	0x40000000
// SGL: End
#define	CONFIG_SYS_SDRAM_SIZE	(512 * 1024 * 1024)
#define	CONFIG_SYS_MALLOC_LEN	(CONFIG_ENV_SIZE + 16 * 1024)
#define	CONFIG_SYS_INIT_SP_ADDR	(0xfe798000 - 0x104)
#define	CONFIG_SYS_LOAD_ADDR	(CONFIG_SYS_SDRAM_BASE + 0x7fc0)

#define	CONFIG_SYS_MEMTEST_START	CONFIG_SYS_SDRAM_BASE
#define	CONFIG_SYS_MEMTEST_END \
	(CONFIG_SYS_SDRAM_BASE - 1 + CONFIG_SYS_SDRAM_SIZE)
#define	CONFIG_SYS_ALT_MEMTEST

/* serial port */
#define	CONFIG_BOARD_EARLY_INIT_F	1
#define	CONFIG_SCIF_CONSOLE	1
#define	CONFIG_CONS_SCIF0	1
#define	SCIF0_BASE		0xffe40000
// SGL: Milan: Milan has ext clk @ 1.8432 MHz, whilst BockW ext clk is 14.7456 MHz
//#define	CONFIG_SYS_CLK_FREQ	14745600
#define	CONFIG_SYS_CLK_FREQ     1843200
// SGL: End
#define	CONFIG_CPU_RCM1A	1

#define	CONFIG_BAUDRATE	115200
#define	CONFIG_SYS_BAUDRATE_TABLE	{ 115200 }

/* ethernet */
#define	CONFIG_NET_MULTI
#define	CONFIG_SMC911X		1
#define	CONFIG_SMC911X_16_BIT
// SGL: Milan: Base addr is different in Milan.
#if 1
#define	CONFIG_SMC911X_BASE	0x18000100
#else
#define	CONFIG_SMC911X_BASE	0x18300000
#endif

/* flash configuration */
#define	CONFIG_SYS_FLASH_CFI
#define	CONFIG_SYS_FLASH_CFI_WIDTH	FLASH_CFI_16BIT
#define	CONFIG_FLASH_CFI_DRIVER
#define	CONFIG_CFI_FLASH_USE_WEAK_ACCESSORS
#define	CONFIG_FLASH_SHOW_PROGRESS	45
#define	CONFIG_SYS_HZ		1000
#define	CONFIG_SYS_TIMERBASE	0xffd80000
#define	CONFIG_SYS_FLASH_BASE	0x00000000
#define	CONFIG_SYS_MAX_FLASH_BANKS	1
#define	CONFIG_SYS_MONITOR_BASE	CONFIG_SYS_FLASH_BASE
#define	CONFIG_SYS_MAX_FLASH_SECT	1024	/* sectors per dev */
#define	CONFIG_ENV_SECT_SIZE	0x20000
#define	CONFIG_ENV_SIZE		CONFIG_ENV_SECT_SIZE
#define	CONFIG_ENV_IS_IN_FLASH	1
#define	CONFIG_ENV_OFFSET	0x00060000
#define	CONFIG_ENV_ADDR	(CONFIG_SYS_FLASH_BASE + CONFIG_ENV_OFFSET)

/* misc */
#define	CONFIG_SYS_LONGHELP
#define CONFIG_SYS_PROMPT       "$ "
#define	CONFIG_SYS_CBSIZE	256
#define	CONFIG_SYS_PBSIZE \
	(CONFIG_SYS_CBSIZE + sizeof(CONFIG_SYS_PROMPT) + 16)
#define	CONFIG_SYS_MAXARGS	16

#define	CONFIG_DISPLAY_BOARDINFO
#define	CONFIG_DISPLAY_CPUINFO
#define	CONFIG_EXTRA_ENV_SETTINGS "\0"

#define CONFIG_ARM_ERRATA_743622

#endif	/* __CONFIG_H */

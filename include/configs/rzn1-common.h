/*
 * (C) Copyright 2016 Renesas Electronics Europe Ltd
 * Phil Edworthy <phil.edworthy@renesas.com>
 *
 * Renesas RZ/N1 device configuration - you should not change these!
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1_COMMON_H
#define __RZN1_COMMON_H

#include "renesas/rzn1-memory-map.h"

#define CONFIG_CORTEX_A7
#define CONFIG_SMP_PEN_WFE
#define CONFIG_BOARD_EARLY_INIT_F
#ifndef CONFIG_ARCH_RZN1L
 #define CONFIG_SYS_CACHELINE_SIZE	64	/* Needed by DFU */
#endif

/* Clocks. All but the ARM timer clock are used to program clock dividers */
/* ARM timer clock, this is fixed */
#define CONFIG_SYS_HZ_CLOCK		6250000
#define CONFIG_SYS_CLK_FREQ		CONFIG_SYS_HZ_CLOCK

/* 20MHz UART clock. This can generate 114942 baud, i.e 0.22% error. */
#define CONFIG_SYS_NS16550_CLK		20000000

/* 83.33MHz NAND Flash Ctrl clock, fastest as NAND is an async i/f. */
#define CONFIG_SYS_NAND_CLOCK		83333333

/* 250MHz QSPI Ctrl clock. The driver divides to meet the requested
 * SPI clock. This sysctrl divider can divide by any number, whereas
 * the divider in the IP can only divide by even numbers. However,
 * Cadence recommend the IP divider is set to a minimum of 4 and the
 * SPI clock maximum is 62.5MHz. With this setting you can get SPI
 * clocks of 62.5MHz, 31.25MHz, etc.
 */
#define CONFIG_CQSPI_REF_CLK		250000000

/* 83.33MHz I2C clock */
#define IC_CLK				83

/* 50MHz SDHCI clock */
#define SDHC_CLK_MHZ			50


/* SRAM */
#define CONFIG_SYS_SRAM_BASE		RZN1_SRAM_ID_BASE
#define CONFIG_SYS_SRAM_SIZE		RZN1_SRAM_ID_SIZE

/* DDR Memory */
#ifdef CONFIG_ARCH_RZN1D
#define CONFIG_CADENCE_DDR_CTRL
#define CONFIG_SYS_SDRAM_BASE		RZN1_V_DDR_BASE
#define CONFIG_NR_DRAM_BANKS		1
#define CONFIG_SYS_SDRAM_SIZE		(256 * 1024 * 1024)
#endif

/* Serial */
#define CONFIG_SYS_NS16550_MEM32

/* Serial: sensible defaults */
#define CONFIG_SYS_CBSIZE		256
#define CONFIG_SYS_MAXARGS		16

/* SPI */
#define CONFIG_SPI_REGISTER_FLASH	/* required */

/* NAND Flash */
#define CONFIG_SYS_NO_FLASH
#define CONFIG_SYS_NAND_BASE		RZN1_NAND_BASE
#define CONFIG_SYS_NAND_SELF_INIT
#define CONFIG_SYS_NAND_ONFI_DETECTION

/* NAND: sensible defaults */
#define CONFIG_SYS_MAX_NAND_DEVICE	1

/* Timer */
#define CONFIG_SYS_ARCH_TIMER

/* Ethernet */
#define CONFIG_DW_ALTDESCRIPTOR

#define CONFIG_SYS_BOOTM_LEN	(64 << 20)	/* Increase max gunzip size */

#endif /* RZN1_COMMON_H */

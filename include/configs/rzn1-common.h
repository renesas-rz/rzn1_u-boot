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

#define CONFIG_RZN1
#define CONFIG_CORTEX_A7
#define CONFIG_SMP_PEN_WFE
#define CONFIG_BOARD_EARLY_INIT_F
#define CONFIG_SYS_GENERIC_BOARD
#define CONFIG_SYS_CACHELINE_SIZE	64	/* Needed by DFU */
/* Arch specific memcpy is critical for QSPI read performance */
#define CONFIG_USE_ARCH_MEMCPY
/* Arch specific memset is critical for performance when using DDR with ECC */
#define CONFIG_USE_ARCH_MEMSET

/* Clocks */
#define CONFIG_SYS_CLK_FREQ		250000000
#define CONFIG_SYS_HZ_CLOCK		  6250000
#define CONFIG_SYS_NS16550_CLK		 20000000
#define CONFIG_SYS_NAND_CLOCK		 83333333
#define CONFIG_CQSPI_REF_CLK		250000000
#define IC_CLK				100	/* MHz */

/* SRAM */
#define CONFIG_SYS_SRAM_BASE		RZN1_SRAM_ID_BASE
#define CONFIG_SYS_SRAM_SIZE		RZN1_SRAM_ID_SIZE

/* DDR Memory */
#ifndef CONFIG_RZN1_NO_DDR
#define CONFIG_CADENCE_DDR_CTRL
#define CONFIG_SYS_SDRAM_BASE		RZN1_V_DDR_BASE
#define CONFIG_NR_DRAM_BANKS		1
#define CONFIG_SYS_SDRAM_SIZE		(256 * 1024 * 1024)
#endif

/* Serial */
#define CONFIG_SYS_NS16550
#define CONFIG_SYS_NS16550_SERIAL
#define CONFIG_SYS_NS16550_MEM32
#define CONFIG_SYS_NS16550_REG_SIZE	-4
#define CONFIG_SYS_NS16550_COM1		RZN1_UART0_BASE
#define CONFIG_SYS_NS16550_COM2		RZN1_UART1_BASE
#define CONFIG_SYS_NS16550_COM3		RZN1_UART2_BASE

/* Serial: sensible defaults */
#define CONFIG_CONS_INDEX		1
#define CONFIG_SYS_BAUDRATE_TABLE  	{9600, 19200, 38400, 57600, 115200}
#define CONFIG_SYS_CBSIZE		256
#define CONFIG_SYS_PBSIZE		256
#define CONFIG_SYS_MAXARGS		16

/* SPI */
/* NODE: CONFIG_CADENCE_QSPI_MODE needs to be defined in your board file */
#if !defined(CONFIG_RZN1S_USE_QSPI1)
 #define CONFIG_CQSPI_BASE		RZN1_QSPI_BASE
 #define CONFIG_CQSPI_AHB_BASE		RZN1_V_QSPI_BASE
#else
 #define CONFIG_CQSPI_BASE		RZN1_QSPI_BASE, RZN1_QSPI1_BASE
 #define CONFIG_CQSPI_AHB_BASE		RZN1_V_QSPI_BASE, RZN1_V_QSPI1_BASE
#endif
#define CONFIG_SPI_REGISTER_FLASH	/* required */
#define CONFIG_SPI_FLASH_BAR		/* Bank addressing, i.e. use 4 byte addr for > 16MB */

/* NAND Flash */
#define CONFIG_NAND_CADENCE_EVATRONIX
#define CONFIG_SYS_NO_FLASH
#define CONFIG_SYS_NAND_BASE		RZN1_NAND_BASE
#define CONFIG_SYS_NAND_SELF_INIT
#define CONFIG_SYS_NAND_ONFI_DETECTION

/* NAND: sensible defaults */
#define CONFIG_SYS_MAX_NAND_DEVICE	1

/* Timer */
#define CONFIG_SYS_ARCH_TIMER

/* I2C */
#define CONFIG_SYS_I2C_SLAVE		0	/* slave only so ignored */

/* Ethernet */
#define CONFIG_DW_ALTDESCRIPTOR

/* USB */
#define CONFIG_G_DNL_MANUFACTURER	"Renesas Electronics, LTD"
#define CONFIG_G_DNL_VENDOR_NUM		0x045b	/* Renesas Vendor ID */
#define CONFIG_USB_GADGET_DUALSPEED
#define CONFIG_USB_GADGET_VBUS_DRAW	0	/* Not bus powered */

#endif /* RZN1_COMMON_H */

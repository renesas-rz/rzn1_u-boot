/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 * Phil Edworthy <phil.edworthy@renesas.com>
 *
 * Renesas RZ/N1 device configuration for SPL builds - you should not change these!
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1_SPL_H
#define __RZN1_SPL_H

/* SPL part */
#define CONFIG_SPL
#define CONFIG_SPL_FRAMEWORK
#define CONFIG_SPL_LIBCOMMON_SUPPORT
#define CONFIG_SPL_LIBGENERIC_SUPPORT
#define CONFIG_SPL_BOARD_INIT
#define CONFIG_SPL_SERIAL_SUPPORT
#define CONFIG_SPL_MULTIIMAGE

/* Allow booting linux directly */
#define CONFIG_SPL_OS_BOOT
/* We /cannot/ boot linux without a device tree */
#define CONFIG_SPL_LIBFDT_SUPPORT

/* This is required to fish the ethernet addresses from the environment */
#if defined(RZN1_ENABLE_ETHERNET)
#define CONFIG_SPL_ENV_SUPPORT
#endif

#if defined(CONFIG_SPL_SPI_LOAD)
	#define CONFIG_SPL_SPI_FLASH_SUPPORT
	#define CONFIG_SPL_SPI_SUPPORT
#endif
#if defined(CONFIG_SPL_NAND_LOAD)
	#define CONFIG_SPL_NAND_SUPPORT
	#define CONFIG_SPL_NAND_DRIVERS
	#define CONFIG_SPL_NAND_ONFI
	/*
	 * #define CONFIG_SPL_NAND_SIMPLE
	 * This driver is presumably quite a bit smaller, but requires quite a few
	 * hard coded parameters to work.
	 */
#endif

#endif /* RZN1_SPL_H */

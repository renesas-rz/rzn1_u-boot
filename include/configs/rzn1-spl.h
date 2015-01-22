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

#if defined(CONFIG_SPL_SPI_LOAD)
	#define CONFIG_SPL_SPI_FLASH_SUPPORT
	#define CONFIG_SPL_SPI_SUPPORT
#else
	#define CONFIG_SPL_NAND_SUPPORT
	#define CONFIG_SPL_NAND_DRIVERS
	#define CONFIG_SPL_NAND_ONFI
#endif

#endif /* RZN1_SPL_H */

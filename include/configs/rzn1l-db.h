/*
 * Renesas RZN1L-DB board
 *
 * (C) Copyright 2017 Renesas Electronics Europe Limited
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1L_DB_H
#define __RZN1L_DB_H

#include "rzn1-common.h"
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"

#define CONFIG_BOARD_NAME	"Renesas RZN1L-DB"

#define CONFIG_OF_BOARD_SETUP
#define CONFIG_BOARD_LATE_INIT
#define CONFIG_SYS_DCACHE_OFF

#define CONFIG_SYS_THUMB_BUILD
#define CONFIG_SYS_LONGHELP
#define	CONFIG_CMDLINE_EDITING
#define CONFIG_VERSION_VARIABLE		/* include version env variable */
/* debug purpose */
#define CONFIG_MD5
#define CONFIG_CMD_MD5SUM

/* Local helper symbols to enable/disable functionality */
#define RZN1_ENABLE_I2C
#define RZN1_ENABLE_ETHERNET
#define RZN1_ENABLE_QSPI
#undef RZN1_ENABLE_NAND
#define RZN1_ENABLE_USBF

/* SRAM */
#define CONFIG_SYS_TEXT_BASE		0x200a0000
#define CONFIG_SYS_STAY_IN_SRAM
/* Very early stack, 320KB above start of U-Boot image */
#define CONFIG_SYS_INIT_SP_ADDR		(CONFIG_SYS_TEXT_BASE + (320 * 1024) - 4)
#define CONFIG_SYS_MALLOC_LEN		(320 * 1024)

#define	CONFIG_SYS_ALT_MEMTEST
#define CONFIG_SYS_MEMTEST_SCRATCH	RZN1_SRAM_SYS_BASE

/* This is not used by uboot startup, just by qspi etc */
#define CONFIG_SYS_LOAD_ADDR		0x80000000

/* Serial */
#define CONFIG_BAUDRATE			115200


/* ENV settings.
 * To avoid U-Boot accessing NAND/QSPI, you might want to set NOWHERE instead.
 */
#if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_ENV_IS_IN_SPI_FLASH
#else
 #define CONFIG_ENV_IS_NOWHERE		/* Store ENV in memory only */
#endif

/* The ENV offsets match the DFU offsets for *_env */
#if defined(CONFIG_ENV_IS_IN_SPI_FLASH)
 #define CONFIG_ENV_OFFSET		0xa0000
 #define CONFIG_ENV_SECT_SIZE		(64 * 1024)
#endif
#define CONFIG_ENV_SIZE			(8 * 1024)


/***** SPI Flash *****/
#if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_SF_DEFAULT_SPEED	62500000
 #define CONFIG_ENV_SPI_MAX_HZ		62500000
/* Reading using QuadIO can achieve 20% better throughput compared to QuadOutput
 * on this board. However, the if you want to use the same U-Boot binary on
 * boards with a second source SPI Flash, check that all use the same number of
 * dummy cycles in this mode. If not, you may want to use the QuadOuput read
 * command instead */
/* Macronix MX25L25635F : 8 dummy cycles for Quad Output Fast,  6 for QuadIO */
/* Micron   N25Q128     : 8 dummy cycles for Quad Output Fast, 10 for QuadIO */
 #define CONFIG_SPI_FLASH_READ_QUAD_CMD	CMD_READ_QUAD_IO_FAST
 #define CONFIG_SPI_FLASH_DUMMY_CYCLES	6
 #define CONFIG_CQSPI_DECODER		0	/* i.e. don't use 4-to-16 CS decoder */
#endif /* RZN1_ENABLE_QSPI */

#define CONFIG_SYS_MAX_FLASH_BANKS	1


/***** Ethernet *****/
#if defined(RZN1_ENABLE_ETHERNET)
 #define CONFIG_MII
 #define CONFIG_PHY_GIGE		/* Include GbE speed/duplex detection */
 #define CONFIG_PHY_MICREL
 #define CONFIG_PHY_MARVELL
 #define CONFIG_PHY_RESET_DELAY		1000	/* PHY RESET recovery delay in usec */

/*
 * GIGABIT_ETHERNET (IC43) connector is not fitted by default, but we still try it.
 * It's connected to a Marvell 88E1512 PHY, which is connected to RGMII/GMII
 * Converter 1, and then on to GMAC1.
 */
 #define CONFIG_PHY_ADDR		8

/*
 * GMAC2 is connected to the 5pt-switch, and all ports are enabled.
 * Switch Port  RGMII/GMII Converter    PHY Address    Connector
 *      1               4                    4            CN1
 *      0               5                    5            CN5
 */
 #define CONFIG_HAS_ETH1
 #define CONFIG_PHY1_ADDR		4
#endif /* RZN1_ENABLE_ETHERNET */

/***** USB Device aka Gadget aka Function *****/
#if defined(RZN1_ENABLE_USBF)
 /* Default is... 8 MB !! Note that it needs to be AT LEAST the size of
  * a single erase block of the nand/qspi, otherwise the operation will fail */
 #define CONFIG_SYS_DFU_DATA_BUF_SIZE	(64 * 1024)
 #define DFU_DEFAULT_POLL_TIMEOUT	100

 #define DFU_EXT_INFO \
	"dfu_ext_info=" \
	"sf sf_spl raw 0 10000;" \
	"sf sf_rpkgt raw 10000 10000;" \
	"sf sf_uboot raw 20000 80000;" \
	"sf sf_env raw a0000 10000;" \
	"sf sf_dtb raw b0000 20000;" \
	"sf sf_cm3 raw d0000 100000;" \
	"sf sf_kernel raw 1d0000 600000;" \
	"sf sf_data raw 7d0000 0;" \
	"sf sf_vxworks raw d0000 600000;" \
	"ram r_kernel ram 80000000 400000\0"
#else
 #define DFU_EXT_INFO
#endif /* RZN1_ENABLE_USBF */

#define CONFIG_BOOTDELAY 1
#define CONFIG_BOOTCOMMAND ""
/* Default environment variables */
#define CONFIG_EXTRA_ENV_SETTINGS \
	DFU_EXT_INFO \
	"bootdelay=1\0" \
	"loadaddr=80000000\0" \
	"ethaddr=00:00:0a:02:57:CD\0" \
	"eth1addr=00:00:0a:02:57:CE\0" \
	"netmask=255.255.255.0\0" \
	"serverip=192.168.1.30\0" \
	"ipaddr=192.168.1.70\0"

#endif /* __RZN1L_DB_H */

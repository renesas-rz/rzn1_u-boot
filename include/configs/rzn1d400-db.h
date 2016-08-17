/*
 * Renesas RZN1-400  module (RZ/N1D device) base board
 *
 * (C) Copyright 2016 Renesas Electronics Europe Limited
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1D400_DB_H
#define __RZN1D400_DB_H

#include "rzn1-common.h"
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"

#undef RZN1_ENABLE_NAND
#undef CONFIG_NAND_CADENCE_EVATRONIX

/*
 * The BootROM will start the 2nd core and execute WFE. On an event it will
 * read from the SYSCTRL BOOTADDR register. However, this register can only
 * be accessed from Secure mode. U-Boot switches to Non-secure mode by writing
 * the address of _smp_pen to BOOTADDR, then kicking the 2nd CPU. Normally, the
 * _smp_pen code switches mode, then runs the smp_waitloop code. Instead we
 * run secondary smp_waitloop code that uses a different PEN address.
 *
 * Note: Modified functionality of CONFIG_ARMV7_NONSEC and CONFIG_ARMV7_VIRT.
 * Specifying these includes the code to support switching to NONSEC or HYP
 * mode, but U-Boot will only switch if boot_nonsec or boot_hyp environment
 * variables exist and are set to 1.
 */
#define CONFIG_ARMV7_NONSEC
#define CONFIG_ARMV7_VIRT
#define CONFIG_SMP_PEN_ADDR		smp_secondary_bootaddr
#define CONFIG_OF_BOARD_SETUP

#define CONFIG_SYS_THUMB_BUILD
#define CONFIG_SYS_LONGHELP
#define CONFIG_SYS_HUSH_PARSER
#define	CONFIG_CMDLINE_EDITING
#define CONFIG_CMD_MISC
#define CONFIG_CMD_RUN
#define CONFIG_CMD_CACHE
#define CONFIG_CMD_BDI
#define CONFIG_CMD_TIMER
#define CONFIG_CMD_IMI
#define CONFIG_CMD_BOOTZ
#define	CONFIG_OF_LIBFDT
#define CONFIG_VERSION_VARIABLE		/* include version env variable */
/* debug purpose */
#define CONFIG_MD5
#define CONFIG_CMD_MD5SUM
#define CONFIG_SOURCE
#define CONFIG_CMD_SOURCE
#define CONFIG_CMD_ECHO

/* Local helper symbols to enable/disable functionality */
#define RZN1_ENABLE_I2C
#define RZN1_ENABLE_QSPI
#define RZN1_ENABLE_ETHERNET
#define RZN1_ENABLE_SDHC
#define RZN1_ENABLE_USBF
#define RZN1_ENABLE_GPIO
#define RZN1_ENABLE_SPL
/* ECC is enabled */
#define RZN1_ENABLE_DDR_ECC

/* SRAM */
#define CONFIG_SYS_TEXT_BASE		0x200a0000
#define CONFIG_SYS_STAY_IN_SRAM
/* Very early stack, 320KB above start of U-Boot image */
#define CONFIG_SYS_INIT_SP_ADDR		(CONFIG_SYS_TEXT_BASE + (320 * 1024) - 4)
#define CONFIG_SYS_MALLOC_LEN		(320 * 1024)

/* DDR Memory */
#ifdef RZN1_ENABLE_DDR_ECC
#define CONFIG_CADENCE_DDR_CTRL_8BIT_WIDTH
#define CONFIG_CADENCE_DDR_CTRL_ENABLE_ECC
#define CONFIG_SYS_DDR_MAX_SIZE 	128
#else
#define CONFIG_SYS_DDR_MAX_SIZE 	256
#endif

#define CONFIG_CMD_MEMORY
#define CONFIG_CMD_MEMTEST
#define CONFIG_SYS_MEMTEST_START	(CONFIG_SYS_SDRAM_BASE)
#define CONFIG_SYS_MEMTEST_END		(CONFIG_SYS_MEMTEST_START + CONFIG_SYS_DDR_MAX_SIZE * 1024 * 1024 - 1)
#define	CONFIG_SYS_ALT_MEMTEST
#define CONFIG_SYS_MEMTEST_SCRATCH	RZN1_SRAM_SYS_BASE
#undef CONFIG_SYS_SDRAM_SIZE
#define CONFIG_SYS_SDRAM_SIZE		(CONFIG_SYS_DDR_MAX_SIZE * 1024 * 1024)

/* This is not used by uboot startup, just by qspi etc */
#define CONFIG_SYS_LOAD_ADDR		0x80008000

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
#define CONFIG_CMD_SAVEENV


/***** I2C *****/
#if defined(RZN1_ENABLE_I2C)
 #define CONFIG_HARD_I2C
 #define CONFIG_DW_I2C
 #define CONFIG_SYS_I2C_BASE		RZN1_I2C1_BASE
 #define CONFIG_SYS_I2C_SPEED		100000
 #define CONFIG_CMD_I2C
 #define CONFIG_PCA9698				/* there is one expander */
#endif /* RZN1_ENABLE_I2C */


/***** SPI Flash *****/
#if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_CADENCE_QSPI
 #define CONFIG_CMD_SF
 #define CONFIG_SPI
 #define CONFIG_CADENCE_QSPI_MODE	RD_FULL
 #define CONFIG_SF_DEFAULT_SPEED	62500000
 #define CONFIG_ENV_SPI_MAX_HZ		62500000
/* 6 dummy cycles for max compatibility */
 #define CONFIG_SPI_FLASH_DUMMY_CYCLES	6

 #define CONFIG_CQSPI_DECODER		0	/* i.e. don't use 4-to-16 CS decoder */
 /* Device specific CS timing delays in nano seconds */
 #define CONFIG_CQSPI_TSHSL_NS		200	/* De-Assert */
 #define CONFIG_CQSPI_TSD2D_NS		255	/* De-Assert Different Slaves */
 #define CONFIG_CQSPI_TCHSH_NS		20	/* End Of Transfer */
 #define CONFIG_CQSPI_TSLCH_NS		20	/* Start Of Transfer */

 /* QSPI Flash devices on CS0: 32MByte, Macronix MX25L25635F */
 #define CONFIG_SPI_FLASH
 #define CONFIG_SPI_FLASH_MACRONIX
 #endif /* RZN1_ENABLE_QSPI */


/***** GPIO *****/
#ifdef RZN1_ENABLE_GPIO
#define CONFIG_RZN1_GPIO
#define CONFIG_CMD_GPIO
#endif

/***** Ethernet *****/
#if defined(RZN1_ENABLE_ETHERNET)
 #define CONFIG_CMD_NET
 #define CONFIG_CMD_MII
 #define CONFIG_CMD_PING
 #define CONFIG_MII
 #define CONFIG_PHYLIB
 #define CONFIG_PHY_GIGE		/* Include GbE speed/duplex detection */
 #define CONFIG_PHY_MICREL
 #define CONFIG_PHY_MARVELL
 #define CONFIG_PHY_RESET_DELAY		1000	/* PHY RESET recovery delay in usec */
 #define CONFIG_DESIGNWARE_ETH

/* There are two GMACs */
/* GMAC1 is connected directly to RGMII/GMII Converter 1. This can only be used
 * with the Renesas RZ/N1 Extension board using PHY address 8, connector J22 */
 #define CONFIG_PHY_ADDR               8

/*
 * GMAC2 is connected to the 5pt-switch, and all ports are enabled.
 * Switch Port  RGMII/GMII Converter    PHY Address    Connector
 *      3               2                    1         Extension board J23
 *      2               3                    10        Extension board J24
 *      1               4                    4         CN1 (Default)
 *      0               5                    5         CN4
 */
 #define CONFIG_MT5PT_SWITCH
 #define CONFIG_HAS_ETH1
 #define CONFIG_PHY1_ADDR		4
 #define CONFIG_APPLY_ETH_PHY_RESET_PULSE /* Reset cycle on signal phy reset */
#endif /* RZN1_ENABLE_ETHERNET */

#if defined(CONFIG_APPLY_ETH_PHY_RESET_PULSE) && (!defined(RZN1_ENABLE_GPIO))
#error when CONFIG_APPLY_ETH_PHY_RESET_PULSE is defined, RZN1_ENABLE_GPIO must be defined
#endif

/***** SDHC *****/
#if defined(RZN1_ENABLE_SDHC)
 #define CONFIG_RZN1_SDHCI0
 #define CONFIG_SDHCI
 #define CONFIG_GENERIC_MMC
 #define CONFIG_MMC
 #define CONFIG_MMC_SDMA
 #define CONFIG_CMD_MMC
 #define CONFIG_DOS_PARTITION
 #define CONFIG_FS_FAT_MAX_CLUSTSIZE (8 * 1024)
 #define CONFIG_CMD_FAT
#endif /* RZN1_ENABLE_SDHC */

/***** USB Device aka Gadget aka Function *****/
#if defined(RZN1_ENABLE_USBF)
 #define CONFIG_USB_GADGET

 #define CONFIG_USBF_RENESAS

 #define CONFIG_DFU_FUNCTION
 #ifdef CONFIG_DFU_FUNCTION
 /* Default is... 8 MB !! Note that it needs to be AT LEAST the size of
  * a single erase block of the nand/qspi, otherwise the operation will fail */
 #define CONFIG_SYS_DFU_DATA_BUF_SIZE	(64 * 1024)
 #define CONFIG_SYS_DFU_HOARD_BUFFER
 #define DFU_DEFAULT_POLL_TIMEOUT	100

 #define CONFIG_CMD_DFU_EXT

 #define CONFIG_USBDOWNLOAD_GADGET		/* Needed by DFU */
 #define CONFIG_G_DNL_PRODUCT_NUM	0x0239	/* TODO: RZ/N1 Peripheral ID */

 #define CONFIG_DFU_RAM		/* DEBUG only */
 #if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_DFU_SPI
 #endif

 #define DFU_EXT_INFO \
	"dfu_ext_info=" \
	"spi sf_spl raw 0 10000;" \
	"spi sf_rpkgt raw 10000 10000;" \
	"spi sf_uboot raw 20000 80000;" \
	"spi sf_env raw a0000 10000;" \
	"spi sf_dtb raw b0000 20000;" \
	"spi sf_kernel raw d0000 600000;" \
	"spi sf_cm3 raw 6d0000 100000;" \
	"spi sf_data raw 7d0000 0;" \
	"spi sf_vxworks raw d0000 600000;" \
	"ram r_kernel ram 80008000 D80000;" \
	"ram r_vxworks ram 80008000 D80000\0"
 #else
 #define DFU_EXT_INFO
 #endif /* CONFIG_DFU_FUNCTION */
#else
 #define DFU_EXT_INFO
#endif /* RZN1_ENABLE_USBF */

#define CONFIG_BOOTDELAY 1
#define CONFIG_BOOTCOMMAND ""
/* Default environment variables */
#define CONFIG_EXTRA_ENV_SETTINGS \
	DFU_EXT_INFO \
	"bootdelay=1\0" \
	"loadaddr=80008000\0" \
	"ethaddr=00:00:0a:02:57:CD\0" \
	"eth1addr=00:00:0a:02:57:CE\0" \
	"netmask=255.255.255.0\0" \
	"serverip=192.168.1.30\0" \
	"ipaddr=192.168.1.50\0"


/***** SPL (Secondary Program Loader) *****/
#if defined(RZN1_ENABLE_SPL)

#define CONFIG_CMD_SPL
/* SPL build */
/* U-Boot/SPL is at the top of UA SRAM, out of the way of CM3 code which has to
 * be at the start of UA SRAM. These definitions setup SPL so that code, heap,
 * and stack at located together. */
#define CONFIG_SPL_MAX_FOOTPRINT	(80 * 1024)
#define CONFIG_SYS_SPL_MALLOC_SIZE	(40 * 1024)
#define CONFIG_SYS_SPL_STACK_SIZE	(8 * 1024)

#define CONFIG_SYS_SPL_SRAM_SIZE	(CONFIG_SPL_MAX_FOOTPRINT + \
						CONFIG_SYS_SPL_MALLOC_SIZE + \
						CONFIG_SYS_SPL_STACK_SIZE)
#define CONFIG_SPL_TEXT_BASE		0x040e0000 // (0x04100000 - CONFIG_SYS_SPL_SRAM_SIZE)

#define CONFIG_SYS_SPL_MALLOC_START	(CONFIG_SPL_TEXT_BASE + CONFIG_SPL_MAX_FOOTPRINT)
#define CONFIG_SPL_STACK		(CONFIG_SYS_SPL_MALLOC_START + CONFIG_SYS_SPL_MALLOC_SIZE + CONFIG_SYS_SPL_STACK_SIZE)


#if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_SPL_SPI_LOAD
#endif

#include "rzn1-spl.h"
#if defined(CONFIG_SPL_SPI_LOAD)
	#define CONFIG_SPL_SPI_BUS              0
	#define CONFIG_SPL_SPI_CS               0
	/* Offset corresponds to DFU sf_rpkgt */
	#define CONFIG_SYS_SPI_U_BOOT_OFFS      0x10000
#endif

/* Make SPL skip checking the signatures of loaded images */
/* WARNING! This is for development & testing only */
#define RZN1_SKIP_BOOTROM_CALLS

/*
 * This is the hash of the public key used to sign BLp wrapped images that is
 * loaded by U-Boot/SPL. If you are not verifying the signature of packages, it
 * will not be used.
 * This particular hash is key 6 from keys used during testing of the BootROM.
 */
#define CONFIG_SPL_RZN1_RPKG_HASH \
	0xf5, 0x05, 0x93, 0x54, \
	0xda, 0x47, 0x4f, 0xe4, \
	0x1f, 0x2c, 0x7d, 0xfe, \
	0xbb, 0x41, 0x0d, 0xed, \
	0xa3, 0x25, 0x3f, 0xef, \
	0xa7, 0x29, 0x0c, 0x28, \
	0x07, 0x1f, 0xbf, 0x76, \
	0x26, 0xa3, 0x7e, 0x4c,

#endif /* RZN1_ENABLE_SPL */

#endif /* __RZN1D400_DB_H */

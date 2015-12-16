/*
 * (C) Copyright 2016 Renesas Electronics Europe Ltd
 * Phil Edworthy <phil.edworthy@renesas.com>
 *
 * Configuration for Renesas RZN1 device on a SCIT FPGA board.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1_SCIT_H
#define __RZN1_SCIT_H

#include "rzn1-common.h"
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"

/*
 * The BootROM will start the 2nd core and execute WFE. On an event it will
 * read from the SYSCTRL BOOTADDR register. However, this register can only
 * be accessed from Secure mode. U-Boot switches to Non-secure mode by writing
 * the address of _smp_pen to BOOTADDR, then kicking the 2nd CPU. Normally, the
 * _smp_pen code switches mode, then runs the smp_waitloop code. Instead we
 * run secondary smp_waitloop code that uses a different PEN address.
 * If you use a different PEN2 address, Linux will also have to be changed.
 *
#define CONFIG_ARMV7_NONSEC
#define CONFIG_ARMV7_VIRT
#define CONFIG_SMP_PEN_ADDR		(RZN1_SYSTEM_CTRL_BASE + RZN1_SYSCTRL_REG_BOOTADDR)
#define CONFIG_SMP_PEN2_ADDR		(0x20000000)
*/

#define CONFIG_SYS_THUMB_BUILD
#define CONFIG_SYS_LONGHELP
#define	CONFIG_CMDLINE_EDITING
#define CONFIG_CMD_MISC
#define CONFIG_CMD_RUN
#define CONFIG_CMD_CACHE
#define CONFIG_CMD_BDI
#define CONFIG_CMD_TIMER
#define CONFIG_CMD_BOOTZ
#define	CONFIG_OF_LIBFDT
#define CONFIG_CMDLINE_TAG		/* enable passing of ATAGs  */

/* Local helper symbols to enable/disable functionality */
#define RZN1_ENABLE_QSPI
#define RZN1_ENABLE_NAND
#define RZN1_ENABLE_ETHERNET
#define RZN1_ENABLE_SDHC
#define RZN1_ENABLE_USBF
#define RZN1_ENABLE_SPL

/* Use SCIT special registers (where the SERCOS IP block should be) to control the ethernet PHY */
#define CONFIG_MACH_SCIT

/* SCIT FPGA specific clocks */
#undef CONFIG_SYS_CLK_FREQ
#define CONFIG_SYS_CLK_FREQ		16000000
#undef CONFIG_SYS_HZ_CLOCK
#define CONFIG_SYS_HZ_CLOCK		CONFIG_SYS_CLK_FREQ
#undef CONFIG_SYS_NS16550_CLK
#define CONFIG_SYS_NS16550_CLK		CONFIG_SYS_CLK_FREQ
#undef CONFIG_SYS_NAND_CLOCK
#define CONFIG_SYS_NAND_CLOCK		CONFIG_SYS_CLK_FREQ
#undef CONFIG_CQSPI_REF_CLK
#define CONFIG_CQSPI_REF_CLK		CONFIG_SYS_CLK_FREQ

/* SRAM */
#define CONFIG_SYS_TEXT_BASE		0x04000000
#define CONFIG_SYS_STAY_IN_SRAM
/* Very early stack, 256KB into SRAM */
#define CONFIG_SYS_INIT_SP_ADDR		(CONFIG_SYS_SRAM_BASE + (256 * 1024) - 4)
#define CONFIG_SYS_MALLOC_LEN		(320 * 1024)

/* DDR Memory */
#define CONFIG_CMD_MEMORY
#define CONFIG_CMD_MEMTEST
#define CONFIG_SYS_MEMTEST_START	(CONFIG_SYS_SDRAM_BASE)
#define CONFIG_SYS_MEMTEST_END		(CONFIG_SYS_MEMTEST_START + 32 * 1024 * 1024)
#define	CONFIG_SYS_ALT_MEMTEST
#define CONFIG_SYS_MEMTEST_SCRATCH	RZN1_RAM2MB_SYS_BASE

/* This is not used by uboot startup, just by nand etc */
#define CONFIG_SYS_LOAD_ADDR		0x81000000

/* Serial */
#define CONFIG_BAUDRATE			57600


/* ENV settings.
 * Note: if you have QSPI and NAND enabled, this will put the env in QSPI.
 * To avoid U-Boot accessing NAND/QSPI, you might want to set NOWHERE instead.
 */
#if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_ENV_IS_IN_SPI_FLASH
#elif defined(RZN1_ENABLE_NAND)
 #define CONFIG_ENV_IS_IN_NAND
#else
 #define CONFIG_ENV_IS_NOWHERE		/* Store ENV in memory only */
#endif

/* The ENV offsets match the DFU offsets for *_env */
#if defined(CONFIG_ENV_IS_IN_NAND)
 #define CONFIG_ENV_OFFSET		0x300000
#elif defined(CONFIG_ENV_IS_IN_SPI_FLASH)
 #define CONFIG_ENV_OFFSET		0x80000
#endif
#define CONFIG_ENV_SECT_SIZE		(128 * 1024)
#define CONFIG_ENV_SIZE			(8 * 1024)
#define CONFIG_CMD_SAVEENV


/***** SPI Flash *****/
#if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_CADENCE_QSPI
 #define CONFIG_CMD_SF
 #define CONFIG_SPI
 #define CONFIG_SF_DEFAULT_SPEED	104000000
 #define CONFIG_ENV_SPI_MAX_HZ		10000000

 #define CONFIG_CQSPI_DECODER		0	/* i.e. don't use 4-to-16 CS decoder */
 /* Device specific CS timing delays in nano seconds */
 #define CONFIG_CQSPI_TSHSL_NS		200	/* De-Assert */
 #define CONFIG_CQSPI_TSD2D_NS		255	/* De-Assert Different Slaves */
 #define CONFIG_CQSPI_TCHSH_NS		20	/* End Of Transfer */
 #define CONFIG_CQSPI_TSLCH_NS		20	/* Start Of Transfer */

 /* SPI Flash devices on SCIT:
  * @SSZ0: QSPI on SCIT-FPGA-MEM-01: 32MByte, 256Mbit, WinBond W25Q256FVFIG
  * @SSZ1: SPI on SCIT-FPGA-MEM-01: 4MByte, 32Mbit, Adesto AT45DB321E (DataFlash, not supported by U-Boot)
  * @SSZ2: QSPI on EB-SCIT-PERIPH: 32MByte, 256Mbit, Spansion S25FL256SAGMFI000 (optional)
  */
 #define CONFIG_SPI_FLASH
 #define CONFIG_SPI_FLASH_WINBOND
 #define CONFIG_SPI_FLASH_ATMEL
 #define CONFIG_SPI_FLASH_SPANSION
#endif /* RZN1_ENABLE_QSPI */


/***** NAND Flash *****/
#if defined(RZN1_ENABLE_NAND)
 /* NAND Flash devices on SCIT:
  * @CSZ0: on SCIT-FPGA-MEM-01: 512MByte, 256Mx16, 4GBit, MT29F4G16ABADAWP
  *        This is a 16-bit device, but the NAND Flash Controller is 8-bit only,
  *        so we ignore this device and hard code the CS to 1.
  * @CSZ1: on EB-SCIT-PERIPH: 128MByte, 128Mx8, 1Gbit, S34ML01G100 (optional)
  */
 #define CONFIG_CMD_NAND
 #define CONFIG_NAND_CADENCE_EVATRONIX_CS	1
 #define CONFIG_NAND_OOB_FS_BYTES	8 /* JFFS2 */
#endif /* RZN1_ENABLE_NAND */


/***** Ethernet *****/
#if defined(RZN1_ENABLE_ETHERNET)
 #define CONFIG_CMD_NET
 #define CONFIG_CMD_MII
 #define CONFIG_CMD_PING
 #define CONFIG_MII
 #define CONFIG_PHYLIB
 #define CONFIG_PHY_GIGE			/* Include GbE speed/duplex detection */
 #define CONFIG_PHY_VITESSE
 #define CONFIG_DESIGNWARE_ETH
 #define CONFIG_MT5PT_SWITCH
 /* If not using the 5pt switch, the MAC is connected to PHY with address 0.
  * With the 5pt switch, the MAC is connected to two PHYs with address 0 and 1.
  */
 #define CONFIG_PHY_ADDR		0
#endif /* RZN1_ENABLE_ETHERNET */


/***** SDHC *****/
#if defined(RZN1_ENABLE_SDHC)
 #define CONFIG_GENERIC_MMC
 #define CONFIG_MMC
 #define CONFIG_SDHCI
 #define CONFIG_RZN1_SDHCI0
 //#define CONFIG_RZN1_SDHCI1
 #define CONFIG_CMD_MMC
 #define CONFIG_DOS_PARTITION
 #define CONFIG_CMD_FAT
 #define CONFIG_FS_FAT_MAX_CLUSTSIZE (8 * 1024)
#endif /* RZN1_ENABLE_SDHC */


/***** USB Device aka Gadget aka Function *****/
#if defined(RZN1_ENABLE_USBF)
 #define CONFIG_USB_GADGET

 #define CONFIG_USBF_RENESAS

 #define CONFIG_DFU_FUNCTION
 #ifdef CONFIG_DFU_FUNCTION
 /* Default is... 8 MB !! Note that it needs to be AT LEAST the size of
  * a single erase block of the nand, otherwise the operation will fail */
 #define CONFIG_SYS_DFU_DATA_BUF_SIZE	(128 * 1024)
 #define CONFIG_SYS_DFU_HOARD_BUFFER

 #define CONFIG_CMD_DFU_EXT

 #define CONFIG_USBDOWNLOAD_GADGET		/* Needed by DFU */
 #define CONFIG_G_DNL_PRODUCT_NUM	0x0239	/* TODO: RZ/N1 Peripheral ID */

 #define CONFIG_DFU_RAM		/* DEBUG only */
 #if defined(RZN1_ENABLE_NAND)
 #define CONFIG_DFU_NAND
 #define CONFIG_MTD_PARTITIONS
 #define CONFIG_CMD_MTDPARTS
 #endif
 #if defined(RZN1_ENABLE_QSPI)
 #define CONFIG_DFU_SPI
 #endif

 #define DFU_EXT_INFO \
	"dfu_ext_info=" \
	"nand n_spl raw 0 100000;" \
	"nand n_rpkgt raw 100000 100000;" \
	"nand n_uboot raw 200000 100000;" \
	"nand n_env raw 300000 100000;" \
	"nand n_cm3 raw 400000 200000;" \
	"nand n_kernel1 raw 600000 1000000;" \
	"nand n_kernel2 raw 1600000 1000000;" \
	"nand n_data raw 2600000 0;" \
	"spi sf_spl raw 0 20000;" \
	"spi sf_rpkgt raw 20000 20000;" \
	"spi sf_uboot raw 40000 40000;" \
	"spi sf_env raw 80000 20000;" \
	"spi sf_cm3 raw a0000 20000;" \
	"spi sf_kernel raw c0000 1000000;" \
	"spi sf_data raw 10c0000 0;" \
	"spi:2 sf2_data raw 0 0;" \
	"ram r_kernel ram 80200000 D80000\0"
 #else
 #define DFU_EXT_INFO
 #endif /* CONFIG_DFU_FUNCTION */
#else
 #define DFU_EXT_INFO
#endif /* RZN1_ENABLE_USBF */

/* Default environment variables */
#define CONFIG_EXTRA_ENV_SETTINGS \
	DFU_EXT_INFO \
	"loadaddr=80200000\0"


/***** SPL (Secondary Program Loader) *****/
#if defined(RZN1_ENABLE_SPL)
#define CONFIG_CMD_SPL
/* SPL build */
/* U-Boot/SPL is at the top of UA SRAM, out of the way of CM3 code which has to
 * be at the start of UA SRAM. These definitions setup SPL so that code, heap,
 * and stack at located together. */
#define CONFIG_SPL_TEXT_BASE		0x040f0000
#define CONFIG_SPL_MAX_FOOTPRINT	(40 * 1024)
#define CONFIG_SYS_SPL_MALLOC_START	(CONFIG_SPL_TEXT_BASE + CONFIG_SPL_MAX_FOOTPRINT)
#define CONFIG_SYS_SPL_MALLOC_SIZE	(16 * 1024)
#define CONFIG_SPL_STACK		(CONFIG_SYS_SPL_MALLOC_START + CONFIG_SYS_SPL_MALLOC_SIZE + 8 * 1024)

/* The default is for SPL to load images from NAND.
 * We assume that if you have support for QSPI, you will want to load images
 * from QSPI. If this is not be the case, change these lines */
#if defined(RZN1_ENABLE_QSPI)
 /* Boot from QSPI */
 #define CONFIG_SPL_SPI_LOAD
#endif

#include "rzn1-spl.h"
#if defined(CONFIG_SPL_SPI_LOAD)
	#define CONFIG_SPL_SPI_BUS              0
	#define CONFIG_SPL_SPI_CS               0
	/* Offset corresponds to DFU sf_rpkgt */
	#define CONFIG_SYS_SPI_U_BOOT_OFFS      0x20000
#else
	/* Offset corresponds to DFU n_rpkgt */
	#define CONFIG_SYS_NAND_U_BOOT_OFFS     0x100000
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

#endif /* RZN1_SCIT_H */

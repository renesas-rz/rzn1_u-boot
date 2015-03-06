/*
 * SPL: Load multiple images.
 *
 * This code reads a Package Table from the boot media, NAND or QSPI, at an
 * offset defined by CONFIG_SYS_NAND_U_BOOT_OFFS or CONFIG_SYS_SPI_U_BOOT_OFFS
 * respectively. The Package Table provides information about the list of
 * images to load. These images can be either uImages or Renesas packages.
 *
 * Renesas packages use the BLP header (see BLpHeader_t) that provides
 * information on the size of the payload, the load address, execution address,
 * the public key used to sign the image, and the signature itself.
 * U-Boot/SPL includes a hash of the public key which is used to verify that
 * the public key in the BLP header is correct. This hash is defined using the
 * CONFIG_SPL_RZN1_RPKG_HASH symbol.
 *
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/io.h>
#include <common.h>
#include <nand.h>
#include <spl.h>
#include <spi_flash.h>
#include "crypto_api.h"
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"

DECLARE_GLOBAL_DATA_PTR;

#define sysctrl_readl(addr) \
	readl(RZN1_SYSTEM_CTRL_BASE + addr)
#define sysctrl_writel(val, addr) \
	writel(val, RZN1_SYSTEM_CTRL_BASE + addr)

#define MAX_TBL_ENTRIES	20
#define PKGT_MAGIC 0x504b4754	/* "PKGT" in big endian */

enum {
	UIMAGE = 0,		/* uImage */
	RENESAS_RPKG = 1,	/* Renesas Package (uses BLP header) */
};

enum {
	CORTEX_A7_1 = 0,	/* Cortex A7 - 1st core */
	CORTEX_A7_2 = 1,	/* Cortex A7 - 2nd core (currently unused) */
	CORTEX_M3 = 2,		/* Cortex M3 */
	DATA_ONLY = 3,		/* Data */
};

struct pkg_entry {
	uint32_t pkg_type;
	uint32_t cpu_core;
	uint32_t offset;	/* offset into the current media for the image */
};

struct pkg_table {
	uint32_t magic;		/* "PKGT" in big endian */
	uint32_t nr_entries;	/* number of pkg_table entries */
	uint32_t flags;		/* reserved for future use */
	struct pkg_entry entries[MAX_TBL_ENTRIES];
};

struct loaded_data_s {
	uint32_t addr;
	uint32_t size;
};

static struct loaded_data_s loaded_data[MAX_TBL_ENTRIES];
static int nr_loaded_data;

/* Hash of the public key that was used to sign the RPKG */
uint8_t rzn1_rpkg_public_key_hash[] = { CONFIG_SPL_RZN1_RPKG_HASH };

#if defined(CONFIG_SPL_SPI_LOAD)
static BLpHeader_t blp_header_struct;
static void *blp_header = (void *)&blp_header_struct;
#else
/* Re-use intermediate storage for NAND page */
extern u32 nand_page[];
static void *blp_header = nand_page;
#endif

static int addr_ok(uint32_t addr, uint32_t low, uint32_t limit)
{
	if ((addr < low) || (addr >= (low + limit)))
		return 0;

	return 1;
}

/* Check that the address range is _completely_ inside another range */
static int range_ok(uint32_t addr, uint32_t size, uint32_t low, uint32_t limit)
{
	if (addr_ok(addr, low, limit) && addr_ok(addr + size - 1, low, limit))
		return 1;

	return 0;
}

/* Check that the address range is _partially_ inside another range */
static int range_conflicts(uint32_t addr, uint32_t size, uint32_t low, uint32_t limit)
{
	if (addr_ok(addr, low, limit) || addr_ok(addr + size - 1, low, limit))
		return 1;

	return 0;
}

/* Use the BootROM code to check the signature */
static int rpkg_verify_sig(BLpHeader_t *pBLp_Header, void *pPayload, uint32_t exec_offset)
{
	boot_rom_api_t *pAPI = (boot_rom_api_t *)CRYPTO_API_ADDRESS;
	SB_StorageArea_t StorageArea;
	uint32_t ret;

	debug("%s: pBLp_Header %p, pPayload %p, exec_offset %d\n", __func__, pBLp_Header, pPayload, exec_offset);

	ret = pAPI->crypto_init();
	if (ret != 0) {
		printf("%s: failed to initialise!\n", __func__);
		return -1;
	}
	debug("%s: Call to crypto_init completed!\n", __func__);

	ret = pAPI->ecdsa_verify(pBLp_Header,
			pPayload,
			exec_offset,
			&StorageArea,
			(void *)&rzn1_rpkg_public_key_hash);
	if (ret != ECDSA_VERIFY_STATUS_VERIFIED) {
		printf("%s: failed to verify image (%d)!\n", __func__, ret);
		return -2;
	}
	debug("%s: verified!\n", __func__);

	return 0;
}

static int parse_blp_header(BLpHeader_t *hdr, struct spl_image_info *info)
{
	uint32_t tmp;
	uint32_t load_addr;
	uint32_t exec_offs;
	uint32_t size;
	uint32_t valid_addr = 0;
	int i;

	/* Check the ImageAttributes are what we expect */
	tmp = __be32_to_cpu(hdr->Custom_attribute_Load_Address_type_ID);
	if (tmp != 0x80000001) {
		printf("%s: Bad BLP Load Address ID = 0x%x\n", __func__,
		       hdr->Custom_attribute_Load_Address_type_ID);
		return 0;
	}
	tmp = __be32_to_cpu(hdr->Custom_attribute_Execution_Offset_type_ID);
	if (tmp != 0x80000002) {
		printf("%s: Bad BLP Execution Offset ID = 0x%x\n", __func__,
		       hdr->Custom_attribute_Execution_Offset_type_ID);
		return 0;
	}

	load_addr = __be32_to_cpu(hdr->Custom_attribute_Load_Address_value_Big_Endian);
	exec_offs = __be32_to_cpu(hdr->Custom_attribute_Execution_Offset_value_Big_Endian);
	size = __be32_to_cpu(hdr->ImageLen);
	debug("%s: load addr 0x%x, size %d, exec addr 0x%x\n", __func__, load_addr, size, exec_offs);

	/*
	 * Perform some basic checks on the address range.
	 * This is not exhaustive as the boot allows Cortex M3 code to be loaded
	 * and started before loading other images. The Cortex M3 code could do
	 * pretty much anything...
	 */

	/* Check the image destination is a valid memory region */
#if defined(CONFIG_CADENCE_DDR_CTRL)
	valid_addr |= range_ok(load_addr, size, CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
#endif
	valid_addr |= range_ok(load_addr, size, RZN1_RAM2MB_ID_BASE, RZN1_RAM2MB_ID_SIZE);
	valid_addr |= range_ok(load_addr, size, RZN1_RAM2MB_SYS_BASE, RZN1_RAM2MB_SYS_SIZE);

	/* Check the image destination isn't overwriting us */
	valid_addr &= !range_conflicts(load_addr, size, CONFIG_SPL_TEXT_BASE, CONFIG_SPL_MAX_FOOTPRINT);
	valid_addr &= !range_conflicts(load_addr, size, CONFIG_SYS_SPL_MALLOC_START, CONFIG_SYS_SPL_MALLOC_SIZE);
	valid_addr &= !range_conflicts(load_addr, size, CONFIG_SPL_STACK, CONFIG_SPL_STACK + gd->start_addr_sp);

	/*
	 * Check the image destination is still available.
	 * We try to prevent someone overwriting part of a signed image with
	 * data from another signed image. Note that we can't check that the
	 * code uses an area of memory that is in use by another image, nor
	 * would we want to as it could be used for comms between processors.
	 */
	for (i = 0; i < nr_loaded_data; i++)
		valid_addr &= !range_conflicts(load_addr, size, loaded_data[i].addr, loaded_data[i].size);

	info->load_addr = load_addr;
	info->entry_point = load_addr + exec_offs;
	info->size = size;

	return valid_addr;
}

#if defined(CONFIG_SPL_SPI_LOAD)
static int spi_load_rpkg_header(struct spi_flash *flash, u32 offset)
{
	return spi_flash_read(flash, offset, sizeof(BLpHeader_t), blp_header);
}

static int spi_load_rpkg_payload(struct spi_flash *flash, u32 offset)
{
	return spi_flash_read(flash, offset + sizeof(BLpHeader_t),
			spl_image.size, (void *)spl_image.load_addr);
}
#else
static int nand_load_rpkg_header(u32 offset)
{
	return nand_spl_load_image(offset, sizeof(BLpHeader_t), blp_header);
}

static int nand_load_rpkg_payload(u32 offset)
{
	return nand_spl_load_image(offset + sizeof(BLpHeader_t),
		spl_image.size, (void *)spl_image.load_addr);
}
#endif

static void jump_to_image(u32 exec_addr, void *table, int index)
{
	typedef void __noreturn (*image_entry_noargs_t)(void *table, int index);
	image_entry_noargs_t image_entry = (image_entry_noargs_t)exec_addr;
	int valid_addr = 0;

	/* Check the execution addr is a valid memory region */
#if defined(CONFIG_CADENCE_DDR_CTRL)
	valid_addr |= addr_ok(load_addr, CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
#endif
	valid_addr |= addr_ok(load_addr, RZN1_RAM2MB_ID_BASE, RZN1_RAM2MB_ID_SIZE);
	valid_addr |= addr_ok(load_addr, RZN1_RAM2MB_SYS_BASE, RZN1_RAM2MB_SYS_SIZE);

	if (!valid_addr) {
		printf("U-Boot SPL: Attempting to execute CA7 outside of RAM!\n");
		return;
	}

	cleanup_before_linux();

	image_entry(table, index);
	/* Should not return! */
}

static void stop(void)
{
	hang();

	/* Just in case hang() doesn't work */
	while (1)
		;
}

/* Depending on OTP values in the device, the Cortex M3 will either start
 * running code from QSPI or from the start of SRAM.
 */
static int cm3_runs_from_qspi(void)
{
	if (sysctrl_readl(RZN1_SYSCTRL_REG_OPMODE) & (1 << 4))
		return 0;

	return 1;
}

/* Start the Cortex M3 running, if the device is set to run from SRAM, it's
 * entry point is always the start of SRAM at 0x04000000. Note that the CM3
 * actually starts execution at address 0x0, but this is mirrored to the
 * start of SRAM.
 */
static void start_cm3(void)
{
	debug("%s: Starting the CM3\n", __func__);
	sysctrl_writel(0x3, RZN1_SYSCTRL_REG_PWRCTRL_CM3);
}

static void pkgt_msg(int i, struct pkg_entry *entry, char *msg, int details)
{
	printf("U-Boot SPL: Error with PKG Table entry %d (%s).\n", i, msg);
	printf("    PKG media offset 0x%08x ", entry->offset);
	if (entry->cpu_core == DATA_ONLY)
		printf("(Data)\n");
	else if (entry->cpu_core == CORTEX_A7_1)
		printf("(CA7 code)\n");
	else if (entry->cpu_core == CORTEX_M3)
		printf("(CM3 code)\n");
	if (details)
		printf("    PKG load address 0x%08x, size 0x%08x\n", spl_image.load_addr, spl_image.size);
	printf("    Skipping this entry...\n");
}

static int load_rpkg(struct spi_flash *flash, int i, struct pkg_entry *entry, int verify)
{
	int ret;

	/* Check that the CM3 can run code */
	if (entry->cpu_core == CORTEX_M3 && cm3_runs_from_qspi()) {
		pkgt_msg(i, entry, "CM3 is using QSPI", 0);
		return -1;
	}

	/* Load the RPKG header */
#if defined(CONFIG_SPL_SPI_LOAD)
	ret = spi_load_rpkg_header(flash, entry->offset);
#else
	ret = nand_load_rpkg_header(entry->offset);
#endif
	if (ret) {
		pkgt_msg(i, entry, "I/O loading header", 0);
		return -2;
	}

	/* Check the RPKG header */
	if (!parse_blp_header(blp_header, &spl_image)) {
		pkgt_msg(i, entry, "Address/size is not allowed", 1);
		return -3;
	}

	/* Check that CM3 images are at the correct place */
	if (entry->cpu_core == CORTEX_M3 &&
	    (spl_image.load_addr != RZN1_RAM2MB_ID_BASE ||
	     spl_image.entry_point != RZN1_RAM2MB_ID_BASE)) {
		pkgt_msg(i, entry, "Invalid CM3 address", 1);
		return -4;
	}

	/* Load the payload */
#if defined(CONFIG_SPL_SPI_LOAD)
	ret = spi_load_rpkg_payload(flash, entry->offset);
#else
	ret = nand_load_rpkg_payload(entry->offset);
#endif
	if (ret) {
		pkgt_msg(i, entry, "I/O loading payload", 1);
		return -5;
	}

	if (verify) {
		/* Check the signature */
		ret = rpkg_verify_sig(blp_header,
				      (void *)spl_image.load_addr,
				spl_image.entry_point - spl_image.load_addr);
		if (ret) {
			pkgt_msg(i, entry, "Verification failed", 1);
			return -6;
		}
	}

	return 0;
}

void spl_load_multi_images(void)
{
	struct pkg_table table_data;
	struct pkg_table *table = &table_data;
	struct pkg_entry *entry;
#if defined(CONFIG_SPL_SPI_LOAD)
	struct spi_flash *flash;
#endif
#if !defined(RZN1_SKIP_BOOTROM_CALLS)
	boot_rom_api_t *pAPI = (boot_rom_api_t *)CRYPTO_API_ADDRESS;
	uint32_t state;
#endif
	uint32_t ca7_addr = 0;
	int ca7_index = -1;
	int verify = 0;
	int i;

#if !defined(RZN1_SKIP_BOOTROM_CALLS)
	/*
	 * Get the BootROM to tell us if it's possible to, and if we need to,
	 * verify the signature of the images.
	 */
	state = pAPI->read_security_state();
	if ((state & 0x1) && (state & 0x2)) {
		printf("U-Boot SPL: Error: Requires secure boot, but not available\n");
		stop();
	}
	if (state & 0x1) {
		debug("%s: Secure boot required\n", __func__);
		verify = 1;
	}

#if defined(RZN1_FORCE_VERIFY_USING_BOOTROM)
	/* TODO: Forcing verify=1 as it can't be set on SCIT */
	verify = 1;
#endif
#endif

	/* Read PKG Table from current media */
#if defined(CONFIG_SPL_SPI_LOAD)
	flash = spl_spi_probe();
	spi_flash_read(flash, CONFIG_SYS_SPI_U_BOOT_OFFS,
		       sizeof(struct pkg_table), (void *)table);
#else
	spl_nand_load_init();
	nand_spl_load_image(CONFIG_SYS_NAND_U_BOOT_OFFS,
			    sizeof(struct pkg_table), (void *)nand_page);
	memcpy(table, nand_page, sizeof(struct pkg_table));
#endif

	/*
	 * If we don't need to verify the image signature and we don't have a
	 * PKG Table, assume we are loading a uImage.
	 */
	if (!verify && table->magic != PKGT_MAGIC) {
		debug("%s: No PKG Table, assuming uImage\n", __func__);
#if defined(CONFIG_SPL_SPI_LOAD)
		spl_spi_load_one_uimage(flash, CONFIG_SYS_SPI_U_BOOT_OFFS);
#else
		spl_nand_load_one_uimage(CONFIG_SYS_NAND_U_BOOT_OFFS);
#endif
		jump_to_image_no_args(&spl_image);
	}

	if (table->magic != PKGT_MAGIC) {
		printf("U-Boot SPL: Error: PKG Table does not have correct ID!\n");
		stop();
	}
	if (table->nr_entries == 0) {
		printf("U-Boot SPL: Error: PKG Table does not have any entries!\n");
		stop();
	}
	if (table->nr_entries > MAX_TBL_ENTRIES) {
		printf("U-Boot SPL: Error: PKG Table has too many entries, max %d!\n", MAX_TBL_ENTRIES);
		stop();
	}

	nr_loaded_data = 0;

	for (i = 0; i < table->nr_entries; i++) {
		entry = &table->entries[i];
		debug("%s: PKGT[%d] offset 0x%x, type %d, cpu %d\n", __func__,
		      i, entry->offset, entry->pkg_type, entry->cpu_core);

		if (entry->cpu_core != CORTEX_A7_1 &&
		    entry->cpu_core != CORTEX_M3 &&
		    entry->cpu_core != DATA_ONLY) {
			pkgt_msg(i, entry, "Unsupported cpu/data", 0);
			continue;
		}

		if (entry->pkg_type == UIMAGE) {
			if (verify) {
				pkgt_msg(i, entry, "Secure mode cannot load uImage", 0);
				continue;
			}
#if defined(CONFIG_SPL_SPI_LOAD)
			spl_spi_load_one_uimage(flash, entry->offset);
#else
			spl_nand_load_one_uimage(entry->offset);
#endif
		} else if (entry->pkg_type == RENESAS_RPKG) {
#if defined(CONFIG_SPL_SPI_LOAD)
			int ret = load_rpkg(flash, i, entry, verify);
#else
			int ret = load_rpkg(NULL, i, entry, verify);
#endif
			if (ret)
				continue;
		} else {
			pkgt_msg(i, entry, "Unsupported pkg type", 0);
			continue;
		}

		/* Record the image info */
		loaded_data[nr_loaded_data].addr = spl_image.load_addr;
		loaded_data[nr_loaded_data].size = spl_image.size;
		nr_loaded_data++;

		/* Record CA7 info for later */
		if (entry->cpu_core == CORTEX_A7_1) {
			ca7_addr = spl_image.load_addr;
			ca7_index = i;
		}

		/* Start CM3 if needed */
		if (entry->cpu_core == CORTEX_M3)
			start_cm3();
	}

	if (ca7_index >= 0)
		jump_to_image(ca7_addr, &table, ca7_index);

	printf("U-Boot SPL: Error: PKG Table has not started Cortex A7.\n");
	stop();
}

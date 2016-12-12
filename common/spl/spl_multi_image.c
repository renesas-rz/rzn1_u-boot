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
 * RZN1_SPL_RPKG_HASH symbol.
 *
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/gic.h>
#include <asm/io.h>
#include <common.h>
#include <fdt_support.h>
#include <libfdt.h>
#include <nand.h>
#include <spl.h>
#include <spi_flash.h>
#include "crypto_api.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"
#include "renesas/rzn1-utils.h"

#if defined(CONFIG_ARMV7_NONSEC) || defined(CONFIG_ARMV7_VIRT)
#include <asm/armv7.h>
#include <asm/secure.h>
#endif
#if defined(CONFIG_SPL_ENV_SUPPORT)
#include <environment.h>
#endif

#include "pkgt_table.h"
#include "spkg_header.h"

DECLARE_GLOBAL_DATA_PTR;

/* The top part of Sys SRAM is used by crypto HW when verifying signatures */
#define CRYPTO_SRAM_USED	(64 * 1024)

#define sysctrl_readl(addr) \
	readl(RZN1_SYSTEM_CTRL_BASE + addr)
#define sysctrl_writel(val, addr) \
	writel(val, RZN1_SYSTEM_CTRL_BASE + addr)

static u8 default_load_source;
#if defined(RZN1_ENABLE_QSPI)
struct spi_flash *flash;
#endif
#if defined(RZN1_ENABLE_NAND)
/*
 * When reading data from a NAND Flash device, the minimum we can read is a
 * page. SPL will be able to load multiple images to arbitrary locations, so
 * we cannot make any assumptions about what areas are available to use.
 * Therefore, we need storage inside the SPL image.
 */
static u32 nand_page[8192/4];
static struct image_header *header = (struct image_header *)nand_page;
#endif

#ifndef IH_MAGIC
#define IH_MAGIC	0x27051956	/* Image Magic Number		*/
#endif

struct spl_image_info spl_image;

struct loaded_data_t {
	struct pkg_entry *part;
	u32 load_addr;
	u32 entry_point;
	u32 size, mapped;
};

/* indexed with PKGT_CORE_CA70/CA71/CM3 and PKGT_KIND_CODE/DTB/DATA */
static struct loaded_data_t loaded_core[PKGT_CORE_END][PKGT_KIND_END];

/* Hash of the public key that was used to sign the RPKG */
static const uint8_t rzn1_rpkg_public_key_hash[] = { RZN1_SPL_RPKG_HASH };

/* if NAND exists, we can use the static page they use to load pages */
#if defined(RZN1_ENABLE_NAND)
/* Re-use intermediate storage for NAND page */
extern u32 nand_page[];
static struct spkg_file *spkg = (struct spkg_file *)nand_page;
#else
static struct spkg_file spkg_header_struct;
static struct spkg_file *spkg = &spkg_header_struct;
#endif

enum pkg_entry_errors {
	SUCCESS = 0,
	CRYPTO_INIT_FAILED = -1,
	SIGNATURE_VERIFY_FAILED = -2,
	UNSUPPORTED_BLP_HEADER = -3,
	INVALID_ADDR_SIZE = -4,
	MEDIA_FAILED = -5,
	BAD_HEADER_CRC = -6,
	CANT_LOAD_SECURE_UIMAGE = -7,
	BAD_CPU_INDEX = -8,
	BAD_PACKAGE_KIND = -9,
	BAD_PACKAGE_TYPE = -10,
	BAD_PACKAGE_SRC = -11,
	MISSING_SPKG_HDR = -12,
};

static const char * pkg_entry_error_str[] = {
	"Success",
	"Failed to initialise crypto",
	"Signature verification failed",
	"Unsupported BLp header",
	"Invalid address and/or size",
	"QSPI or NAND read failed",
	"Bad CRC in header",
	"Secure mode cannot load uImage",
	"Unsupported cpu index",
	"Unsupported package kind",
	"Unsupported package type",
	"Unsupported package source",
	"SPKG header not found",
};

static const char *get_err_str(int err)
{
	return pkg_entry_error_str[-err];
}

static int addr_ok(u32 addr, u32 low, u32 limit)
{
	if ((addr < low) || (addr >= (low + limit)))
		return 0;

	return 1;
}

/* Check that the address range is _completely_ inside another range */
static int range_ok(u32 addr, u32 size, u32 low, u32 limit)
{
	if (addr_ok(addr, low, limit) && addr_ok(addr + size - 1, low, limit))
		return 1;

	return 0;
}

/* Check that the address range is _partially_ inside another range */
static int range_conflicts(u32 addr, u32 size, u32 low, u32 limit)
{
	if (addr_ok(addr, low, limit) || addr_ok(addr + size - 1, low, limit))
		return 1;

	return 0;
}

static __noreturn void stop(void)
{
	hang();
	/* Just in case hang() doesn't work */
	while (1)
		;
}

static const char * const pkg_type[] = { "uImage", "RPKG", "SPKG", "raw"};
static const char * const pkg_core[] = { "CA7 #0", "CA7 #1", "CM3" };
static const char * const pkg_kind[] = { "code", "dtb", "data" };
static const char * const pkg_src[] = { "(same)", "QSPI", "NAND" };

static void pkgt_msg(const struct pkg_entry * const entry, const char *msg, int details)
{
	printf("SPL: Error with PKG Table entry (%s).\n", msg);
	printf("    PKG media offset 0x%08x ", entry->offset);

	printf("%s %s %s in %s\n",
	       pkg_core[PKG_CORE(entry)], pkg_kind[PKG_KIND(entry)],
		pkg_type[PKG_TYPE(entry)], pkg_src[PKG_SRC(entry)]);
	if (details)
		printf("    PKG load address 0x%08x, size 0x%08x\n",
		       spl_image.load_addr, spl_image.size);
}


/* Depending on OTP values in the device, the Cortex M3 will either start
 * running code from QSPI or from the start of SRAM.
 */
static int cm3_runs_from_qspi(void)
{
	if (sysctrl_readl(RZN1_SYSCTRL_REG_OPMODE) & (1 << 4))
		return 1;

	return 0;
}

/* Align on the next 16MB boundary */
#define MAP_ALIGNMENT	(16 * 1024 * 1024)

/* if the code for a core is loaded, add size (and alignment, and a bit extra)
 * to it's size, for relocating DTB, initramfs etc */
static u32 reserve_core_range(int core, u32 size)
{
	struct loaded_data_t *code = &loaded_core[core][PKGT_KIND_CODE];
	/* round load address to 4K */
	u32 dest = (code->load_addr + code->size + code->mapped + (MAP_ALIGNMENT-1)) &
				~(MAP_ALIGNMENT-1);

	/* add at least 4KB, and round */
	size = (size + 8192) & ~4095;

	if (!code->load_addr)
		return 0;

	debug("%s core %d reserve %d at %08x\n", __func__, core, size, dest);

	code->mapped += size;

	return dest;
}

/* Load data from the flash specified by the table entry */
static int source_load(const struct pkg_entry * const entry,
	u32 offset, size_t size, void *dst)
{
	int ret = SUCCESS;

	switch (PKG_SRC(entry)) {
#if defined(RZN1_ENABLE_QSPI)
	case PKGT_SRC_QSPI:
		if (!flash)
			flash = spl_spi_probe();
		ret = spi_flash_read(flash, offset, size, dst);
		break;
#endif
#if defined(RZN1_ENABLE_NAND)
	case PKGT_SRC_NAND:
		ret = nand_spl_load_image(offset, size, dst);
		break;
#endif
	}
	if (ret != 0)
		return MEDIA_FAILED;

	return SUCCESS;
}

/* Use the BootROM code to check the signature */
static int verify_signature(
	const BLpHeader_t * const pBLp_Header, const void * const pPayload,
	u32 exec_offset)
{
	boot_rom_api_t *pAPI = (boot_rom_api_t *)CRYPTO_API_ADDRESS;
	SB_StorageArea_t StorageArea;
	u32 ret;

	debug("%s: pBLp_Header %p, pPayload %p, exec_offset %d\n", __func__,
	      pBLp_Header, pPayload, exec_offset);

	rzn1_clk_set_gate(RZN1_HCLK_CRYPTO_EIP93_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_CRYPTO_EIP150_ID, 1);

	if (pAPI->crypto_init() != 0)
		return CRYPTO_INIT_FAILED;

	ret = pAPI->ecdsa_verify(pBLp_Header,
			pPayload,
			exec_offset,
			&StorageArea,
			(void *)&rzn1_rpkg_public_key_hash);
	if (ret != ECDSA_VERIFY_STATUS_VERIFIED)
		return SIGNATURE_VERIFY_FAILED;

	return SUCCESS;
}

/* Returns ZERO if range is valid */
static int validate_load_range(const struct loaded_data_t * const info)
{
	struct pkg_entry *entry = info->part;
	int i,j;
	int valid_addr = 0;

	/*
	 * Perform some basic checks on the address range.
	 * This is not exhaustive as the boot allows Cortex M3 code to be loaded
	 * and started before loading other images. The Cortex M3 code could do
	 * pretty much anything...
	 */
	debug("%s: load addr 0x%x exec addr 0x%x size %d\n", __func__,
	      info->load_addr, info->entry_point-info->load_addr, info->size);

	/* Are CM3 images at the correct place and can CM3 run them? */
	if (PKG_CORE(entry) == PKGT_CORE_CM3 &&
	    (info->load_addr != RZN1_SRAM_ID_BASE ||
	     info->entry_point != RZN1_SRAM_ID_BASE ||
	     cm3_runs_from_qspi()))
		return INVALID_ADDR_SIZE;

	/* Check the image destination is a valid memory region */
#if defined(CONFIG_SYS_SDRAM_BASE)
	/* This is DDR on RZ/N1D, but SRAM on RZ/N1S and RZ/N1L */
	valid_addr |= range_ok(info->load_addr, info->size,
				CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
#endif
	valid_addr |= range_ok(info->load_addr, info->size,
				RZN1_SRAM_ID_BASE, RZN1_SRAM_ID_SIZE);
	valid_addr |= range_ok(info->load_addr, info->size,
				RZN1_SRAM_SYS_BASE, RZN1_SRAM_SYS_SIZE - CRYPTO_SRAM_USED);

	/* Check the image destination isn't overwriting us */
	valid_addr &= !range_conflicts(info->load_addr, info->size,
				CONFIG_SPL_TEXT_BASE, CONFIG_SPL_MAX_FOOTPRINT);
	valid_addr &= !range_conflicts(info->load_addr, info->size,
				CONFIG_SYS_SPL_MALLOC_START, CONFIG_SYS_SPL_MALLOC_SIZE);
	valid_addr &= !range_conflicts(info->load_addr, info->size,
				CONFIG_SPL_STACK, CONFIG_SPL_STACK + gd->start_addr_sp);
	/*
	 * Check the image destination is still available.
	 * We try to prevent someone overwriting part of a signed image with
	 * data from another signed image. Note that we can't check that the
	 * code uses an area of memory that is in use by another image, nor
	 * would we want to as it could be used for comms between processors.
	 */
	for (i = 0; i < PKGT_CORE_END; i++)
		for (j = 0; j < PKGT_KIND_END; j++)
			if ((info != &loaded_core[i][j]) && /* <- not us! */
			    loaded_core[i][j].size) {
				valid_addr &= !range_conflicts(
						info->load_addr, info->size,
						loaded_core[i][j].load_addr,
						loaded_core[i][j].size);
			}
	return valid_addr ? SUCCESS : INVALID_ADDR_SIZE;
}

static int parse_rpkg_header(BLpHeader_t *hdr, struct loaded_data_t *info)
{
	u32 tmp;

	/* Check the ImageAttributes are what we expect */
	tmp = __be32_to_cpu(hdr->Custom_attribute_Load_Address_type_ID);
	if (tmp != 0x80000001)
		return UNSUPPORTED_BLP_HEADER;
	info->load_addr = __be32_to_cpu(hdr->Custom_attribute_Load_Address_value_Big_Endian);
	info->entry_point = info->load_addr;

	/* Assume the execution offset is 0 if not present */
	tmp = __be32_to_cpu(hdr->Custom_attribute_Execution_Offset_type_ID);
	if (tmp == 0x80000002)
		info->entry_point += __be32_to_cpu(hdr->Custom_attribute_Execution_Offset_value_Big_Endian);

	info->size = __be32_to_cpu(hdr->ImageLen);

	return validate_load_range(info);
}

/* Passed an array of SPKG_HEADER_COUNT headers, return the first
 * one that has a valid signature and CRC.
 * Return NULL if none of them work out
 */
static struct spkg_hdr *locate_valid_spkg_header(struct spkg_hdr *h)
{
	int i;

	for (i = 0; i < SPKG_HEADER_COUNT; i++, h++) {
		if (h->signature == SPKG_HEADER_SIGNATURE &&
		    crc32(0, (u8*)h, sizeof(*h)-sizeof(h->crc)) == h->crc) {
			if (i)
				debug("%s skipped %d invalid headers\n",
				      __func__, i);
			return h;
		}
	}
	return NULL;
}

static int parse_spkg_header(struct spkg_hdr *h, struct loaded_data_t *info)
{
	h = locate_valid_spkg_header(h);
	if (!h)
		return MISSING_SPKG_HDR;

	info->load_addr = h->load_address;
	info->entry_point = info->load_addr + h->execution_offset;
	info->size = (h->payload_length >> 8) - SPKG_BLP_SIZE;

	/* Initramfs/DTB with a zero load address means to load it just after
	 * the code for that core, if it's available etc */
	if ((PKG_IS_INITRAMFS(info->part) ||  PKG_KIND(info->part) == PKGT_KIND_DTB)
			&& info->load_addr == 0) {
		/* round */
		u32 size = (info->size + 4095) & ~4095;

		/* set our relocated address */
		h->load_address = info->load_addr =
			reserve_core_range(PKG_CORE(info->part), size);

		/* not really needed as it's data, but adjust anyway */
		info->entry_point += info->load_addr;
	}

	return validate_load_range(info);
}

static int load_spkg_header(const struct pkg_entry * const entry)
{
	return source_load(entry, entry->offset, sizeof(*spkg), spkg);
}
static int load_rpkg_header(const struct pkg_entry * const entry)
{
	return source_load(entry, entry->offset,
			sizeof(BLpHeader_t), spkg->blp);
}
static int load_payload(const struct loaded_data_t * const part, u32 payload_offset)
{
	return source_load(part->part,
			part->part->offset + payload_offset,
			part->size,
			(void *)part->load_addr);
}

int __weak ft_board_setup(void *blob, bd_t *bd)
{
	return 0;
}

#ifdef CONFIG_ARMV7_NONSEC
/* this is used by the secondary core bootstrapper to know if it should
 * kick off in HYP mode or in plain NONSEC */
extern int nonsec_and_hyp;
#endif

static void jump_to_image(
	struct loaded_data_t *code,
	struct loaded_data_t *dtb,
	struct loaded_data_t *data)
{
	int valid_addr = 0;

	/* Check the execution addr is a valid memory region */
#if defined(CONFIG_SYS_SDRAM_BASE)
	/* This is DDR on RZ/N1D, but SRAM on RZ/N1S and RZ/N1L */
	valid_addr |= addr_ok(code->entry_point, CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
#endif
	valid_addr |= addr_ok(code->entry_point, RZN1_SRAM_ID_BASE, RZN1_SRAM_ID_SIZE);
	valid_addr |= addr_ok(code->entry_point, RZN1_SRAM_SYS_BASE, RZN1_SRAM_SYS_SIZE);
	/* It is also possible to start code straight from the QSPI mapping */
	valid_addr |= addr_ok(code->entry_point, RZN1_V_QSPI_BASE, RZN1_V_QSPI_SIZE);

	if (!valid_addr) {
		printf("SPL: Invalid CA7 entry point (%08x)!\n",
		       code->entry_point);
		return;
	}

	spl_image.entry_point = code->entry_point;
	spl_image.size = code->size;

	if (dtb->load_addr && fdt_check_header((void *)dtb->load_addr) == 0) {
		void *dtb_mapped = (void*)dtb->load_addr;

		debug("Original DTB is %d size for real\n",
			fdt_totalsize(dtb_mapped));
		if (dtb->size == 0)
			dtb->size = fdt_totalsize(dtb_mapped);

		/* if DTB is pointing to the QSPI mapping, it needs relocating */
		if (addr_ok(dtb->load_addr, RZN1_V_QSPI_BASE, RZN1_V_QSPI_SIZE)) {
			dtb_mapped = (void*)reserve_core_range(PKG_CORE(dtb->part),
						dtb->size);
		}
		/* make sure we got a bit more room to do stuff in that block */
		fdt_open_into((void*)dtb->load_addr, dtb_mapped,
			      dtb->size + 4096);
		dtb->load_addr = (u32)dtb_mapped;

		fdt_find_and_setprop(dtb_mapped,
				     "/chosen", "rzn1,spl",
				     &valid_addr, sizeof(valid_addr), 1);
		if (PKG_IS_BACKUP(code->part)) {
			fdt_find_and_setprop(dtb_mapped,
					     "/chosen", "rzn1,backup",
					     &valid_addr, sizeof(valid_addr), 1);
		}
		const char *boot_source = default_load_source == PKGT_SRC_QSPI ?
					"qspi" : default_load_source == PKGT_SRC_NAND ?
					"nand" : "unknown";
		fdt_find_and_setprop(dtb_mapped,
				     "/chosen/", "rzn1,boot-source",
				     boot_source, strlen(boot_source) + 1, 1);
		fdt_find_and_setprop(dtb_mapped,
				     "/chosen", "rzn1,flash-offset",
				     &code->part->offset,
				     sizeof(code->part->offset), 1);

		const char *cmdline = getenv("bootargs");
		if (cmdline != NULL)
			fdt_find_and_setprop(dtb_mapped,
				     "/chosen/", "bootargs",
				     cmdline, strlen(cmdline) + 1, 1);

#if defined(CONFIG_SYS_SDRAM_BASE)
		fdt_fixup_memory(dtb_mapped,
				 CONFIG_SYS_SDRAM_BASE, gd->ram_size);
#endif

		fdt_fixup_ethernet(dtb_mapped);

		if (data && data->load_addr && PKG_IS_INITRAMFS(data->part)) {
			fdt_initrd(dtb_mapped,
				   data->load_addr, data->load_addr + data->size);
		}
	} else {
		if (dtb->load_addr)
			pkgt_msg(code->part, "Invalid DTB", 0);
		dtb->load_addr = 0;
	}

	debug("%s %08x(%08x)\n", __func__, code->entry_point, dtb->load_addr);

	if (PKG_IS_NONSEC(code->part)) {
#ifdef CONFIG_ARMV7_NONSEC
		unsigned long machid = 0xffffffff;
		unsigned long r2 = dtb->load_addr;

		nonsec_and_hyp = !!PKG_IS_HYP(code->part);

		/* if we also want hyp mode, add it to the device tree */
		if (nonsec_and_hyp)
			fdt_find_and_setprop((void *)dtb->load_addr,
					     "/chosen", "rzn1,hyp",
					     &nonsec_and_hyp, sizeof(nonsec_and_hyp), 1);
		ft_board_setup((void *)dtb->load_addr, NULL);

		/* Do not remove or move this line! */
		cleanup_before_linux();

		if (armv7_init_nonsec() == 0) {
			if (nonsec_and_hyp)
				debug("entered HYP mode\n");
			else
				debug("entered non-secure state\n");
#ifndef CONFIG_ARMV7_VIRT
			/* Use a '1' we already have */
			fdt_find_and_setprop((void *)dtb->load_addr,
					     "/chosen", "rzn1,nonsec",
					    &valid_addr, sizeof(valid_addr), 1);
#endif

			secure_ram_addr(_do_nonsec_entry)((void *)code->entry_point,
							  0, machid, r2);
		} else {
			pkgt_msg(code->part, "NONSEC switch failed", 0);
			return;
		}
#else
		pkgt_msg(code->part, "NONSEC/HYP not compiled in", 0);
		return;
#endif
	}
	jump_to_image_linux(&spl_image, (void *)dtb->load_addr);
	/* Should not return! */
}

static int load_rpkg_from_header(
	struct loaded_data_t *iopart, int must_verify, u32 payload_offset)
{
	struct loaded_data_t newpart = *iopart;
	struct loaded_data_t *part = &newpart;
	void *blp_header = (void *)&spkg->blp;
	int ret;

	ret = parse_rpkg_header(blp_header, part);

	if (ret == SUCCESS)
		ret = load_payload(part, payload_offset);

	if ((ret == SUCCESS) && must_verify)
		ret = verify_signature(blp_header,
				(void *)part->load_addr,
				part->entry_point - part->load_addr);

	/* Everything is good, copy the part info over */
	if (ret == SUCCESS)
		*iopart = *part;

	return ret;
}

static int load_rpkg(struct loaded_data_t *part, int must_verify)
{
	struct pkg_entry *entry = part->part;
	int ret;

	ret = load_rpkg_header(entry);

	if (ret == SUCCESS)
		ret = load_rpkg_from_header(part, must_verify, sizeof(BLpHeader_t));

	return ret;
}

static int load_spkg(struct loaded_data_t *iopart, int must_verify)
{
	struct loaded_data_t newpart = *iopart;
	struct loaded_data_t *part = &newpart;
	struct pkg_entry *entry = part->part;
	int ret;

	ret = load_spkg_header(entry);

	/* If we need to must_verify the signature, we only use the RPKG data */
	if ((ret == SUCCESS) && must_verify)
		return load_rpkg_from_header(iopart, must_verify, sizeof(*spkg));

	if (ret == SUCCESS)
		ret = parse_spkg_header(spkg->header, part);

	if (ret == SUCCESS)
		ret = load_payload(part, sizeof(*spkg));

	/* Verify payload's CRC, unless it's marked as OK */
	if ((ret == SUCCESS) && !PKG_IS_NOCRC(entry)) {
		u32 wanted, crc;

		wanted = *((u32 *)(part->load_addr + part->size - sizeof(u32)));
		/* The BLP is not necessary followed by the payload */
		crc = crc32(0, spkg->blp, sizeof(spkg->blp));
		crc = crc32(crc, (void *)part->load_addr,
			part->size - sizeof(u32));
		if (crc != wanted)
			ret = BAD_HEADER_CRC;
	}

	/* Everything is good, copy the part info over */
	if (ret == SUCCESS)
		*iopart = *part;

	return ret;
}

static int load_rpkgt_entry(
	struct pkg_table *table,
	struct pkg_entry *entry,
	int must_verify, int load_alternative_pkg)
{
	struct loaded_data_t *part = NULL;
	uint8_t type = PKG_TYPE(entry);
	uint8_t core = PKG_CORE(entry);
	uint8_t kind = PKG_KIND(entry);
	uint8_t src = PKG_SRC(entry);
	int ret = SUCCESS;

	if (core > PKGT_CORE_CM3)
		return BAD_CPU_INDEX;
	if (kind > PKGT_KIND_DATA)
		return BAD_PACKAGE_KIND;
	if (type > PKGT_TYPE_RAW)
		return BAD_PACKAGE_TYPE;
	if (src > PKGT_SRC_NAND)
		return BAD_PACKAGE_SRC;
	if (src == PKGT_SRC_SAME)
		PKG_SET_SRC(entry, default_load_source);

	printf("SPL: %s %s %s in %s at 0x%x%s%s%s%s%s%s\n",
		pkg_core[core], pkg_kind[kind],
		pkg_type[type], pkg_src[src],
		entry->offset,
		PKG_IS_BACKUP(entry) ? " Backup" : "",
		PKG_IS_ALT(entry) ? " ALT" : "",
		PKG_IS_NOCRC(entry) ? " NOCRC" : "",
		PKG_IS_INITRAMFS(entry) ? " Initramfs" : "",
		PKG_IS_NONSEC(entry) ? " NONSEC" : "",
		PKG_IS_HYP(entry) ? "+HYP" : "");

	part = &loaded_core[core][kind];
	if (part->part && PKG_IS_BACKUP(entry)) {
		/* This part was already loaded, and the one we are looking at
		 * is a backup, so we can safely skip it */
		return 1;
	}
	if (!!load_alternative_pkg != !!PKG_IS_ALT(entry)) {
		/* An alternative package is one marked as such in the Package
		 * Table. The user can specify to load an alternative package
		 * by pressing the 'u' key during boot.
		 * Ignore packages that do not match the user's selection. */
		return 2;
	}
	if (part->load_addr) {
		debug("part already loaded\n");
		return 3;
	}
	part->part = entry;

	if (type == PKGT_TYPE_UIMAGE) {
		if (must_verify)
			return CANT_LOAD_SECURE_UIMAGE;
		switch (PKG_SRC(entry)) {
#if defined(RZN1_ENABLE_QSPI)
		case PKGT_SRC_QSPI:
			spl_spi_load_one_uimage(&spl_image, flash, entry->offset);
			break;
#endif
#if defined(RZN1_ENABLE_NAND)
		case PKGT_SRC_NAND:
			spl_nand_load_element(&spl_image, entry->offset, header);
			break;
#endif
		default:
			return CANT_LOAD_SECURE_UIMAGE;
		}
		part->load_addr = spl_image.load_addr;
		part->entry_point = spl_image.entry_point;
		part->size = spl_image.size;
	} else if (type == PKGT_TYPE_RPKG) {
		ret = load_rpkg(part, must_verify);
	} else if (type == PKGT_TYPE_SPKG) {
		ret = load_spkg(part, must_verify);
	} else if (type == PKGT_TYPE_RAW) {
		part->load_addr = entry->offset;
		part->entry_point = entry->offset;
		part->size = 0;
	} else {
		ret = BAD_PACKAGE_TYPE;
	}
	/* If it failed, make sure we clear the data */
	if (ret < 0) {
		pkgt_msg(entry, get_err_str(ret), 1);
		memset(part, 0, sizeof(struct loaded_data_t));
	}
	return ret;
}

int spl_start_uboot(void);

void __noreturn spl_load_multi_images(void)
{
	struct pkg_table table_data;
	struct pkg_table *table = &table_data;
#if !defined(RZN1_SKIP_BOOTROM_CALLS)
	boot_rom_api_t *pAPI = (boot_rom_api_t *)CRYPTO_API_ADDRESS;
	u32 state;
#endif
	int must_verify = 0;
	int i, nr_entries, retries_count = PKGT_REDUNDANCY_COUNT;
	u32 table_offset = 0x10000;
	int boot_device = spl_boot_device();
	int load_alternative_pkg = 0;

	memset(&spl_image, '\0', sizeof(spl_image));

	/* Ethernet MACS will need the environment */
#if defined(CONFIG_SPL_ENV_SUPPORT)
	env_init();
	env_relocate_spec();
#endif

#if !defined(RZN1_SKIP_BOOTROM_CALLS)
	/*
	 * Get the BootROM to tell us if it's possible to, and if we need to,
	 * verify the signature of the images.
	 */
	state = pAPI->read_security_state();
	if ((state & 0x1) && (state & 0x2)) {
		printf("SPL: Error: Requires secure boot, but not available\n");
		stop();
	}
	if (state & 0x1) {
		debug("%s: Secure boot required\n", __func__);
		must_verify = 1;
	}

#if defined(RZN1_FORCE_VERIFY_USING_BOOTROM)
	/* Force the BootROM to verify the image, used for testing */
	must_verify = 1;
	printf("SPL: Forced signature verification!\n");
#endif
#else
	printf("SPL: Warning: Skipping signature verification!\n");
#endif

	/* Prepare CM3 */
	rzn1_clk_set_gate(RZN1_HCLK_CM3_ID, 1);
	sysctrl_writel(0x5, RZN1_SYSCTRL_REG_PWRCTRL_CM3);

	/* Read PKG Table from current media */
	switch (boot_device) {
#if defined(RZN1_ENABLE_QSPI)
	case BOOT_DEVICE_SPI:
		default_load_source = PKGT_SRC_QSPI;
		flash = spl_spi_probe();
		table_offset = CONFIG_SYS_SPI_U_BOOT_OFFS;
		debug("%s: QSPI boot %x\n", __func__, table_offset);
		break;
#endif
#if defined(RZN1_ENABLE_NAND)
	case BOOT_DEVICE_NAND:
		default_load_source = PKGT_SRC_NAND;
		table_offset = CONFIG_SYS_NAND_U_BOOT_OFFS;
		debug("%s: NAND boot %x\n", __func__, table_offset);
		break;
#endif
	default:
		printf("SPL: Error: Invalid boot source %d\n", boot_device);
		stop();
	}

	debug("%s: Boot dev:%d load PKG Table at %x\n", __func__,
		boot_device, table_offset);

	/* Now try to load a valid table header */
	for (; retries_count;
		retries_count--, table_offset += sizeof(struct pkg_table)) {
		/* Read PKG Table from current media */
		switch (boot_device) {
#if defined(RZN1_ENABLE_QSPI)
		case BOOT_DEVICE_SPI:
			spi_flash_read(flash, table_offset,
				       sizeof(struct pkg_table), (void *)table);
			break;
#endif
#if defined(RZN1_ENABLE_NAND)
		case BOOT_DEVICE_NAND:
			nand_spl_load_image(table_offset,
					    sizeof(struct pkg_table), (void *)nand_page);
			memcpy(table, nand_page, sizeof(struct pkg_table));
			break;
#endif
		}

		/*
		 * If we don't need to verify the image signature and we don't have a
		 * PKG Table, assume we are loading a uImage.
		 */
		if (!must_verify && table->magic != PKGT_MAGIC) {
			if (be32_to_cpu(table->magic) == IH_MAGIC) {
				debug("%s: No PKG Table, assuming uImage (%08x)\n", __func__,
				      table->magic);
				switch (default_load_source) {
#if defined(RZN1_ENABLE_QSPI)
				case PKGT_SRC_QSPI:
					spl_spi_load_one_uimage(&spl_image, flash, table_offset);
					break;
#endif
#if defined(RZN1_ENABLE_NAND)
				case PKGT_SRC_NAND:
					spl_nand_load_element(&spl_image, table_offset, header);
					break;
#endif
				}
				jump_to_image_no_args(&spl_image);
			}
			/* not a uImage, try another copy of the header */
			continue;
		}

		if (table->magic != PKGT_MAGIC) {
			printf("SPL: Error: PKG Table does not have correct ID!\n");
			continue;
		}

		{	/* check CRC of the table */
			u32 crc = table->crc;
			table->crc = 0;
			u32 wanted = crc32(0, (u8*)table, sizeof(*table));
			if (wanted != crc) {
				debug("SPL: PKGT header has invalid CRC\n");
				continue;
			}
		}

		nr_entries = (table->pkgt >> PKGT_COUNT_BIT) & (PKGT_MAX_TBL_ENTRIES-1);
		if (nr_entries == 0) {
			printf("SPL: Error: PKG Table is empty!\n");
			continue;
		}

		load_alternative_pkg = spl_start_uboot();
		for (i = 0; i < nr_entries; i++) {
			int ret = load_rpkgt_entry(table,
					&table->entries[i], must_verify, load_alternative_pkg);
			if (ret < 0) {
				if (must_verify)
					continue;
			}
		}
		break;/* we handled a valid table already, bail */
	}

	if (retries_count == 0) {
		printf("SPL: Error: Unable to load a PKG Table or uImage, hanging.\n");
		stop();
	} else if (retries_count < PKGT_REDUNDANCY_COUNT)
		printf("SPL: Warning: Skipped %d bad PKGT headers\n",
		       PKGT_REDUNDANCY_COUNT - retries_count);

	/* Start the Cortex M3 running, if the device is set to run from SRAM, it's
	 * entry point is always the start of SRAM at 0x04000000. Note that the CM3
	 * actually starts execution at address 0x0, but this is mirrored to the
	 * start of SRAM.
	 */
	if (loaded_core[PKGT_CORE_CM3][PKGT_KIND_CODE].load_addr) {
		debug("%s: Starting the CM3\n", __func__);
		sysctrl_writel(0x3, RZN1_SYSCTRL_REG_PWRCTRL_CM3);

		/*
		 * Fixed delay after starting CM3 to allow it to do all setup
		 * that *may* clash with code we are going to run on CA7s.
		 */
		printf("SPL: Delay after starting the CM3\n");
		mdelay(500);
	}

	/* Second CA7 can be started independently to CA7#0 */
	if (loaded_core[PKGT_CORE_CA71][PKGT_KIND_CODE].load_addr) {

		unsigned gic;

		/* get the GIC base address from the CBAR register */
		asm("mrc p15, 4, %0, c15, c0, 0\n" : "=r" (gic));
		gic += GIC_DIST_OFFSET;

		debug("%s: Starting the CA7#1\n", __func__);
		sysctrl_writel(loaded_core[PKGT_CORE_CA71][PKGT_KIND_CODE].load_addr,
			       RZN1_SYSCTRL_REG_BOOTADDR);
		/* kick all CPUs (except this one) by writing to GICD_SGIR */
		writel(1U << 24, gic + GICD_SGIR);
	}

	/* And now start the 'main' OS, hopefully */
	if (loaded_core[PKGT_CORE_CA70][PKGT_KIND_CODE].load_addr) {
		jump_to_image(
			&loaded_core[PKGT_CORE_CA70][PKGT_KIND_CODE],
			&loaded_core[PKGT_CORE_CA70][PKGT_KIND_DTB],
			&loaded_core[PKGT_CORE_CA70][PKGT_KIND_DATA]);
	}

	printf("SPL: Error: PKG Table has not started Cortex A7#0.\n");
	stop();
}

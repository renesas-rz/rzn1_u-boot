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
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"

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

#define sysctrl_readl(addr) \
	readl(RZN1_SYSTEM_CTRL_BASE + addr)
#define sysctrl_writel(val, addr) \
	writel(val, RZN1_SYSTEM_CTRL_BASE + addr)

u8 default_load_source;
#if defined(CONFIG_SPL_SPI_LOAD)
struct spi_flash *flash;
#endif
#if defined(CONFIG_SPL_NAND_LOAD)
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
static struct loaded_data_t loaded_core[3][3];

/* Hash of the public key that was used to sign the RPKG */
static const uint8_t rzn1_rpkg_public_key_hash[] = { RZN1_SPL_RPKG_HASH };

/* if NAND exists, we can use the static page they use to load pages */
#if defined(CONFIG_SPL_NAND_LOAD)
/* Re-use intermediate storage for NAND page */
extern u32 nand_page[];
static struct spkg_file *spkg = (struct spkg_file *)nand_page;
#else
static struct spkg_file spkg_header_struct;
static struct spkg_file *spkg = &spkg_header_struct;
#endif

static int addr_ok(
	uint32_t addr, uint32_t low, uint32_t limit)
{
	if ((addr < low) || (addr >= (low + limit)))
		return 0;

	return 1;
}

/* Check that the address range is _completely_ inside another range */
static int range_ok(
	uint32_t addr, uint32_t size, uint32_t low, uint32_t limit)
{
	if (addr_ok(addr, low, limit) && addr_ok(addr + size - 1, low, limit))
		return 1;

	return 0;
}

/* Check that the address range is _partially_ inside another range */
static int range_conflicts(
	uint32_t addr, uint32_t size, uint32_t low, uint32_t limit)
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

static const char * const pkg_type[] = { "uImage", "RPKG", "SPKG" };
static const char * const pkg_core[] = { "CA7 #0", "CA7 #1", "CM3" };
static const char * const pkg_kind[] = { "code", "dtb", "data" };
static const char * const pkg_src[] = { "(same)", "QSPI", "NAND" };

static void pkgt_msg(struct pkg_entry *entry, char *msg, int details)
{
	printf("U-Boot SPL: Error with PKG Table entry (%s).\n", msg);
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

/* Align on the next megabyte boundary */
#define MAP_ALIGNEMNT	(16*1024*1024)

/* if the code for a core is loaded, add size (and alignemnt, and a bit extra)
 * to it's size, for relocating DTB, initramfs etc */
static uint32_t reserve_core_range(int core, uint32_t size)
{
	struct loaded_data_t *code =
		&loaded_core[core][PKGT_KIND_CODE];
	/* round load address to 4K */
	uint32_t dest = (code->load_addr + code->size + code->mapped + (MAP_ALIGNEMNT-1)) &
				~(MAP_ALIGNEMNT-1);

	/* add at least 4KB, and round */
	size = (size + 8192) & ~4095;

	if (!code->load_addr)
		return 0;

	debug("%s core %d reserve %d at %08x\n", __func__, core, size, dest);

	code->mapped += size;

	return dest;
}

/* Load data from the flash specified by the table entry */
static int source_load(
	struct pkg_entry *entry,
	uint32_t offset,
	size_t size,
	void *dst)
{
	int ret = -1;

	switch (PKG_SRC(entry)) {
#if defined(CONFIG_SPL_SPI_LOAD)
	case PKGT_SRC_QSPI:
		if (!flash)
			flash = spl_spi_probe();
		ret = spi_flash_read(flash, offset, size, dst);
		break;
#endif
#if defined(CONFIG_SPL_NAND_LOAD)
	case PKGT_SRC_NAND:
		ret = nand_spl_load_image(offset, size, dst);
		break;
#endif
	}
	return ret;
}

/* Use the BootROM code to check the signature */
static int rpkg_verify_sig(
	BLpHeader_t *pBLp_Header, void *pPayload, uint32_t exec_offset)
{
	boot_rom_api_t *pAPI = (boot_rom_api_t *)CRYPTO_API_ADDRESS;
	SB_StorageArea_t StorageArea;
	uint32_t ret;

	debug("%s: pBLp_Header %p, pPayload %p, exec_offset %d\n", __func__,
	      pBLp_Header, pPayload, exec_offset);

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

/* Returns ZERO if range is valid */
static int validate_load_range(struct loaded_data_t *info)
{
	int i,j;
	int valid_addr = 0;
	/*
	 * Perform some basic checks on the address range.
	 * This is not exhaustive as the boot allows Cortex M3 code to be loaded
	 * and started before loading other images. The Cortex M3 code could do
	 * pretty much anything...
	 */

	/* Check the image destination is a valid memory region */
#if defined(CONFIG_SYS_SDRAM_BASE)
	valid_addr |= range_ok(info->load_addr, info->size,
				CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
#endif
	valid_addr |= range_ok(info->load_addr, info->size,
				RZN1_SRAM_ID_BASE, RZN1_SRAM_ID_SIZE);
	valid_addr |= range_ok(info->load_addr, info->size,
				RZN1_SRAM_SYS_BASE, RZN1_SRAM_SYS_SIZE);

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
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			if ((info != &loaded_core[i][j]) && /* <- not us! */
			    loaded_core[i][j].size) {
				valid_addr &= !range_conflicts(
						info->load_addr, info->size,
						loaded_core[i][j].load_addr,
						loaded_core[i][j].size);
			}
	return valid_addr ? 0 : -1;
}

static int parse_rpkg_header(BLpHeader_t *hdr, struct loaded_data_t *info)
{
	uint32_t tmp;

	/* Check the ImageAttributes are what we expect */
	tmp = __be32_to_cpu(hdr->Custom_attribute_Load_Address_type_ID);
	if (tmp != 0x80000001) {
		printf("%s: Bad BLP Load Address ID = 0x%x\n", __func__,
		       hdr->Custom_attribute_Load_Address_type_ID);
		return -1;
	}
	tmp = __be32_to_cpu(hdr->Custom_attribute_Execution_Offset_type_ID);
	if (tmp != 0x80000002) {
		printf("%s: Bad BLP Execution Offset ID = 0x%x\n", __func__,
		       hdr->Custom_attribute_Execution_Offset_type_ID);
		return -1;
	}

	info->load_addr = __be32_to_cpu(hdr->Custom_attribute_Load_Address_value_Big_Endian);
	info->entry_point = info->load_addr +
				__be32_to_cpu(hdr->Custom_attribute_Execution_Offset_value_Big_Endian);
	info->size = __be32_to_cpu(hdr->ImageLen);
	debug("%s: load addr 0x%x exec addr 0x%x size %d\n", __func__,
	      info->load_addr, info->entry_point-info->load_addr, info->size);

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
		return -1;
	info->load_addr = h->load_address;
	info->entry_point = info->load_addr + h->execution_offset;
	info->size = (h->payload_length >> 8) - SPKG_BLP_SIZE;

	/* copy these flags bits to the package loader.
	 * This is made so a signed package can force NONSEC/HYP and cannot
	 * be overriden by just rewritting the unprotected table entry */
	if (h->payload_length & (1 << SPKG_CODE_NONSEC_BIT))
		info->part->pkg |= (1 << PKG_NONSEC_BIT);
	if (h->payload_length & (1 << SPKG_CODE_HYP_BIT))
		info->part->pkg |= (1 << PKG_HYP_BIT);

	/* Initramfs/DTB with a zero load address means to load it just after
	 * the code for that core, if it's available etc */
	if ((PKG_IS_INITRAMFS(info->part) ||  PKG_KIND(info->part) == PKGT_KIND_DTB)
			&& info->load_addr == 0) {
		/* round */
		uint32_t size = (info->size + 4095) & ~4095;

		/* set our relocated address */
		h->load_address = info->load_addr =
			reserve_core_range(PKG_CORE(info->part), size);

		/* not really needed as it's data, but adjust anyway */
		info->entry_point += info->load_addr;
	}

	debug("%s: load addr 0x%x, size %d, exec addr 0x%x\n", __func__,
	      info->load_addr, info->size, info->entry_point-info->load_addr);

	return validate_load_range(info);
}

static int load_spkg_header(struct pkg_entry *entry)
{
	return source_load(entry, entry->offset,
				sizeof(*spkg), spkg);
}
static int load_spkg_payload(struct loaded_data_t *part)
{
	return source_load(part->part,
			part->part->offset + sizeof(*spkg),
			part->size,
			(void *)part->load_addr);
}
static int load_rpkg_header(struct pkg_entry *entry)
{
	return source_load(entry, entry->offset,
			sizeof(BLpHeader_t), spkg->blp);
}
static int load_rpkg_payload(struct loaded_data_t *part)
{
	return source_load(part->part,
			part->part->offset + sizeof(BLpHeader_t),
			part->size - sizeof(BLpHeader_t),
			(void *)part->load_addr);
}

int __weak ft_board_setup(void *blob, bd_t *bd)
{
	return 0;
}

#ifdef CONFIG_ARMV7_NONSEC
/* this is used by the secondary core bootstraper to know if it should
 * kick off in HYP more or in plain NONSEC */
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
	valid_addr |= addr_ok(code->entry_point, CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
#endif
	valid_addr |= addr_ok(code->entry_point, RZN1_SRAM_ID_BASE, RZN1_SRAM_ID_SIZE);
	valid_addr |= addr_ok(code->entry_point, RZN1_SRAM_SYS_BASE, RZN1_SRAM_SYS_SIZE);
	/* It is also possible to start code straight from the QSPI mapping */
	valid_addr |= addr_ok(code->entry_point, RZN1_V_QSPI_BASE, RZN1_V_QSPI_SIZE);

	if (!valid_addr) {
		printf("U-Boot SPL: Invalid CA7 entry point (%08x)!\n",
		       code->entry_point);
		return;
	}

	spl_image.entry_point = code->entry_point;
	spl_image.size = code->size;

	if (dtb && dtb->load_addr && fdt_check_header((void *)dtb->load_addr) == 0) {
		void * dtb_mapped = (void*)dtb->load_addr;

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
		dtb->load_addr = (uint32_t)dtb_mapped;

		fdt_find_and_setprop(dtb_mapped,
				     "/chosen", "rzn1,spl",
				     &valid_addr, sizeof(valid_addr), 1);
		if (PKG_IS_BACKUP(code->part)) {
			fdt_find_and_setprop(dtb_mapped,
					     "/chosen", "rzn1,backup",
					     &valid_addr, sizeof(valid_addr), 1);
		}
		const char * boot_source = default_load_source == PKGT_SRC_QSPI ?
					"qspi" : default_load_source == PKGT_SRC_NAND ?
					"nand" : "unknown";
		fdt_find_and_setprop(dtb_mapped,
				     "/chosen/", "rzn1,boot-source",
				     boot_source, strlen(boot_source) + 1, 1);
		fdt_find_and_setprop(dtb_mapped,
				     "/chosen", "rzn1,flash-offset",
				     &code->part->offset,
				     sizeof(code->part->offset), 1);
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
		if (dtb && dtb->load_addr)
			pkgt_msg(code->part, "Invalid DTB", 0);
		dtb = NULL;
	}

	if (dtb)
		debug("%s %08x(%08x)\n", __func__, code->entry_point, dtb->load_addr);

	if (PKG_IS_NONSEC(code->part)) {
#ifdef CONFIG_ARMV7_NONSEC
		unsigned long machid = 0xffffffff;
		unsigned long r2 = 0;

		nonsec_and_hyp = !!PKG_IS_HYP(code->part);

		/* if we also want hyp mode, add it to the device tree */
		if (nonsec_and_hyp)
			fdt_find_and_setprop((void *)dtb->load_addr,
					     "/chosen", "rzn1,hyp",
					     &nonsec_and_hyp, sizeof(nonsec_and_hyp), 1);
		ft_board_setup((void *)dtb->load_addr, NULL);

		/* Do not remove or move this line! */
		cleanup_before_linux();

		if (dtb)
			r2 = dtb->load_addr;

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
		} else
			pkgt_msg(code->part, "NONSEC switch failed", 0);
#else
		pkgt_msg(code->part, "NONSEC/HYP not compiled in", 0);
#endif
	}
	jump_to_image_linux(&spl_image, (void *)dtb->load_addr);
	/* Should not return! */
}

static int load_rpkg_from_header(
	struct loaded_data_t *part, int verify)
{
	struct pkg_entry *entry = part->part;
	void *blp_header = (void *)&spkg->blp;

	/* Check the RPKG header */
	if (parse_rpkg_header(blp_header, part)) {
		pkgt_msg(entry, "Address/size is not allowed", 1);
		return -3;
	}

	/* Check that CM3 images are at the correct place */
	if (PKG_CORE(entry) == PKGT_CORE_CM3 &&
	    (spl_image.load_addr != RZN1_SRAM_ID_BASE ||
	     spl_image.entry_point != RZN1_SRAM_ID_BASE)) {
		pkgt_msg(entry, "Invalid CM3 address", 1);
		return -4;
	}

	/* Load the payload */
	if (load_rpkg_payload(part)) {
		pkgt_msg(entry, "I/O loading payload", 1);
		return -5;
	}

	if (verify) {
		/* Check the signature */
		int ret = rpkg_verify_sig(blp_header,
				      (void *)part->load_addr,
				part->entry_point - part->load_addr);
		if (ret) {
			pkgt_msg(entry, "Verification failed", 1);
			return -6;
		}
	}
	return 0;
}

static int load_rpkg(struct loaded_data_t *part, int verify)
{
	struct pkg_entry *entry = part->part;

	/* Check that the CM3 can run code */
	if (PKG_CORE(entry) == PKGT_CORE_CM3 && cm3_runs_from_qspi()) {
		pkgt_msg(entry, "CM3 is using QSPI", 0);
		return -1;
	}

	/* Load the RPKG header */
	if (load_rpkg_header(entry)) {
		pkgt_msg(entry, "I/O loading header", 0);
		return -2;
	}

	return load_rpkg_from_header(part, verify);
}

static int load_spkg(struct loaded_data_t *part, int verify)
{
	struct pkg_entry *entry = part->part;

	/* Check that the CM3 can run code */
	if (PKG_CORE(entry) == PKGT_CORE_CM3 && cm3_runs_from_qspi()) {
		pkgt_msg(entry, "CM3 is using QSPI", 0);
		return -1;
	}
	/* Load the SPKG header */
	if (load_spkg_header(entry)) {
		pkgt_msg(entry, "I/O loading SPKG header", 0);
		return -2;
	}
	/* Check the SPKG header */
	if (parse_spkg_header(spkg->header, part)) {
		pkgt_msg(entry, "Invalid SPKG header", 1);
		return -3;
	}
	/* Check that CM3 images are at the correct place */
	if (PKG_CORE(entry) == PKGT_CORE_CM3 &&
	    (part->load_addr != RZN1_SRAM_ID_BASE ||
	     part->entry_point != RZN1_SRAM_ID_BASE)) {
		pkgt_msg(entry, "Invalid CM3 address", 1);
		return -4;
	}
	/* Load the payload */
	if (load_spkg_payload(part)) {
		pkgt_msg(entry, "I/O loading SPKG payload", 1);
		return -5;
	}
	/* Verify payload's CRC, unless it's marked as OK */
	if (!PKG_IS_NOCRC(entry)) {
		u32 wanted, crc;

		wanted = *((u32 *)(part->load_addr + part->size - sizeof(u32)));
		/* The BLP is not necessary followed by the payload */
		crc = crc32(0, spkg->blp, sizeof(spkg->blp));
		crc = crc32(crc, (void *)part->load_addr,
			part->size - sizeof(u32));
		if (crc != wanted) {
			pkgt_msg(entry, "Invalid SPKG payload CRC", 1);
			return -6;
		}
	}
	return 0;
}

static int load_rpkgt_entry(
	struct pkg_table *table,
	struct pkg_entry *entry,
	int verify, int alt_loading)
{
	struct loaded_data_t *part = NULL;
	uint8_t type = PKG_TYPE(entry);
	uint8_t core = PKG_CORE(entry);
	uint8_t kind = PKG_KIND(entry);
	uint8_t src = PKG_SRC(entry);
	int ret = 0;

	if (core > PKGT_CORE_CM3) {
		pkgt_msg(entry, "Unsupported cpu index", 0);
		return -1;
	}
	if (kind > PKGT_KIND_DATA) {
		pkgt_msg(entry, "Unsupported package kind", 0);
		return -2;
	}
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
		/* This part was already loaded, and the one
		 * we are looking at is a backup, so we can
		 * safely skip it */
		return 1;
	}
	if (!!alt_loading != !!PKG_IS_ALT(entry)) {
		return 2;
	}
	if (part->load_addr) {
		debug("part already loaded\n");
		return 3;
	}
	part->part = entry;

	if (type == PKGT_TYPE_UIMAGE) {
		if (verify) {
			pkgt_msg(entry, "Secure mode cannot load uImage", 0);
			return -3;
		}
		switch (PKG_SRC(entry)) {
		case PKGT_SRC_QSPI:
#if defined(CONFIG_SPL_SPI_LOAD)
			spl_spi_load_one_uimage(&spl_image, flash, entry->offset);
#endif
			break;
		case PKGT_SRC_NAND:
#if defined(CONFIG_SPL_NAND_LOAD)
			spl_nand_load_element(&spl_image, entry->offset, header);
#endif
			break;
		}
		part->load_addr = spl_image.load_addr;
		part->entry_point = spl_image.entry_point;
		part->size = spl_image.size;
	} else if (type == PKGT_TYPE_RPKG) {
		ret = load_rpkg(part, verify);
	} else if (type == PKGT_TYPE_SPKG) {
		ret = load_spkg(part, verify);
	} else if (type == PKGT_TYPE_RAW) {
		part->load_addr = entry->offset;
		part->entry_point = entry->offset;
		part->size = 0;
	} else {
		pkgt_msg(entry, "Unsupported pkg type", 0);
		ret = -1;
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
	uint32_t state;
#endif
	int verify = 0;
	int i, nr_entries, retries_count = PKGT_REDUNDANCY_COUNT;
	uint32_t table_offset = 0x10000;
	int boot_device = spl_boot_device();
	int alt_loading = 0;

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
		printf("U-Boot SPL: Error: Requires secure boot, but not available\n");
		stop();
	}
	if (state & 0x1) {
		debug("%s: Secure boot required\n", __func__);
		verify = 1;
	}

#if defined(RZN1_FORCE_VERIFY_USING_BOOTROM)
	/* Force the BootROM to verify the image, used for testing */
	verify = 1;
#endif
#endif

	/* Read PKG Table from current media */
	switch (boot_device) {
#if defined(CONFIG_SPL_SPI_LOAD)
	case BOOT_DEVICE_SPI:
		default_load_source = PKGT_SRC_QSPI;
		flash = spl_spi_probe();
		table_offset = CONFIG_SYS_SPI_U_BOOT_OFFS;
		debug("%s: QSPI boot %x\n", __func__, table_offset);
		break;
#endif
#if defined(CONFIG_SPL_NAND_LOAD)
	case BOOT_DEVICE_NAND:
		default_load_source = PKGT_SRC_NAND;
		table_offset = CONFIG_SYS_NAND_U_BOOT_OFFS;
		debug("%s: NAND boot %x\n", __func__, table_offset);
		break;
#endif
	default:
		printf("U-Boot SPL: Error: Invalid boot source %d\n", boot_device);
	}

	debug("%s: %sBoot dev:%d load RPKG table at %x\n", __func__,
		alt_loading ? "ALTERNATIVE " : "", boot_device,
	      table_offset);

	/* Now try to load a valid table header */
	for (; retries_count;
		retries_count--, table_offset += sizeof(struct pkg_table)) {
		/* Read PKG Table from current media */
		switch (boot_device) {
#if defined(CONFIG_SPL_SPI_LOAD)
		case BOOT_DEVICE_SPI:
			default_load_source = PKGT_SRC_QSPI;
			if (!flash)
				flash = spl_spi_probe();
			spi_flash_read(flash, table_offset,
				       sizeof(struct pkg_table), (void *)table);
			break;
#endif
#if defined(CONFIG_SPL_NAND_LOAD)
		case BOOT_DEVICE_NAND:
			default_load_source = PKGT_SRC_NAND;
			nand_spl_load_image(table_offset,
					    sizeof(struct pkg_table), (void *)nand_page);
			memcpy(table, nand_page, sizeof(struct pkg_table));
			break;
#endif
		}
		if (!default_load_source)
			debug("%s: No PKG Loading code! Config error.\n", __func__);

		/*
		 * If we don't need to verify the image signature and we don't have a
		 * PKG Table, assume we are loading a uImage.
		 */
		if (!verify && table->magic != PKGT_MAGIC) {
			if (be32_to_cpu(table->magic) == IH_MAGIC) {
				debug("%s: No PKG Table, assuming uImage (%08x)\n", __func__,
				      table->magic);
				switch (default_load_source) {
				case PKGT_SRC_QSPI:
#if defined(CONFIG_SPL_SPI_LOAD)
					spl_spi_load_one_uimage(&spl_image, flash, table_offset);
#endif
					break;
				case PKGT_SRC_NAND:
#if defined(CONFIG_SPL_NAND_LOAD)
					spl_nand_load_element(&spl_image, table_offset, header);
#endif
					break;
				}
				jump_to_image_no_args(&spl_image);
			}
			/* not a uImage, try another copy of the header */
			continue;
		}

		if (table->magic != PKGT_MAGIC) {
			printf("U-Boot SPL: Error: PKG Table does not have correct ID!\n");
			continue;
		}

		{	/* check CRC of the table */
			uint32_t crc = table->crc;
			table->crc = 0;
			uint32_t wanted = crc32(0, (u8*)table, sizeof(*table));
			if (wanted != crc) {
			//	printf("U-Boot SPL: PKGT header has invalid CRC\n");
				continue;
			}
		}

		nr_entries = (table->pkgt >> PKGT_COUNT_BIT) & (PKGT_MAX_TBL_ENTRIES-1);
		if (nr_entries == 0) {
			printf("U-Boot SPL: Error: PKG Table does not have any entries!\n");
			continue;
		}

		alt_loading = spl_start_uboot();
		for (i = 0; i < nr_entries; i++) {
			load_rpkgt_entry(table,
					&table->entries[i], verify, alt_loading);
		}
		break;/* we handled a valid table already, bail */
	}

	if (retries_count == 0)
		printf("U-Boot SPL: Unable to load a PKGT or uImage, hanging.\n");
	else if (retries_count < PKGT_REDUNDANCY_COUNT)
		printf("U-Boot SPL: Warning: Skipped %d PKGT header(s)\n",
		       PKGT_REDUNDANCY_COUNT - retries_count);

	/* Start the Cortex M3 running, if the device is set to run from SRAM, it's
	 * entry point is always the start of SRAM at 0x04000000. Note that the CM3
	 * actually starts execution at address 0x0, but this is mirrored to the
	 * start of SRAM.
	 */
	if (loaded_core[PKGT_CORE_CM3][PKGT_KIND_CODE].load_addr) {
		debug("%s: Starting the CM3\n", __func__);
		sysctrl_writel(0x3, RZN1_SYSCTRL_REG_PWRCTRL_CM3);
	}

	/* Second CA7 can be started independantly to CA7#0 */
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

	printf("U-Boot SPL: Error: PKG Table has not started Cortex A7#0.\n");
	stop();
}

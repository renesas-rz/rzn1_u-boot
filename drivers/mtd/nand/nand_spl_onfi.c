/*
 * Generic ONFi code for SPL.
 *
 * Copyright (C) 2015 Renesas Electronics Europe Ltd
 *
 * Parts based on drivers/mtd/nand/mxs_nand_spl.c
 * Copyright (C) 2014 Gateworks Corporation
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <nand.h>

nand_info_t nand_info[1];

/* Generic SPL functions below */
int nand_scan_ident(struct mtd_info *mtd, int max_chips,
		    const struct nand_flash_dev *table)
{
	struct nand_chip *chip = mtd->priv;
	int i;
	u8 mfg_id, dev_id;
	u8 id_data[8];
	struct nand_onfi_params *p = &chip->onfi_params;

	/* Reset the chip */
	chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);

	/* Send the command for reading device ID */
	chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);

	/* Read manufacturer and device IDs */
	mfg_id = chip->read_byte(mtd);
	dev_id = chip->read_byte(mtd);

	/* Try again to make sure */
	chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
	for (i = 0; i < 8; i++)
		id_data[i] = chip->read_byte(mtd);
	if (id_data[0] != mfg_id || id_data[1] != dev_id) {
		printf("second ID read did not match");
		return -ENODEV;
	}
	debug("READID is 0x%02x:0x%02x\n", mfg_id, dev_id);

	/* read ONFI */
	chip->onfi_version = 1;
	chip->cmdfunc(mtd, NAND_CMD_READID, 0x20, -1);
	if (chip->read_byte(mtd) != 'O' || chip->read_byte(mtd) != 'N' ||
	    chip->read_byte(mtd) != 'F' || chip->read_byte(mtd) != 'I') {
		printf("not ONFi device");
		return -ENODEV;
	}

	/* we have ONFI, probe it */
	chip->cmdfunc(mtd, NAND_CMD_PARAM, 0, -1);
	chip->read_buf(mtd, (uint8_t *)p, sizeof(*p));
	mtd->name = p->model;
	mtd->writesize = le32_to_cpu(p->byte_per_page);
	mtd->erasesize = le32_to_cpu(p->pages_per_block) * mtd->writesize;
	mtd->oobsize = le16_to_cpu(p->spare_bytes_per_page);
	chip->chipsize = le32_to_cpu(p->blocks_per_lun);
	chip->chipsize *= (uint64_t)mtd->erasesize * p->lun_count;
	/* Calculate the address shift from the page size */
	chip->page_shift = ffs(mtd->writesize) - 1;
	chip->phys_erase_shift = ffs(mtd->erasesize) - 1;
	/* Convert chipsize to number of pages per chip -1 */
	chip->pagemask = (chip->chipsize >> chip->page_shift) - 1;
	chip->badblockbits = 16;

	debug("erasesize=%d (>>%d)\n", mtd->erasesize, chip->phys_erase_shift);
	debug("writesize=%d (>>%d)\n", mtd->writesize, chip->page_shift);
	debug("oobsize=%d\n", mtd->oobsize);
	debug("chipsize=%lld\n", chip->chipsize);

	return 0;
}

int nand_scan_tail(struct mtd_info *mtd)
{
	return 0;
}

int nand_register(int devnum)
{
	return 0;
}

int nand_default_bbt(struct mtd_info *mtd)
{
	return 0;
}

void nand_deselect(void)
{
}

void nand_init(void)
{
	struct mtd_info *mtd = &nand_info[0];
	struct nand_chip *chip;

	board_nand_init();

	chip = mtd->priv;

	chip->numchips = 1;

	/* SPL uses own buffers */
	chip->buffers = NULL;
	chip->oob_poi = NULL;

	/* setup flash layout (does not scan as we override that) */
	mtd->size = chip->chipsize;

	printf("NAND %llu MiB\n", (mtd->size / (1024 * 1024)));
}

static int spl_read_page_ecc(struct mtd_info *mtd, void *buf, unsigned int page)
{
	struct nand_chip *chip = mtd->priv;
	int ret;

	chip->cmdfunc(mtd, NAND_CMD_READ0, 0x0, page);
	ret = chip->ecc.read_page(mtd, chip, buf, 0, page);
	if (ret < 0) {
		printf("read_page failed %d\n", ret);
		return -1;
	}
	return 0;
}

static int spl_is_badblock(struct mtd_info *mtd, int page)
{
	register struct nand_chip *chip = mtd->priv;
	uint16_t oob = 0;

	chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
	chip->read_buf(mtd, (uint8_t *)&oob, 2);
	debug("%s page:%d oob = 0x%04X\n", __func__, page, oob);

	return oob != 0xffff;
}

int nand_spl_load_image(uint32_t offs, unsigned int size, void *buf)
{
	struct mtd_info *mtd = &nand_info[0];
	struct nand_chip *chip = mtd->priv;
	unsigned int page;
	unsigned int nand_page_per_block;
	unsigned int sz = 0;
	uint32_t data_offs;

	page = offs >> chip->page_shift;
	nand_page_per_block = mtd->erasesize / mtd->writesize;

	debug("%s offset:0x%08x len:%d page:%d\n", __func__, offs, size, page);

	/* Read first page */
	if (spl_read_page_ecc(mtd, buf, page) < 0)
		return -1;
	page++;

	/* If the caller passed in an offset that is not page aligned, deal
	 * with it by moving the data in the output buffer.
	 */
	data_offs = offs & ((1 << chip->page_shift) - 1);
	sz = mtd->writesize - data_offs;
	if (data_offs) {
		u8 *buf8 = buf;
		unsigned int i;
		for (i = 0; i < sz; i++) {
			*buf8 = *(buf8 + data_offs);
			buf8++;
		}
	}
	buf += sz;

	size = roundup(size, mtd->writesize);
	while (sz < size) {
		if (spl_read_page_ecc(mtd, buf, page) < 0)
			return -1;
		sz += mtd->writesize;
		buf += mtd->writesize;
		page++;

		/*
		 * Check if we have crossed a block boundary, and if so
		 * check for bad block.
		 */
		if (!(page % nand_page_per_block)) {
			/*
			 * Yes, new block. See if this block is good. If not,
			 * loop until we find a good block.
			 */
			while (spl_is_badblock(mtd, page)) {
				page = page + nand_page_per_block;
				/* Check if we've reached the end of flash. */
				if (page >= mtd->size >> chip->page_shift)
					return -ENOMEM;
			}
		}
	}

	return 0;
}

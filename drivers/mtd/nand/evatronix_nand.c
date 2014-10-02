/*
 * evatronix_nand.c - NAND Flash Driver for Evatronix NANDFLASH-CTRL
 * NAND Flash Controller IP.
 *
 * Intended to handle one NFC, with up to two connected NAND flash chips,
 * one per bank.
 *
 * This implementation has been designed against Rev 1.15 of the
 * NANDFLASH-CTRL Design Specification.
 *
 * Copyright (c) 2014 Axis Communication AB, Lund, Sweden.
 * Portions Copyright (c) 2010 ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#if defined(LINUX)
#include <asm/dma.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_mtd.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/concat.h>
#include <linux/mtd/partitions.h>
#include <linux/version.h>
#include "evatronix_nand.h"
#else
#include <common.h>
#include <malloc.h>
#include <nand.h>
#include <asm/dma-mapping.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand_ecc.h>
#include "evatronix_nand.h"
#endif

#if !defined(LINUX)
/* For U-Boot, these are not necessary in this driver when not using irqs */
#define spinlock_t int
#define spin_lock_init(x)
#define init_waitqueue_head(x)
#endif

/* Driver configuration */

#undef ETX_CE_BANK_SEL /* Separate chips connected as separate banks */
#define ETX_RB_WIRED_AND /* Use wired-AND for ready/busy from flash chip */

#undef POLLED_XFERS
#define POLLED_XFERS

/* Workarounds for development environment */

#define WORKAROUND_NO_ECC_CNT

/* Debugging */

#if defined(DEBUG)
#define MTD_TRACE(FORMAT, ...) \
	pr_info("mtd trace: %s: " FORMAT, __func__, ## __VA_ARGS__)
#else
#define MTD_TRACE(FORMAT, ...) \
	if (0) \
		pr_info("mtd trace: %s: " FORMAT, __func__, ## __VA_ARGS__)
#endif

#ifndef CONFIG_MTD_NAND_EVATRONIX_CHIPS
#define CONFIG_MTD_NAND_EVATRONIX_CHIPS 1
#endif

/* Read modes */
enum etx_read_mode {
	ETX_READ_STD, /* Standard page read with ECC */
	ETX_READ_RAW, /* Raw mode read of main area without ECC */
	ETX_READ_OOB, /* Read oob only (no ECC) */
	ETX_READ_ALL  /* Read main+oob in raw mode (no ECC) */
};

#define DMA_BUF_SIZE (8192 + 640) /* main + spare for 8k page flash */

/* # bytes into the OOB we put our ECC */
#define ECC_OFFSET 2

/* Chip Select for device on different CS */
#if defined(CONFIG_NAND_CADENCE_EVATRONIX_CS)
static int cs = CONFIG_NAND_CADENCE_EVATRONIX_CS;
#else
static int cs = 0;
#endif

/* Timing parameters, from dt */
struct etx_timings {
	uint32_t time_seq_0;
	uint32_t time_seq_1;
	uint32_t timings_asyn;
	uint32_t time_gen_seq_0;
	uint32_t time_gen_seq_1;
	uint32_t time_gen_seq_2;
	uint32_t time_gen_seq_3;
};

/* Configuration, from dt */
struct etx_setup {
	nand_ecc_modes_t ecc_mode;
	int ecc_blksize;
	int ecc_strength;
	bool on_flash_bbt;
	struct etx_timings timings;
};

/* DMA buffer, from both software (buf) and hardware (phys) perspective. */
struct etx_dma {
	void *buf; /* mapped address */
	dma_addr_t phys; /* physical address */
	int bytes_left; /* how much data left to read from buffer? */
	uint8_t *ptr; /* work pointer */
};

#ifndef POLLED_XFERS
/* Interrupt management */
struct etx_irq {
	int done; /* interrupt triggered, consequently we're done. */
	uint32_t int_status; /* INT_STATUS at time of interrupt */
	wait_queue_head_t wq; /* For waiting on controller interrupt */
};
#endif

/* Information common to all chips, including the NANDFLASH-CTRL IP */
struct etx_info {
	unsigned char __iomem *regbase;
	struct device *dev;
	struct nand_hw_control *controller;
	struct etx_setup *setup;
	struct etx_dma dma;
#ifndef POLLED_XFERS
	struct etx_irq irq;
#endif
};

/* Per-chip controller configuration */
struct etx_config {
	uint32_t mem_ctrl;
	uint32_t control;
	uint32_t ecc_ctrl;
	uint32_t ecc_offset;
	uint32_t mem_status_mask;
};

/* Cache for info that we need to save across calls to etx_nand_command */
struct etx_cmd_cache {
	unsigned int command;
	int page;
	int column;
	int oob_required;
	int write_raw;
};

/* Information for each physical NAND chip. */
struct chip_info {
	struct mtd_info *mtd;
	struct nand_chip chip;
	struct etx_cmd_cache cmd_cache;
	struct etx_config etx_config;
	int ecc_enabled;
};

/* What we tell mtd is an mtd_info actually is a complete chip_info */
#define TO_CHIP_INFO(mtd) ((struct chip_info *)((struct nand_chip *)(mtd)->priv)->priv)

/* This is a global pointer, as we only support one single instance of the NFC.
 * For multiple instances, we would need to add etx_info as a parameter to
 * several functions, as well as adding it as a member of the chip_info struct.
 * Since most likely a system would only have one NFC instance, we don't
 * go all the way implementing that feature now. */
static struct etx_info *etx_info;

/* The timing setup is expected to come via DT. We keep some default timings
 * here for reference, based on a 100 MHz reference clock. */

static const struct etx_timings default_mode0_pll_enabled = {
	0x0d151533, 0x000b0515, 0x00000046,
	0x00150000, 0x00000000, 0x00000005, 0x00000015 };

/* oob info generated at runtime depending on ecc strength */
static struct nand_ecclayout eva_oobinfo;

/**** Utility routines. */

/* Count the number of 0's in buff upto a max of max_bits */
/* Used to determine how many bit flips there are in an allegely erased block */
static int count_zero_bits(uint8_t *buff, int size, int max_bits)
{
	int k, zero_bits = 0;
	uint32_t *buf32 = (uint32_t *)buff;

	/* size is always a multiple of 4 */
	for (k = 0; k < size/4; k++) {
		if (~buf32[k]) {
			zero_bits += hweight32(~buf32[k]);
			if (zero_bits > max_bits)
				break;
		}
	}

	return zero_bits;
}

/**** Low level stuff. Read and write registers, interrupt routine, etc. */

/* Read and write NFC SFR registers */

static uint32_t etx_read(uint reg_offset)
{
	return ioread32(etx_info->regbase + reg_offset);
}

static void etx_write(uint32_t data, uint reg_offset)
{
	/* Note: According to NANDFLASH-CTRL Design Specification, rev 1.14,
	 * p19, the NFC SFR's can only be written when STATUS.CTRL_STAT is 0.
	 * So, should really check for that here. */
	iowrite32(data, etx_info->regbase  + reg_offset);
}

#ifndef POLLED_XFERS
static irqreturn_t etx_irq(int irq, void *device_info)
{
	/* Note that device_info = etx_info, so if we don't want a global
	 * etx_info we can get it via device_info. */

	/* Save interrupt status in case caller wants to check what actually
	 * happened. */
	etx_info->irq.int_status = etx_read(INT_STATUS_REG);

	MTD_TRACE("Got interrupt %d, INT_STATUS 0x%08x\n",
		  irq, etx_info->irq.int_status);

	/* disable global NFC interrupt */
	etx_write(etx_read(CONTROL_REG) & ~CONTROL_INT_EN, CONTROL_REG);

	etx_info->irq.done = 1;
	wake_up(&etx_info->irq.wq);

	return IRQ_HANDLED;
}
#endif

/* Get resources from platform: register bank mapping, irqs, etc */
static int etx_init_resources(struct platform_device *pdev)
{
#if defined(LINUX)
	struct device *dev = &pdev->dev;
	struct resource *resource;
#ifndef POLLED_XFERS
	int irq;
#endif
	int res;

	/* Register base for controller, ultimately from device tree */
	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource) {
		dev_err(dev, "No register addresses configured!\n");
		return -ENOMEM;
	}
	etx_info->regbase = devm_ioremap_resource(dev, resource);
	if (IS_ERR(etx_info->regbase))
		return PTR_ERR(etx_info->regbase);

	dev_info(dev, "Got SFRs at phys %p..%p, mapped to %p\n",
		 (void *)resource->start, (void *)resource->end,
		 etx_info->regbase);
#else
	/* U-Boot */
	etx_info->regbase = (void *)CONFIG_SYS_NAND_BASE;
#endif

	/* A DMA buffer */
#if defined(LINUX)
	etx_info->dma.buf =
		dma_alloc_coherent(dev, DMA_BUF_SIZE,
				   &etx_info->dma.phys, GFP_KERNEL);
#else
	etx_info->dma.buf =
		dma_alloc_coherent(DMA_BUF_SIZE,
				   (unsigned long*)&etx_info->dma.phys);
#endif
	if (etx_info->dma.buf == NULL) {
		dev_err(dev, "dma_alloc_coherent failed!\n");
		return -ENOMEM;
	}

	MTD_TRACE("DMA buffer %p at physical %p\n",
		 etx_info->dma.buf, (void *)etx_info->dma.phys);

#ifndef POLLED_XFERS
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "No irq configured\n");
		return irq;
	}
	res = request_irq(irq, etx_irq, 0, "evatronix-nand", etx_info);
	if (res < 0) {
		dev_err(dev, "request_irq failed\n");
		return res;
	}
	dev_info(dev, "Successfully registered IRQ %d\n", irq);
#endif

	return 0;
}

/* Write timing setup to controller */
static void setup_etx_timing(struct etx_setup *etx_setup)
{
	etx_write(etx_setup->timings.time_seq_0, TIME_SEQ_0_REG);
	etx_write(etx_setup->timings.time_seq_1, TIME_SEQ_1_REG);
	etx_write(etx_setup->timings.timings_asyn, TIMINGS_ASYN_REG);
	etx_write(etx_setup->timings.time_gen_seq_0, TIME_GEN_SEQ_0_REG);
	etx_write(etx_setup->timings.time_gen_seq_1, TIME_GEN_SEQ_1_REG);
	etx_write(etx_setup->timings.time_gen_seq_2, TIME_GEN_SEQ_2_REG);
	etx_write(etx_setup->timings.time_gen_seq_3, TIME_GEN_SEQ_3_REG);
}

/* Write per-chip specific config to controller */
static void config_etx(struct etx_config *etx_config, void *ref)
{
	static void *saved_ref;

	/* To avoid rewriting these unnecessarily every time, we only do
	 * it when the ref has changed, or if ref == NULL (=> force). */
	if (ref) {
		if (ref == saved_ref)
			return;
		saved_ref = ref; /* only save if non-null */
	}

	etx_write(etx_config->mem_ctrl, MEM_CTRL_REG);
	etx_write(etx_config->control, CONTROL_REG);
	etx_write(etx_config->ecc_ctrl, ECC_CTRL_REG);
	etx_write(etx_config->ecc_offset, ECC_OFFSET_REG);
}

#ifndef POLLED_XFERS
/* Set up interrupt and wq, with supplied interrupt mask */
static void setup_int(uint32_t what)
{
	/* Flag waited on by wq */
	etx_info->irq.done = 0;

	/* clear interrupt status bits */
	etx_write(0, INT_STATUS_REG);

	/* set interrupt mask */
	etx_write(what, INT_MASK_REG);

	/* enable global NFC interrupt. Ooooh... */
	etx_write(etx_read(CONTROL_REG) | CONTROL_INT_EN, CONTROL_REG);
}
#endif

/* Set up interrupt, send command, then wait for (any bit of) expected state */
/* Before issuing a command, we should check if the controller is ready.
 * We can't check INT_STATUS_REG.MEM0_RDY_INT_FL as it is not a status bit,
 * it is set on an nfc state transition after the completion of for
 * instance a page program command, so we can use it as a command
 * completed trigger however.
 * (See NFC Design Spec (rev 1.15) figure 35 for illustration.)
 * TODO: However, we could check STATUS.CTRL_STAT, which should always
 * be 0 prior to issuing a command, indicating the controller is not
 * busy. */
static void command_and_wait(uint32_t etx_command, uint32_t int_state)
#ifndef POLLED_XFERS
{
	long timeout;

	/* Set up interrupt condition. Here we utilize the fact that the
	 * bits in INT_STATE are the same as in INT_MASK. */
	setup_int(int_state);

	/* Send command */
	etx_write(etx_command, COMMAND_REG);

	/* The timeout should only trigger in abnormal situations, so
	 * we leave it at one second for now. (nand_base uses 20ms for write
	 * and 400ms for erase, respectively.) */
	/* TODO: A special case might be to consider the case of an
	 * unconnected flash chip during probe. If that causes the timeout
	 * to be triggered, we might want to lower it, and even make it
	 * dependent on the NAND flash command being executed. */
	timeout = wait_event_timeout(etx_info->irq.wq, etx_info->irq.done,
				     1 * HZ);
	if (timeout <= 0) {
		dev_info(etx_info->dev,
			 "Request 0x%08x timed out waiting for 0x%08x\n",
			 etx_command, int_state);
		/* TODO: Do something useful here? */
	}
}
#else /* POLLED_XFERS */
{
	int cmd_loops = 0;
	uint32_t read_status, read_int_status, dma_status;

	/* Clear interrupt status bits */
	etx_write(0, INT_STATUS_REG);

	/* Send command */
	etx_write(etx_command, COMMAND_REG);

	/* Wait for command to complete */
	MTD_TRACE("Waiting for 0x%08x bit(s) to be set in int_status\n",
		  int_state);

#define MAX_CMD_LOOPS 100000
	do {
		cmd_loops++;
		read_status = etx_read(STATUS_REG);
		read_int_status = etx_read(INT_STATUS_REG);
		dma_status = etx_read(DMA_CTRL_REG);
		MTD_TRACE("Wait for command done: 0x%08x/0x%08x/0x%08x (%d)\n",
			  read_status, read_int_status, dma_status, cmd_loops);
	} while (!(read_int_status & int_state) && cmd_loops < MAX_CMD_LOOPS);

	if (cmd_loops >= MAX_CMD_LOOPS)
		MTD_TRACE("Int wait for 0x%08x timed out after %d loops: "
			  "STATUS = 0x%08x, INT_STATUS=0x%08x, "
			  "DMA_CTRL = 0x%08x, command 0x%08x\n",
			  int_state, cmd_loops, read_status, read_int_status,
			  dma_status, etx_command);
}
#endif

/* Initialize DMA, wq and interrupt status for upcoming transfer. */
static void init_dma(uint64_t addr, int bytes)
{
	int dma_trig_level;

	/* DMA control */

	/* Start when COMMAND register written, set burst type/size */
	etx_write(DMA_CTRL_DMA_START | DMA_CTRL_DMA_BURST_I_P_4, DMA_CTRL_REG);

	/* DMA address and length */
	if (etx_read(PARAM_REG_REG) & PARAM_REG_DMA64)
		etx_write(addr >> 32, DMA_ADDR_H_REG);
	etx_write(addr, DMA_ADDR_L_REG);

	/* Byte counter */
	/* Round up to nearest 32-bit word */
	etx_write((bytes + 3) & 0xfffffffc, DMA_CNT_REG);

	/* Cap DMA trigger level at FIFO size */
	dma_trig_level = bytes / 4; /* 32-bit entities */
	if (dma_trig_level > DMA_TLVL_MAX)
		dma_trig_level = DMA_TLVL_MAX;
	etx_write(dma_trig_level, DMA_TLVL_REG);
}

/* Initialize transfer to or from DMA buffer */
static void init_dmabuf(int bytes)
{
	etx_info->dma.ptr = etx_info->dma.buf;
	etx_info->dma.bytes_left = bytes;
}

/* Initialize controller for DATA_REG readout */
static void init_dreg_read(int bytes)
{
	/* Transfer to DATA_REG register */
	etx_write(DATA_REG_SIZE_DATA_REG_SIZE(bytes), DATA_REG_SIZE_REG);
}

/* Set up CONTROL depending on whether we want ECC or not */
static void setup_control(int enable_ecc)
{
	uint32_t control;

	/* When reading the oob, we never want ECC, when reading the
	 * main area, it depends. */
	control = etx_read(CONTROL_REG) & ~CONTROL_ECC_EN;
	if (enable_ecc)
		control |= CONTROL_ECC_EN;
	etx_write(control, CONTROL_REG);
}

/* Read from flash using DMA */
/* Assumes basic setup for DMA has been done previously. */
/* The MTD framework never reads a complete page (main + oob) in one go
 * when using HW ECC, so we don't need to support ETX_READ_ALL in this mode.
 * For SW ECC we read the whole page on one go in ALL mode however. */
static void read_dma(struct chip_info *info, int page, int column,
		     enum etx_read_mode m)
{
	int size;
	uint32_t command;

	switch (m) {
	case ETX_READ_OOB:
		size = info->mtd->oobsize;
		break;
	case ETX_READ_ALL:
		size = info->mtd->oobsize + info->mtd->writesize;
		break;
	case ETX_READ_STD:
	case ETX_READ_RAW:
		size = info->mtd->writesize;
		break;
	default:
		BUG();
	}

	/* Set up ECC depending on mode */
	setup_control(m == ETX_READ_STD && info->ecc_enabled);

	/* Set up DMA and transfer size */

	init_dmabuf(size);
	init_dma(etx_info->dma.phys, size);
	etx_write(size, DATA_SIZE_REG);

	/* Set up addresses */

	if (m == ETX_READ_OOB)
		column += info->mtd->writesize;
	etx_write(column, ADDR0_COL_REG);
	etx_write(page, ADDR0_ROW_REG);

	/* For devices > 128 MiB we have 5 address cycles and can use a
	 * standard NFC command sequence. For smaller devices we have
	 * 4 address cycles and need to use a Generic Command Sequence. */
	if (info->chip.chipsize > (128 << 20)) {
		command = COMMAND_READ_PAGE_DMA_STD;
	} else {
		etx_write(GEN_SEQ_CTRL_READ_PAGE_4CYCLE, GEN_SEQ_CTRL_REG);
		command = COMMAND_READ_PAGE_DMA_GEN;
	}

	command_and_wait(command, INT_STATUS_DMA_INT_FL);
}

/* Write using DMA */
/* Assumes DMA has been set up previously and buffer contains data. */
/* Contrary to read, column is set to writesize when writing to oob, by mtd.
 * oob is set when the caller wants to write oob data along with the main data.
 */
static void write_dma(struct chip_info *info, int page, int column,
		int oob, int raw)
{
	int size;
	uint32_t command;

	/* Since the controller handles ECC on its own, raw mode doesn't
	 * come into the size calculations. */
	if (column >= info->mtd->writesize) { /* oob write only */
		size = info->mtd->oobsize;
		raw = 1;
	} else {
		size = info->mtd->writesize;
		if (oob) {
			size += info->mtd->oobsize;
			raw = 1;
		}
	}

	setup_control(!raw && info->ecc_enabled);

	/* Dump selected parts of buffer */
	MTD_TRACE("Write %d bytes: 0x%08x 0x%08x .. 0x%08x\n", size,
		  ((uint32_t *)(etx_info->dma.buf))[0],
		  ((uint32_t *)(etx_info->dma.buf))[1],
		  ((uint32_t *)(etx_info->dma.buf))[size / 4 - 1]);

	/* Set up DMA and transfer size */
	init_dma(etx_info->dma.phys, size);
	etx_write(size, DATA_SIZE_REG);

	/* Set up addresses */
	etx_write(column, ADDR0_COL_REG);
	etx_write(page, ADDR0_ROW_REG);

	/* For devices > 128 MiB we have 5 address cycles and can use a
	 * standard NFC command sequence. For smaller devices we have
	 * 4 address cycles and need to use a Generic Command Sequence. */
	if (info->chip.chipsize > (128 << 20)) {
		command = COMMAND_WRITE_PAGE_DMA_STD;
	} else {
		etx_write(GEN_SEQ_CTRL_WRITE_PAGE_4CYCLE, GEN_SEQ_CTRL_REG);
		command = COMMAND_WRITE_PAGE_DMA_GEN;
	}

	/* Send command and wait for the DMA buffer to be drained */
	command_and_wait(command, INT_STATUS_DMA_INT_FL);

	/* nand_base waits for device ready by reading error status after the
	 * write command. */
}

/* Block erase */
static void block_erase(int page)
{
	/* Set up addresses */
	etx_write(page, ADDR0_ROW_REG);
	MTD_TRACE("Erase block containing page %d\n", page);

	/* Send 3 address cycle block erase command */
	command_and_wait(COMMAND_BLOCK_ERASE, INT_STATUS_MEM_RDY_INT_FL(cs));

#ifndef POLLED_XFERS
	MTD_TRACE("Erase block: INT_STATUS 0x%08x\n", etx_info->irq.int_status);
#endif
	/* TODO: What to do if we get an error bit set here (i.e.INT_STATUS_REG.
	 * STAT_ERR_INT0_FL) ? Normally, error status is checked by nand_base
	 * by doing a status read after the erase command. So we can probably
	 * ignore STAT_ERR_INT0_FL here. If need be, we can save the
	 * status so a subsequent status right might use it for something.
	 * The err bit probably just indicates that the flash didn't pull
	 * R/_B low within tWB. */
}

/* Check for erased page.
 * The prerequisite to calling this routine is: page has been read with
 * HW ECC, which has returned an 'ecc uncorrectable' status, so either the
 * page does in fact contain too many bitflips for the ECC algorithm to correct
 * or the page is in fact erased, which results in the all-FF's ECC to
 * be invalid relative to the all-FF's data on the page.
 * Since with the Evatronix NFC we don't have access to either the ECC bytes
 * or the oob area after a HW ECC read, the following algorithm is adopted:
 * - Count the number of 0's in the main area. If there are more than
 *   the ECC strength per ECC block we assume the page wasn't in fact erased,
 *   and return with an error status.
 * - If the main area appears erased, we still need to determine if the oob is
 *   also erased, if not, it would appear that the page wasn't in fact erased,
 *   and what we're looking at is a page of mostly-FF data with an invalid ECC.
 *   - Thus we need to read the oob, leaving the main area at the start of the
 *     DMA buffer in case someone actually wants to read the data later (e.g.
 *     nanddump).
 *   - We then count the number of non-zero bits in the oob. The accepted
 *     number of zeros could be determined by figuring the the size ratio
 *     of the oob compared to an ECC block. For instance, if the oob is 64
 *     bytes, an ECC block 512 bytes, and the error correction capability
 *     of 8 bits, then the accepted number of zeros for the oob to be
 *     considered erased would be 64/512 * 8 = 1. Alternatively we could just
 *     accept an error correction capability number of zeros.
 *     If there are less than this threshold number of zero bits, the page
 *     is considered erased. In this case we return an all-FF page to the user.
 *     Otherwise, we consider ourselves to have an ECC error on our hands,
 *     and we return the apropriate error status while at the same time leaving
 *     original main area data in place, for potential scrutiny by a user space
 *     application (e.g. nanddump).
 * Caveat: It could be that there are some cases for which an almost-FF page
 * yields an almost-FF ECC. If there are fewer than the error correction
 * capability number of zero bits, we could conclude that such a page would
 * be erased when in fact it actually contains data with too many bitflips.
 * Experience will have to determine whether this can actually occur. From
 * past experiences with ECC codes it seems unlikely that that trivial
 * data will in fact result in a trivial ECC code. Even the fairly basic
 * 1-bit error correction capability Hamming code does not on its own return
 * an all-FF ECC for all-FF data.
 *
 * Function returns 1 if the page is in fact (considered) erased, 0 if not.
 */
static int check_erased_page(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);
	struct nand_chip *chip = &info->chip;

	/* We calculate the number of steps here rather than grabbing
	 * ecc.steps to handle the case of a subpage read where we
	 * haven't read a complete page. */
	int eccsteps = len / chip->ecc.size;
	int eccsize = chip->ecc.size;
	int eccstrength = chip->ecc.strength;

	int main_area_zeros = 0;

	int step;
	uint8_t *bufpos = buf;

	MTD_TRACE("%s: %d byte page, ecc steps %d, size %d, strength %d\n",
		  __func__, len, eccsteps, eccsize, eccstrength);

	/* Check that main area appears erased. If not, return */

	for (step = 0; step < eccsteps; step++) {
		int zeros = count_zero_bits(bufpos, eccsize, eccstrength);

		if (zeros > eccstrength)
			return 0;
		bufpos += eccsize;
		main_area_zeros += zeros;
	}

	/* Ok, main area seems erased. Read oob so we can check it too. */

	/* Note that this will overwrite the DMA buffer with the oob data,
	 * which is ok since the main area data has already been copied
	 * to buf earlier. */
	read_dma(info, info->cmd_cache.page, info->cmd_cache.column,
		 ETX_READ_OOB);

	/* We go for the simple approach and accept eccstrength zero bits */
	/* We only check the BBM and ECC bytes, the rest may be file system */
	if (count_zero_bits(etx_info->dma.buf,
		eva_oobinfo.oobfree->offset, eccstrength) > eccstrength)
		return 0;

	MTD_TRACE("%s: Page is erased.%s\n", __func__,
		  main_area_zeros != 0 ? " Clearing main area to 0xff." : "");

	if (main_area_zeros != 0)
		memset(buf, 0xff, len);

	return 1;
}

/**** MTD API ****/

/* For cmd_ctrl (and possibly others) we need to do absolutely nothing, but the
 * pointer is still required to point to a valid function. */
static void etx_dummy_cmd_ctrl(struct mtd_info *mtd, int cmd,
		unsigned int ctrl)
{
}

/* Read state of ready pin */
static int etx_dev_ready(struct mtd_info *mtd)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);
	struct etx_config *etx_config = &info->etx_config;

	MTD_TRACE("%p\n", mtd);

	return !!(etx_read(STATUS_REG) & etx_config->mem_status_mask);

}

/* Read byte from DMA buffer */
/* Not used directly, only via etx_read_byte */
static uint8_t etx_read_dmabuf_byte(struct mtd_info *mtd)
{
	if (etx_info->dma.bytes_left) {
		MTD_TRACE("%02x\n", *etx_info->dma.ptr);
		etx_info->dma.bytes_left--;
		return *etx_info->dma.ptr++;
	} else
		return 0; /* no data */
}

/* Read block of data from DMA buffer */
static void etx_read_dmabuf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	MTD_TRACE("%p, buf %p, len %d\n", mtd, buf, len);
	if (len > etx_info->dma.bytes_left) {
		dev_crit(etx_info->dev,
			 "Trying to read %d bytes with %d bytes remaining\n",
			 len, etx_info->dma.bytes_left);
		BUG();
	}
	memcpy(buf, etx_info->dma.ptr, len);
	etx_info->dma.ptr += len;
	etx_info->dma.bytes_left -= len;
}

/* Write block of data to DMA buffer */
static void etx_write_dmabuf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	MTD_TRACE("%p, buf %p, len %d\n", mtd, buf, len);
	/* TODO: Grab info pointer from mtd instead of using same always ? */
	if (len > etx_info->dma.bytes_left) {
		dev_crit(etx_info->dev,
			 "Trying to write %d bytes with %d bytes remaining\n",
			 len, etx_info->dma.bytes_left);
		BUG();
	}
	memcpy(etx_info->dma.ptr, buf, len);
	etx_info->dma.ptr += len;
	etx_info->dma.bytes_left -= len;
}

/* Read byte from DMA buffer or DATA_REG, depending on previous command. */
/* Used by MTD for reading ID bytes, and chip status */
static uint8_t etx_read_byte(struct mtd_info *mtd)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);
	uint8_t status_value;

	if (info->cmd_cache.command != NAND_CMD_STATUS)
		return etx_read_dmabuf_byte(mtd);

	MTD_TRACE("Read status\n");

	/* In order to read status, we need to send a READ_STATUS command
	 * to the NFC first, in order to get the data into the DATA_REG */
	init_dreg_read(1);
	/* We want to read all status bits from the device */
	etx_write(STATUS_MASK_STATE_MASK(0xff), STATUS_MASK_REG);
	command_and_wait(COMMAND_READ_STATUS, INT_STATUS_DATA_REG_FL);
	status_value = etx_read(DATA_REG_REG) & 0xff;
	status_value = 0xC0; /* Bit 7 : No write prot., Bit 6: Device ready */
	MTD_TRACE("Status 0x%08x\n", status_value);
	return status_value;
}

/* Do the dirty work for read_page_foo */
static int etx_read_page_mode(struct mtd_info *mtd, struct nand_chip *chip,
		uint8_t *buf, int oob_required, int page, enum etx_read_mode m)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);
	unsigned int max_bitflips;
	uint32_t ecc_status;

	if (page != info->cmd_cache.page) {
		MTD_TRACE("Warning: Read page has different page number than "
			  "READ0: %d vs. %d\n", page, info->cmd_cache.page);
	}

	if (m == ETX_READ_STD) {
		/* ECC error flags and counters are not cleared automatically
		 * so we do it here. */
		/* Note that the design spec says nothing about having to
		 * zero ECC_STAT (although it explicitly says that ECC_CNT
		 * needs to be zeroed by software), but testing on actual
		 * hardware (RTL at this stage) reveals that this is in fact
		 * the case. */
		etx_write(0, ECC_STAT_REG);
		etx_write(0, ECC_CNT_REG);
	}

	read_dma(info, info->cmd_cache.page, info->cmd_cache.column, m);

	/* This is actually etx_read_dmabuf */
	chip->read_buf(mtd, buf, mtd->writesize);

	if (m == ETX_READ_RAW)
		return 0;

	/* Get ECC status from controller */
	ecc_status = etx_read(ECC_STAT_REG);
	max_bitflips = etx_read(ECC_CNT_REG) & ECC_CNT_ERR_LVL_MASK;

#ifdef WORKAROUND_NO_ECC_CNT
	/* If we get an ERROR bit set, but ECC_CNT is 0, we assume
	 * a single bit flip has occurred for want of better information. */
	if ((ecc_status & ECC_STAT_ERROR(cs)) && max_bitflips == 0)
		max_bitflips = 1;
#endif

	if (ecc_status & ECC_STAT_UNC(cs))
		if (!check_erased_page(mtd, buf, mtd->writesize)) {
			dev_warn(etx_info->dev, "NAND: Uncorrected errors on page %d!\n", page);
			mtd->ecc_stats.failed++;
		}

	/* The following is actually not really correct, as the stats should
	 * reflect _all_ bitflips, not just the largest one in the latest read.
	 * We could rectify this by reading chip->ecc.bytes at a time,
	 * and accumulating the statistics per read, but at least for now
	 * the additional overhead doesn't seem to warrant the increased
	 * accuracy of the statistics, since the important figure is the
	 * max number of bitflips in a single ECC block returned by this
	 * function. */
	mtd->ecc_stats.corrected += max_bitflips;

	MTD_TRACE("ECC read status: %s%s%s%s, correction count %d\n",
		  ecc_status & ECC_STAT_UNC(cs) ? "Uncorrected " : "",
		  ecc_status & ECC_STAT_ERROR(cs) ? "Corrected " : "",
		  ecc_status & ECC_STAT_OVER(cs) ? "Over limit " : "",
		  ecc_status & (ECC_STAT_UNC(cs) | ECC_STAT_ERROR(cs) |
				ECC_STAT_OVER(cs)) ?  "" : "ok", max_bitflips);

	/* We shouldn't see oob_required for ECC reads. */
	if (oob_required) {
		dev_crit(etx_info->dev, "Need separate read for the OOB\n");
		BUG();
	}

	return max_bitflips;
}

/* Read page with HW ECC */
static int etx_read_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
		uint8_t *buf, int oob_required, int page)
{
	MTD_TRACE("page %d, oobreq %d\n", page, oob_required);
	return etx_read_page_mode(mtd, chip, buf, oob_required, page,
				  ETX_READ_STD);
}

/* Read page with no ECC */
static int etx_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
		uint8_t *buf, int oob_required, int page)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);

	MTD_TRACE("page %d, oobreq %d\n", page, oob_required);
	etx_read_page_mode(mtd, chip, buf, oob_required, page, ETX_READ_RAW);

	if (!oob_required)
		return 0;

	/* Read OOB */
	read_dma(info, info->cmd_cache.page, info->cmd_cache.column,
		 ETX_READ_OOB);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

/* Write page with HW ECC */
/* This is the only place where we know we'll be writing w/ ECC */
static int etx_write_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
		const uint8_t *buf, int oob_required)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);

	MTD_TRACE("oob_required %d\n", oob_required);

	/* The controller can't write data to the oob when ECC is enabled,
	 * so we set oob_required to 0 here and don't process the oob
	 * further even if requested. This could happen for instance if
	 * using nandwrite -o without -n . */
	if (oob_required)
		dev_warn(etx_info->dev, "Tried to write OOB with ECC!\n");
	info->cmd_cache.oob_required = 0;
	info->cmd_cache.write_raw = 0;

	etx_write_dmabuf(mtd, buf, mtd->writesize);

	return 0;
}

/* Write page with no ECC */
/* This is the only place where we know we won't be writing w/ ECC */
static int etx_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
		const uint8_t *buf, int oob_required)
{
	struct chip_info *info = TO_CHIP_INFO(mtd);

	MTD_TRACE("oob_required %d\n", oob_required);

	/* We need this for the upcoming PAGEPROG command */
	info->cmd_cache.oob_required = oob_required;
	info->cmd_cache.write_raw = 1;

	etx_write_dmabuf(mtd, buf, mtd->writesize);

	if (oob_required)
		chip->write_buf(mtd, info->chip.oob_poi, mtd->oobsize);

	return 0;
}

/* Handle commands from MTD NAND layer */
static void etx_nand_command(struct mtd_info *mtd, unsigned int command,
			     int column, int page_addr)
{
	/* We know that an mtd belonging to us is actually only the first
	 * struct in a multi-struct structure. */
	struct chip_info *info = TO_CHIP_INFO(mtd);

	/* Save command so that other parts of the API can figure out
	 * what's actually going on. */
	info->cmd_cache.command = command;

	/* Configure the NFC for the flash chip in question. */
	config_etx(&info->etx_config, info);

	/* Some commands we execute immediately, while some need to be
	 * deferred until we have all the data needed, i.e. for page read,
	 * we can't initiate the read until we know if we are going to be
	 * using raw mode or not.
	 */
	switch (command) {
	case NAND_CMD_READ0:
		MTD_TRACE("READ0 page %d, column %d\n", page_addr, column);
		if (etx_info->setup->ecc_mode == NAND_ECC_HW) {
			/* We do not yet know if the caller wants to
			 * read the page with or without ECC, so we
			 * just store the page number and main/oob flag
			 * here.
			 * TODO: Since the page number arrives via the
			 * read_page call, we don't really need to
			 * store it. */
			info->cmd_cache.page = page_addr;
			info->cmd_cache.column = column;
		} else {
			/* Read the whole page including oob */
			info->cmd_cache.oob_required = 1;
			read_dma(info, page_addr, column, ETX_READ_ALL);
		}
		break;
	case NAND_CMD_READOOB:
		MTD_TRACE("READOOB page %d, column %d\n", page_addr, column);
		/* In contrast to READ0, where nand_base always calls
		 * a read_page_foo function before reading the data,
		 * for READOOB, read_buf is called instead.
		 * We don't want the actual read in read_buf, so
		 * we put it here. */
		read_dma(info, page_addr, column, ETX_READ_OOB);
		break;
	case NAND_CMD_ERASE1:
		MTD_TRACE("ERASE1 page %d\n", page_addr);
		/* Just grab page parameter, wait until ERASE2 to do
		 * something. */
		info->cmd_cache.page = page_addr;
		break;
	case NAND_CMD_ERASE2:
		MTD_TRACE("ERASE2 page %d, do it\n", info->cmd_cache.page);
		/* Off we go! */
		block_erase(info->cmd_cache.page);
		break;
	case NAND_CMD_RESET:
		MTD_TRACE("chip reset\n");
		/* Clear the FIFOs */
		etx_write(1, FIFO_INIT_REG);

		command_and_wait(COMMAND_RESET,
				 INT_STATUS_CMD_END_INT_FL);
		break;
	case NAND_CMD_SEQIN:
		MTD_TRACE("SEQIN column %d, page %d\n", column, page_addr);
		/* Just grab some parameters, then wait until
		 * PAGEPROG to do the actual operation. */
		info->cmd_cache.page = page_addr;
		info->cmd_cache.column = column;
		/* Prepare DMA buffer for data. We don't yet know
		 * how much data there is, so set size to max. */
		init_dmabuf(DMA_BUF_SIZE);
		break;
	case NAND_CMD_PAGEPROG:
		/* Used for both main area and oob */
		MTD_TRACE("PAGEPROG page %d, column %d, w/oob %d, raw %d\n",
			  info->cmd_cache.page, info->cmd_cache.column,
			  info->cmd_cache.oob_required,
			  info->cmd_cache.write_raw);
		write_dma(info, info->cmd_cache.page,
			  info->cmd_cache.column,
			  info->cmd_cache.oob_required,
			  info->cmd_cache.write_raw);
		break;
	case NAND_CMD_READID:
		MTD_TRACE("READID (0x%02x)\n", column);

		/* Read specified ID bytes */
		/* 0x00 would be NAND_READ_ID_ADDR_STD
		 * 0x20 would be NAND_READ_ID_ADDR_ONFI,
		 * but NAND subsystem knows this and sends us the
		 * address values directly */
		etx_write(column, ADDR0_COL_REG);
		etx_write(0, ADDR0_ROW_REG);

		/* Use maximum size of READID data */
		init_dmabuf(MAX_READID_DATA);
		init_dma(etx_info->dma.phys, MAX_READID_DATA);
		etx_write(MAX_READID_DATA, DATA_SIZE_REG);

		/* Send read id command */
		command_and_wait(COMMAND_READ_ID,
				 INT_STATUS_DMA_INT_FL);
		break;
	case NAND_CMD_STATUS:
		MTD_TRACE("STATUS, defer to later read byte\n");
		/* Don't do anything now, wait until we need to
		 * actually read status. */
		break;
	case NAND_CMD_PARAM:
		MTD_TRACE("PARAM (0x%02x)\n", column);

		etx_write(column, ADDR0_COL_REG);
		etx_write(0, ADDR0_ROW_REG);

		/* Use maximum size of PARAM data */
		init_dmabuf(MAX_PARAM_DATA);
		init_dma(etx_info->dma.phys, MAX_PARAM_DATA);
		etx_write(MAX_PARAM_DATA, DATA_SIZE_REG);

		command_and_wait(COMMAND_PARAM,
				 INT_STATUS_DMA_INT_FL);
		break;
	default:
		MTD_TRACE("Unhandled command 0x%02x (col %d, page addr %d)\n",
			  command, column, page_addr);
		break;
	}
}

/*
 * Calculate the number of clock cycles that exceeds a time in ps, minus 1.
 * ALso limit the result so it fits in a specified number of bits.
 */
static uint32_t ps_to_cycles(uint64_t clockperiod_ps, uint32_t ps, int maxbits)
{
	uint32_t tmp = 0;
	const uint32_t max = (1 << maxbits) - 1;

	while (ps > clockperiod_ps) {
		ps -= clockperiod_ps;
		tmp++;
		if (tmp == max)
			return max;
	}

	return tmp;
}

static void etx_program_timings(struct device *dev, uint32_t clockrate, int mode)
{
	const struct nand_sdr_timings *t;
	uint32_t reg, tmp, tCCS;
	uint64_t clk_period;	/* in pico seconds */
	uint32_t io = 5000;	/* additional I/O delay */

	/* mode field is a bit mask of supported modes, bit 0 for mode 0,
	 * bit 1 for mode 1, bit 2 for mode 2, bit 3 for mode 3, etc. */
	if (mode)
		mode = fls(mode) - 1;

	t = onfi_async_timing_mode_to_sdr_timings(mode);
	if (IS_ERR(t)) {
		dev_err(dev, "Can't get NAND ONFi timings!\n");
		return;
	}

	clk_period = 1000000000000ULL / clockrate;

	tCCS = 5 * t->tWC_min;
	reg = (ps_to_cycles(clk_period, t->tWHR_min + io, 6) << 24) |
	      (ps_to_cycles(clk_period, t->tRHW_min + io, 6) << 16) |
	      (ps_to_cycles(clk_period, t->tADL_min + io, 6) << 8) |
	       ps_to_cycles(clk_period, tCCS + io, 6);
	etx_write(reg, TIME_SEQ_0_REG);

	reg = (ps_to_cycles(clk_period, t->tWW_min + io, 6) << 16) |
	      (ps_to_cycles(clk_period, t->tRR_min + io, 6) << 8) |
	       ps_to_cycles(clk_period, t->tWB_max + io, 6);
	etx_write(reg, TIME_SEQ_1_REG);

	/* tRWH [7:4]  RE# or WE# high hold time */
	/* tRWP [3:0]  RE# or WE# pulse width */
	tmp = max(t->tREH_min, t->tWH_min) + io;
	reg = ps_to_cycles(clk_period, tmp, 4) << 4;
	/* Note: tRWP requires an extra cycle */
	tmp = max(t->tRP_min,  t->tWP_min) + io;
	tmp = ps_to_cycles(clk_period, tmp, 4) + 1;
	reg |= min(tmp, (uint32_t)(1 << 4) - 1);
	etx_write(reg, TIMINGS_ASYN_REG);
}

#if !defined(CONFIG_NAND_OOB_FS_BYTES)
#define CONFIG_NAND_OOB_FS_BYTES 0
#warning "CONFIG_NAND_OOB_FS_BYTES not defined, assuming 0 bytes for file system"
#endif

/* Select an appropriate ECC BCH strength */
static u32 nand_select_ecc(struct mtd_info *mtd, u32 ecc_blksize)
{
	u32 avail = mtd->oobsize - ECC_OFFSET - CONFIG_NAND_OOB_FS_BYTES;
	u32 bytes_per_codeword = (avail * ecc_blksize) / mtd->writesize;
	u8 lut[] = { 56, 42, 28, 14, 7, 4 };
	u8 bch[] = { 32, 24, 16,  8, 4, 2 };
	int i;

	for (i = 0; i < sizeof(lut)/sizeof(lut[0]); i++)
		if (bytes_per_codeword >= lut[i])
			return bch[i];

	return 0;
}

static void setup_ecc_layout(struct mtd_info *mtd, struct nand_chip *chip,
			     struct nand_ecclayout *ecclayout)
{
	int i;

	ecclayout->eccbytes = chip->ecc.bytes * (mtd->writesize / chip->ecc.size);

	for (i = 0; i < ecclayout->eccbytes; i++)
		ecclayout->eccpos[i] = i + ECC_OFFSET;

	/* reserved marker already included in ecclayout->eccbytes */
	ecclayout->oobfree->offset = ecclayout->eccbytes + ECC_OFFSET;

	/* all OOB bytes from oobfree->offset till end off OOB are free */
	ecclayout->oobfree->length = mtd->oobsize - ecclayout->oobfree->offset;

	chip->ecc.layout = ecclayout;
}

/**** Top level probing and device management ****/

/* Get configuration from device tree */
#ifdef CONFIG_OF
static int etx_get_dt_config(struct platform_device *pdev)
{
	struct etx_setup *etx_setup = dev_get_platdata(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	int res, timings;

	if (!np) {
		dev_err(dev, "No configuration\n");
		return -EINVAL;
	}

	/* ECC parameters */
	res = of_get_nand_ecc_mode(np);
	if (res == NAND_ECC_HW || res == NAND_ECC_SOFT_BCH)
		etx_setup->ecc_mode = res;
	else
		dev_warn(dev, "Unsupported/unset ECC mode, using default\n");

	res = of_get_nand_ecc_strength(np);
	/* NFC can handle 2 bits but ECC_BYTES macro can't and it's
	 * highly unlikely we'd ever need to support 2 bits correction
	 * in practice, so don't allow that case here. */
	if (res < 0 ||
	    (res != 4 && res != 8 && res != 16 && res != 24 && res != 32))
		dev_warn(dev, "Unsupported ECC strength, using default\n");
	else
		etx_setup->ecc_strength = res;

	res = of_get_nand_ecc_step_size(np);
	if (res < 0 || (res != 256 && res != 512 && res != 1024))
		dev_warn(dev, "Unsupported ECC step size, using default\n");
	else
		etx_setup->ecc_blksize = res;

	etx_setup->on_flash_bbt = of_get_nand_on_flash_bbt(np);

	timings = sizeof(etx_setup->timings) / sizeof(u32);
	res = of_property_read_u32_array(np, "timings",
					 (u32 *)&etx_setup->timings, timings);
	if (res < 0) {
		dev_warn(dev, "NAND timing setup missing, using defaults\n");
		/* Default values have been set, but we don't know what
		 * read_u32_array does if it fails during parsing, so reset
		 * them here again. */
		memcpy(&etx_setup->timings, &default_mode0_pll_enabled,
		       sizeof(etx_setup->timings));
	}

	return 0;
}
#else
static int etx_get_dt_config(struct platform_device *pdev)
{
	return 0;
}
#endif

/* Per-NAND-chip initialization. */
static __init
struct mtd_info *etx_nand_flash_probe(struct platform_device *pdev,
				      unsigned bank_no)
{
	struct chip_info *this;
	struct device *dev = &pdev->dev;
	struct nand_onfi_params *p;
	int pages_per_block, ecc_blksize, ecc_strength;
#if defined(LINUX)
	static const char *part_probe_types[]
		= { "cmdlinepart", "RedBoot", "ofpart", NULL };
	struct mtd_part_parser_data ppdata;

	ppdata.of_node = pdev->dev.of_node;
#else
	(void)dev;
#endif

	/* Allocate memory for MTD device structure and private data */
	this = devm_kzalloc(dev, sizeof(struct chip_info), GFP_KERNEL);
	if (!this) {
		dev_err(dev, "Unable to allocate local device structure.\n");
		return NULL;
	}

#if defined(LINUX)
	this->mtd = devm_kzalloc(dev, sizeof(struct mtd_info), GFP_KERNEL);
	if (!this->mtd) {
		dev_err(dev, "Unable to allocate mtd structure.\n");
		goto outta_here;
	}
#else
	/* Hook into U-Boot data */
	this->mtd = &nand_info[0];
	memset(this->mtd, 0, sizeof(struct mtd_info));
#endif
	this->chip.priv = this;

	/* Link the private data with the mtd structure */
	this->mtd->priv = &this->chip;

	/* Set up basic config for NAND controller hardware */

	/* Device control. */
#ifdef ETX_CE_BANK_SEL
	/* Separate chips regarded as different banks. */
	this->etx_config.mem_ctrl = MEM_CTRL_BANK_SEL(bank_no);
#else
	/* Separate chips regarded as different chip selects. */
	this->etx_config.mem_ctrl = MEM_CTRL_MEM_CE(bank_no);
#endif

#ifdef ETX_RB_WIRED_AND
	/* Ready/busy from nand flash arrives via wired-AND for device */
	this->etx_config.mem_status_mask = STATUS_MEM_ST(cs);
#else
	/* Ready/busy from nand flash as separate per-device signals */
	this->etx_config.mem_status_mask = STATUS_MEM_ST(bank_no);
#endif

	/* Our interface to the mtd API */
	this->chip.cmdfunc = etx_nand_command;
	this->chip.cmd_ctrl = etx_dummy_cmd_ctrl;
	this->chip.dev_ready = etx_dev_ready;
	this->chip.read_byte = etx_read_byte;
	this->chip.read_buf = etx_read_dmabuf;
	this->chip.write_buf = etx_write_dmabuf;

	/* ONFi Mode 0 timings */
	etx_program_timings(dev, CONFIG_SYS_NAND_CLOCK, 0);

	/* Scan to find existence of the device */
	/* Note that the NFC is not completely set up at this time, but
	 * that is ok as we only need to identify the device here. */
	if (nand_scan_ident(this->mtd, 1, NULL))
		goto outta_here;

	/* Adjust timing to fastest supported ONFi mode */
	etx_program_timings(dev, CONFIG_SYS_NAND_CLOCK,
		onfi_get_async_timing_mode(&this->chip));

	/* If this is a Micron device with on-die ECC enabled by default, we'll
	 * simply turn off ECC in this driver.
	 */
	this->ecc_enabled = 1;
	p = &this->chip.onfi_params;
	if (p->jedec_id == NAND_MFR_MICRON) {
		switch (this->chip.readid_data[1])
		{
		case 0xf1:	/* MT29F1G08ABADA */
		case 0xa1:	/* MT29F1G08ABBDA */
		case 0xb1:	/* MT29F1G16ABBDA */

		case 0xaa:	/* MT29F2G08ABBEA */
		case 0xba:	/* MT29F2G16ABBEA */
		case 0xda:	/* MT29F2G08ABAEA */
		case 0xca:	/* MT29F2G16ABAEA */

		case 0xdc:	/* MT29F4G08ABADA */
		case 0xcc:	/* MT29F4G16ABADA */
		case 0xac:	/* MT29F4G08ABBDA */
		case 0xbc:	/* MT29F4G16ABBDA */
		case 0xa3:	/* MT29F8G08ADBDA */
		case 0xb3:	/* MT29F8G16ADBDA */
		case 0xd3:	/* MT29F8G08ADADA / MT29F16G08AJADA */
		case 0xc3:	/* MT29F8G16ADADA */

			/* if internal ECC is enabled, we can't do the ECC */
			if (this->chip.readid_data[4] & 0x80) {
				this->ecc_enabled = 0;
				dev_info(dev, "NAND device uses on-die ECC\n");
			}
			break;
		default:
			break;
		}
	}

	/* Select an appropriate ECC BCH strength */
	ecc_strength = etx_info->setup->ecc_strength;
	if (ecc_strength == 0)
		ecc_strength = nand_select_ecc(this->mtd,
						etx_info->setup->ecc_blksize);
	if (!ecc_strength) {
		dev_err(dev, "NAND device unusable as OOB is too small!\n");
		goto outta_here;
	}

	/* Set up rest of config for NAND controller hardware */

	/* set ECC block size and pages per block */
	pages_per_block = this->mtd->erasesize / this->mtd->writesize;
	ecc_blksize = etx_info->setup->ecc_blksize;
	this->etx_config.control = CONTROL_ECC_BLOCK_SIZE(ecc_blksize) |
				   CONTROL_BLOCK_SIZE(pages_per_block);

	/* Set up ECC control and offset of ECC data */
	/* We don't use the threshold capability of the controller, as we
	 * let mtd handle that, so set the threshold to same as capability. */
	this->etx_config.ecc_ctrl = ECC_CTRL_ECC_THRESHOLD(ecc_strength) |
				    ECC_CTRL_ECC_CAP(ecc_strength);
	/* Put ECC bytes into OOB at an offset, to skip bad block marker */
	this->etx_config.ecc_offset = this->mtd->writesize + ECC_OFFSET;

	/* Since we've now completed the configuration, we need to force it to
	 * be written to the NFC, else the caching in config_etx will leave
	 * the etx_config values written since nand_scan_ident unwritten. */
	config_etx(&this->etx_config, NULL);

	/* ECC setup */

	/* ECC API */
	/* Override the following functions when using hardware ECC,
	 * otherwise we use the defaults set up by nand_base. */
	if (etx_info->setup->ecc_mode == NAND_ECC_HW) {
		this->chip.ecc.read_page = etx_read_page_hwecc;
		this->chip.ecc.read_page_raw = etx_read_page_raw;
		this->chip.ecc.write_page = etx_write_page_hwecc;
		this->chip.ecc.write_page_raw = etx_write_page_raw;
	}

	this->chip.ecc.mode = etx_info->setup->ecc_mode;
	this->chip.ecc.size = ecc_blksize;
	this->chip.ecc.strength = ecc_strength;
	this->chip.ecc.bytes = ECC_BYTES_CAD(ecc_strength, ecc_blksize);

	setup_ecc_layout(this->mtd, &this->chip, &eva_oobinfo);

	/* Note that with a device that uses on-die ECC, all of the above
	 * ECC info is wrong. Since we don't know what bytes are actually
	 * used by the on-die ECC, we'll leave them as is.
	 */

	/* We set the bitflip_threshold at 75% of the error correction
	 * level to get some margin in case bitflips happen in parts of the
	 * flash that we don't read that often. */
	/* We add 1 so that an ECC strength of 1 gives us a threshold of 1;
	 * rather academic though, as we only support BCH anyway... */
	this->mtd->bitflip_threshold = (ecc_strength + 1) * 3 / 4;

	if (etx_info->setup->on_flash_bbt)
		/* Enable the use of a flash based bad block table.
		 * Since the OOB is not ECC protected we don't put BBT stuff
		 * there. We also don't mark user-detected badblocks as bad in
		 * their oob, only in the BBT, to avoid potential chip problems
		 * when attempting to write bad blocks (writing to bad blocks
		 * is not recommended according to flash manufacturers). */
		this->chip.bbt_options = NAND_BBT_USE_FLASH | NAND_BBT_NO_OOB |
					 NAND_BBT_NO_OOB_BBM;

#if !defined(LINUX)
	/* Don't generate a BBT at the start, check for BB markers as we go */
	this->chip.options = NAND_SKIP_BBTSCAN;
#endif

	this->chip.controller = etx_info->controller;

	/* Finalize NAND scan, including BBT if requested */
	if (nand_scan_tail(this->mtd))
		goto outta_here;

#if defined(LINUX)
	/* First look for RedBoot table or partitions on the command
	 * line, these take precedence over device tree information */
	mtd_device_parse_register(this->mtd, part_probe_types, &ppdata,
				  NULL, 0);

	this->mtd->dev.parent = &pdev->dev;
#else
	if (nand_register(0))
		return NULL;
#endif

	return this->mtd;

outta_here:
#if defined(LINUX)
	kfree(this->mtd);
#endif
	kfree(this);
	return NULL;
}

/* Main probe function. Called to probe and set up device. */
static int __init etx_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtd_info *main_mtd = NULL;
	struct mtd_info *mtds[CONFIG_MTD_NAND_EVATRONIX_CHIPS];
	struct etx_setup *etx_setup;
	struct nand_hw_control *controller;
	int err = 0;

	MTD_TRACE("Initializing Evatronix NANDFLASH-CTRL driver\n");

	/* etx_info is where we keep runtime information about the NFC */
	etx_info = devm_kzalloc(dev, sizeof(*etx_info), GFP_KERNEL);
	if (!etx_info) {
		dev_err(dev, "Unable to allocate device control structure.\n");
		return -ENOMEM;
	}
	etx_info->dev = dev;

	/* Set up a controller struct to act as shared lock for all devices */
	controller = devm_kzalloc(dev, sizeof(*controller), GFP_KERNEL);
	if (controller == NULL) {
		dev_err(dev, "Unable to allocate controller structure.\n");
		return -ENOMEM;
	}
	spin_lock_init(&controller->lock);
	init_waitqueue_head(&controller->wq);
	etx_info->controller = controller;

	/* etx_setup is where we keep settings from DT, in digested form */
	etx_setup = devm_kzalloc(dev, sizeof(*etx_setup), GFP_KERNEL);
	if (!etx_setup) {
		dev_err(dev, "Unable to allocate device setup structure.\n");
		return -ENOMEM;
	}
	pdev->dev.platform_data = etx_setup;
	etx_info->setup = etx_setup;

	/* Default parameters, potentially overriden by DT */
	etx_setup->ecc_mode = NAND_ECC_HW;
	etx_setup->ecc_strength = 0;	/* calculated */
	etx_setup->ecc_blksize = 512;
	memcpy(&etx_setup->timings, &default_mode0_pll_enabled,
	       sizeof(etx_setup->timings));

	/* Get config from device tree. */
	err = etx_get_dt_config(pdev);
	if (err) {
		dev_err(dev, "Can't retrieve dt config\n");
		return err;
	}

	MTD_TRACE("ECC using %s mode with strength %i and block size %i.\n",
		etx_setup->ecc_mode == NAND_ECC_HW ? "hardware" : "software",
		etx_setup->ecc_strength, etx_setup->ecc_blksize);

	/* Initialize interrupts and DMA etc. */
	err = etx_init_resources(pdev);
	if (err)
		return err;

	setup_etx_timing(etx_setup);

#ifndef POLLED_XFERS
	init_waitqueue_head(&etx_info->irq.wq);
#endif

	mtds[0] = etx_nand_flash_probe(pdev, cs);
	if (mtds[0] == NULL)
		return -ENXIO;

#if CONFIG_MTD_NAND_EVATRONIX_CHIPS > 1
	mtds[1] = etx_nand_flash_probe(pdev, 1);
	if (mtds[1] != NULL) {
		/* Two devices found, combine them into one */
		main_mtd = mtd_concat_create(mtds, 2, "axisflash");

		if (main_mtd == NULL) {
			dev_err(dev, "mtd concat failed, using first chip\n");
			main_mtd = mtds[0];
		}
	}
#endif
	if (main_mtd == NULL) {
		/* No concat device, use first chip only */
		main_mtd = mtds[0];
	}

	/* We have our mtd now, insert call to mapping driver here. */

	return err;
}

#ifdef CONFIG_OF
static const struct of_device_id etx_nand_id_table[] = {
	{ .compatible = "evatronix,nandflash-ctrl" },
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, etx_nand_id_table);
#endif

#if defined(LINUX)
static struct platform_driver etx_nand_driver = {
	.driver = {
		.name   = "evatronix-nand",
		.owner  = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(etx_nand_id_table),
#endif
	},
	.probe = etx_nand_probe,
};

module_platform_driver(etx_nand_driver);

MODULE_AUTHOR("Ricard Wanderlof <ricardw@axis.com>");
MODULE_DESCRIPTION("Evatronix NANDFLASH-CTRL driver");
MODULE_LICENSE("GPL");
#else
/* U-Boot */
static struct platform_device pdev;
void board_nand_init(void)
{
	if (etx_nand_probe(&pdev))
		puts("Cadence NAND init failed\n");
}
#endif

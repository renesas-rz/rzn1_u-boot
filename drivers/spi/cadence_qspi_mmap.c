/*
 * (C) Copyright 2016 Renesas Electronics Europe Ltd.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <spi.h>
#include <spi_flash.h>
#include "../mtd/spi/sf_internal.h"
#include "cadence_qspi.h"

#ifndef CONFIG_SPI_REGISTER_FLASH
#error Requires the global define CONFIG_SPI_REGISTER_FLASH in your config file
#endif

void cadence_qspi_apb_controller_init_mmap(struct cadence_spi_platdata *plat)
{
	unsigned reg;

	/* enable direct mode */
	reg = readl(plat->regbase + CQSPI_REG_CONFIG);
	reg |= CQSPI_REG_CONFIG_ENABLE;
	reg |= CQSPI_REG_CONFIG_DIRECT;
	writel(reg, plat->regbase + CQSPI_REG_CONFIG);

	/* Enable AHB write protection, to prevent random write access
	 * that could nuke the flash memory. Instead you have to explicitly
	 * use 'sf write' to change the flash.
	 * We set the protection range to the maximum, so the whole
	 * address range should be protected, regardless of what size
	 * is actually used.
	 */
	writel(0, plat->regbase + CQSPI_REG_LOWER_WRITE_PROTECT);
	writel(~0, plat->regbase + CQSPI_REG_UPPER_WRITE_PROTECT);
	writel(CQSPI_REG_WRPROT_ENABLE,
		plat->regbase + CQSPI_REG_WRITE_PROTECT_CTRL);
}

/*
 * This is an addition to the general spi slave interface, this function
 * is called if a flash has been detected, to allow the slave to configure
 * itself.
 * In our case here, we need to know the number of address lines and stuff
 */
void spi_slave_register_flash(struct spi_flash *flash)
{
	struct spi_slave *slave = flash->spi;
	struct udevice *bus = slave->dev->parent;
	struct cadence_spi_platdata *plat = bus->platdata;
	uint32_t rd_reg, wr_reg, reg;

	if (flash->memory_map == NULL)
		return;

	debug("%s dummy %d - page %d sector %d erase %d(ffs %d)\n", __func__,
	      flash->dummy_cycles, flash->page_size, flash->sector_size,
	      flash->erase_size, ffs(flash->erase_size));
	debug(" CMD rd %2x wr %02x addr:%d\n", flash->read_cmd,
		flash->write_cmd, slave->addressing_bytes);

	cadence_qspi_apb_controller_disable(plat->regbase);

	/* Configure the device size and address bytes */
	reg = readl(plat->regbase + CQSPI_REG_SIZE);
	/* Clear the previous value */
	reg &= ~(CQSPI_REG_SIZE_PAGE_MASK << CQSPI_REG_SIZE_PAGE_LSB);
	reg &= ~(CQSPI_REG_SIZE_BLOCK_MASK << CQSPI_REG_SIZE_BLOCK_LSB);
	reg |= (flash->page_size << CQSPI_REG_SIZE_PAGE_LSB);
	reg |= ((ffs(flash->erase_size)-1) << CQSPI_REG_SIZE_BLOCK_LSB);
	writel(reg, plat->regbase + CQSPI_REG_SIZE);

	/* 3 or 4-byte addressing */
	if (slave->addressing_bytes < 3)
		slave->addressing_bytes = 3;

	reg = readl(plat->regbase + CQSPI_REG_SIZE);
	reg &= ~0xf;
	reg |= slave->addressing_bytes - 1;
	writel(reg, plat->regbase + CQSPI_REG_SIZE);

	rd_reg = (flash->read_cmd << CQSPI_REG_RD_INSTR_OPCODE_LSB);

	switch (flash->read_cmd) {
	case CMD_READ_ARRAY_SLOW:		/* 0x03 */
	case CMD_READ_ARRAY_SLOW_4B:		/* 0x13 */
	case CMD_READ_ARRAY_FAST:		/* 0x0b */
	case CMD_READ_ARRAY_FAST_4B:		/* 0x0c */
		break;
	case CMD_READ_DUAL_IO_FAST:		/* 0xbb */
	case CMD_READ_DUAL_IO_FAST_4B:		/* 0xbc */
		rd_reg |= (CQSPI_INST_TYPE_DUAL << CQSPI_REG_RD_INSTR_TYPE_ADDR_LSB);
	case CMD_READ_DUAL_OUTPUT_FAST:		/* 0x3b */
	case CMD_READ_DUAL_OUTPUT_FAST_4B:	/* 0x3c */
		rd_reg |= (CQSPI_INST_TYPE_DUAL << CQSPI_REG_RD_INSTR_TYPE_DATA_LSB);
		break;
	case CMD_READ_QUAD_IO_FAST:		/* 0xeb */
	case CMD_READ_QUAD_IO_FAST_4B:		/* 0xec */
		rd_reg |= (CQSPI_INST_TYPE_QUAD << CQSPI_REG_RD_INSTR_TYPE_ADDR_LSB);
	case CMD_READ_QUAD_OUTPUT_FAST:		/* 0x6b */
	case CMD_READ_QUAD_OUTPUT_FAST_4B:	/* 0x6c */
		rd_reg |= (CQSPI_INST_TYPE_QUAD << CQSPI_REG_RD_INSTR_TYPE_DATA_LSB);
		break;
	default:
		debug("%s unsupported read command %02x\n", __func__, flash->read_cmd);
	}

	rd_reg |= (flash->dummy_cycles & CQSPI_REG_RD_INSTR_DUMMY_MASK) << CQSPI_REG_RD_INSTR_DUMMY_LSB;
	writel(rd_reg, plat->regbase + CQSPI_REG_RD_INSTR);
	writel(0xff, plat->regbase + CQSPI_REG_MODE_BIT);

	/* Write cmd */
	wr_reg = (flash->write_cmd << CQSPI_REG_RD_INSTR_OPCODE_LSB);
	if  (flash->write_cmd == CMD_QUAD_PAGE_PROGRAM)
		wr_reg |= (CQSPI_INST_TYPE_QUAD << CQSPI_REG_RD_INSTR_TYPE_DATA_LSB);
	writel(wr_reg, plat->regbase + CQSPI_REG_WR_INSTR);

	cadence_qspi_apb_controller_enable(plat->regbase);
}

int cadence_spi_xfer_mmap(struct udevice *dev, unsigned int bitlen,
	const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct cadence_spi_platdata *plat = bus->platdata;
	int err = 0;

	if (flags & SPI_XFER_MMAP_WRITE)
		writel(0, plat->regbase + CQSPI_REG_WRITE_PROTECT_CTRL);

	if (flags & (SPI_XFER_MMAP_END | SPI_XFER_END))
		writel(CQSPI_REG_WRPROT_ENABLE,
			plat->regbase + CQSPI_REG_WRITE_PROTECT_CTRL);

	return err;
}

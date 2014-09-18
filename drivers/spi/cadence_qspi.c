/*
 * (C) Copyright 2014 Altera Corporation <www.altera.com>
 * (C) Copyright 2015 Renesas Electronics Europe Ltd.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <asm/errno.h>
#include <malloc.h>
#include <spi.h>
#include <spi_flash.h>
#include "../mtd/spi/sf_internal.h"
#include "cadence_qspi.h"

#ifndef CONFIG_SPI_REGISTER_FLASH
#error Requires the global define CONFIG_SPI_REGISTER_FLASH in your config file
#endif

struct cadence_qspi_slave {
	struct spi_slave slave;
	struct spi_flash *flash;
	unsigned int	mode;
	unsigned int	max_hz;
	const struct cadence_qspi	*regbase;
	size_t		cmd_len;
	u8		cmd_buf[32];
	unsigned int	qspi_calibrated_hz;
	unsigned int	qspi_calibrated_cs;
	unsigned int	initialised;
};

/* Users can specify one or more QSPI controllers */
static uint32_t qspi_bases[] = { CONFIG_CQSPI_BASE };
static uint32_t qspi_ahb_bases[] = { CONFIG_CQSPI_AHB_BASE };
static unsigned int qspi_modes[] = { CONFIG_CADENCE_QSPI_MODE };

#define to_cadence_qspi_slave(s)		\
		container_of(s, struct cadence_qspi_slave, slave)

#define CQSPI_CAL_DELAY(tdelay_ns, tref_ns, tsclk_ns)	\
	((tdelay_ns) > (tsclk_ns)) ? ((((tdelay_ns) - (tsclk_ns)) / (tref_ns))) : 0

static unsigned int
cadence_qspi_apb_cmd2addr(
	const unsigned char *addr_buf,
	unsigned int addr_width)
{
	unsigned int addr = 0;

	while (addr_width--)
		addr = (addr << 8) | *addr_buf++;

	return addr;
}

static void cadence_qspi_apb_controller_enable(struct cadence_qspi_slave *qslave)
{
	setbits_le32(&qslave->regbase->cfg, CQSPI_REG_CONFIG_ENABLE_MASK);
}

static void cadence_qspi_apb_controller_disable(struct cadence_qspi_slave *qslave)
{
	clrbits_le32(&qslave->regbase->cfg, CQSPI_REG_CONFIG_ENABLE_MASK);
}

/* Return 1 if idle, otherwise return 0 (busy). */
static unsigned int cadence_qspi_wait_idle(struct cadence_qspi_slave *qslave)
{
	unsigned int start, count = 0;
	/* timeout in unit of ms */
	unsigned int timeout = 5000;

	start = get_timer(0);
	while (get_timer(start) < timeout) {
		if ((readl(&qslave->regbase->cfg) >>
			CQSPI_REG_CONFIG_IDLE_LSB) & 0x1)
			count++;
		else
			count = 0;
		/*
		 * Ensure the QSPI controller is in true idle state after
		 * reading back the same idle status consecutively
		 */
		if (count >= CQSPI_POLL_IDLE_RETRY)
			return 1;
	}

	/* Timeout, still in busy mode. */
	printf("QSPI: QSPI is still busy after poll for %d times.\n",
	       CQSPI_REG_RETRY);
	return 0;
}

static void cadence_qspi_apb_readdata_capture(
	struct cadence_qspi_slave *qslave,
	unsigned int delay)
{
	unsigned int reg;

	cadence_qspi_apb_controller_disable(qslave);

	reg = readl(&qslave->regbase->rddatacap);

	reg |= (1 << CQSPI_REG_RD_DATA_CAPTURE_BYPASS_LSB);

	reg &= ~(CQSPI_REG_RD_DATA_CAPTURE_DELAY_MASK
		<< CQSPI_REG_RD_DATA_CAPTURE_DELAY_LSB);

	reg |= ((delay & CQSPI_REG_RD_DATA_CAPTURE_DELAY_MASK)
		<< CQSPI_REG_RD_DATA_CAPTURE_DELAY_LSB);

	writel(reg, &qslave->regbase->rddatacap);

	cadence_qspi_apb_controller_enable(qslave);
}

static unsigned int cadence_qspi_apb_config_baudrate_div(
	struct cadence_qspi_slave *qslave,
	unsigned int ref_clk_hz,
	unsigned int sclk_hz)
{
	unsigned int reg;
	unsigned int div;
	unsigned int actual_div;

	cadence_qspi_apb_controller_disable(qslave);
	reg = readl(&qslave->regbase->cfg);
	reg &= ~(CQSPI_REG_CONFIG_BAUD_MASK << CQSPI_REG_CONFIG_BAUD_LSB);

	if (ref_clk_hz <= sclk_hz)
		div = 1;
	else
		div = (ref_clk_hz + sclk_hz - 1) / sclk_hz;

	if (div > 32)
		div = 32;

	div = ((div + 1) / 2) - 1;
	actual_div = (div + 1) << 1;

	debug("%s: ref_clk %dHz sclk %dHz Div 0x%x\n", __func__,
	      ref_clk_hz, sclk_hz, div);
	debug("%s: output clock is %dHz (div = %d)\n", __func__,
	      ref_clk_hz / actual_div, actual_div);

	div = (div & CQSPI_REG_CONFIG_BAUD_MASK) << CQSPI_REG_CONFIG_BAUD_LSB;
	reg |= div;
	writel(reg, &qslave->regbase->cfg);

	cadence_qspi_apb_controller_enable(qslave);

	return ref_clk_hz / actual_div;
}

static void cadence_qspi_apb_set_clk_mode(
	struct cadence_qspi_slave *qslave,
	unsigned int clk_pol,
	unsigned int clk_pha)
{
	unsigned int reg;

	cadence_qspi_apb_controller_disable(qslave);
	reg = readl(&qslave->regbase->cfg);
	reg &= ~((1 << CQSPI_REG_CONFIG_CLK_POL_LSB) |
		(1 << CQSPI_REG_CONFIG_CLK_PHA_LSB));

	reg |= !!clk_pol << CQSPI_REG_CONFIG_CLK_POL_LSB;
	reg |= !!clk_pha << CQSPI_REG_CONFIG_CLK_PHA_LSB;

	writel(reg, &qslave->regbase->cfg);

	cadence_qspi_apb_controller_enable(qslave);
}

static void cadence_qspi_apb_chipselect(
	struct cadence_qspi_slave *qslave,
	unsigned int chip_select,
	unsigned int decoder_enable)
{
	unsigned int reg;

	cadence_qspi_apb_controller_disable(qslave);

	debug("%s : chipselect %d decode %d\n", __func__, chip_select,
	      decoder_enable);

	reg = readl(&qslave->regbase->cfg);
	/* decoder */
	if (decoder_enable) {
		reg |= CQSPI_REG_CONFIG_DECODE_MASK;
	} else {
		reg &= ~CQSPI_REG_CONFIG_DECODE_MASK;
		/* Convert CS if without decoder.
		 * CS0 to 4b'1110
		 * CS1 to 4b'1101
		 * CS2 to 4b'1011
		 * CS3 to 4b'0111
		 */
		chip_select = 0xF & ~(1 << chip_select);
	}

	reg &= ~(CQSPI_REG_CONFIG_CHIPSELECT_MASK
			<< CQSPI_REG_CONFIG_CHIPSELECT_LSB);
	reg |= (chip_select & CQSPI_REG_CONFIG_CHIPSELECT_MASK)
			<< CQSPI_REG_CONFIG_CHIPSELECT_LSB;
	writel(reg, &qslave->regbase->cfg);

	cadence_qspi_apb_controller_enable(qslave);
}

static void cadence_qspi_apb_delay(
	struct cadence_qspi_slave *qslave,
	unsigned int ref_clk, unsigned int sclk_hz,
	unsigned int tshsl_ns, unsigned int tsd2d_ns,
	unsigned int tchsh_ns, unsigned int tslch_ns)
{
	unsigned int ref_clk_ns;
	unsigned int sclk_ns;
	unsigned int tshsl, tchsh, tslch, tsd2d;
	unsigned int reg;

	cadence_qspi_apb_controller_disable(qslave);

	/* Convert to ns. */
	ref_clk_ns = 1000000000 / ref_clk;

	/* Convert to ns. */
	sclk_ns = 1000000000 / sclk_hz;

	tshsl = CQSPI_CAL_DELAY(tshsl_ns, ref_clk_ns, sclk_ns);
	tchsh = CQSPI_CAL_DELAY(tchsh_ns, ref_clk_ns, sclk_ns);
	tslch = CQSPI_CAL_DELAY(tslch_ns, ref_clk_ns, sclk_ns);
	tsd2d = CQSPI_CAL_DELAY(tsd2d_ns, ref_clk_ns, sclk_ns);

	reg = (tshsl & CQSPI_REG_DELAY_TSHSL_MASK)
			<< CQSPI_REG_DELAY_TSHSL_LSB;
	reg |= (tchsh & CQSPI_REG_DELAY_TCHSH_MASK)
			<< CQSPI_REG_DELAY_TCHSH_LSB;
	reg |= (tslch & CQSPI_REG_DELAY_TSLCH_MASK)
			<< CQSPI_REG_DELAY_TSLCH_LSB;
	reg |= (tsd2d & CQSPI_REG_DELAY_TSD2D_MASK)
			<< CQSPI_REG_DELAY_TSD2D_LSB;
	debug("%s: delay reg = 0x%X\n", __func__, reg);
	writel(reg, &qslave->regbase->delay);

	cadence_qspi_apb_controller_enable(qslave);
}

static int cadence_qspi_apb_exec_flash_cmd(
	struct cadence_qspi_slave *qslave,
	unsigned int reg)
{
	unsigned int retry = CQSPI_REG_RETRY;

	/* Write the CMDCTRL without start execution. */
	writel(reg, &qslave->regbase->flashcmd);
	/* Start execute */
	reg |= CQSPI_REG_CMDCTRL_EXECUTE_MASK;
	writel(reg, &qslave->regbase->flashcmd);

	while (retry--) {
		reg = readl(&qslave->regbase->flashcmd);
		if ((reg & CQSPI_REG_CMDCTRL_INPROGRESS_MASK) == 0)
			break;
		udelay(1);
	}

	if (!retry) {
		printf("QSPI: flash command execution timeout\n");
		return -EIO;
	}

	/* Polling QSPI idle status. */
	if (!cadence_qspi_wait_idle(qslave))
		return -EIO;

	return 0;
}

/* For command RDID, RDSR. */
static int cadence_qspi_apb_command_read(
	struct cadence_qspi_slave *qslave,
	unsigned int cmdlen, const u8 *cmdbuf,
	unsigned int rxlen, u8 *rxbuf)
{
	unsigned int reg;
	int status;
	uint32_t rx[2];

	if (!cmdlen || rxlen > CQSPI_STIG_DATA_LEN_MAX || rxbuf == NULL) {
		printf("QSPI: Invalid input arguments cmdlen %d rxlen %d\n",
		       cmdlen, rxlen);
		return -EINVAL;
	}

	reg = cmdbuf[0] << CQSPI_REG_CMDCTRL_OPCODE_LSB;
	reg |= 1 << CQSPI_REG_CMDCTRL_RD_EN_LSB;

	/* 0 means 1 byte. */
	reg |= ((rxlen - 1) & CQSPI_REG_CMDCTRL_RD_BYTES_MASK)
		<< CQSPI_REG_CMDCTRL_RD_BYTES_LSB;
	status = cadence_qspi_apb_exec_flash_cmd(qslave, reg);
	if (status != 0)
		return status;

	rx[0] = readl(&qslave->regbase->flashcmdrddatalo);
	if (rxlen > 4)
		rx[1] = readl(&qslave->regbase->flashcmdrddataup);
	memcpy(rxbuf, rx, rxlen);

	return 0;
}

/* For commands: WRSR, WREN, WRDI, CHIP_ERASE, BE, etc. */
static int cadence_qspi_apb_command_write(
	struct cadence_qspi_slave *qslave,
	unsigned int cmdlen,
	const u8 *cmdbuf, unsigned int txlen, const u8 *txbuf)
{
	u32 reg;

	if (!cmdlen || cmdlen > 5 || txlen > 8 || cmdbuf == NULL) {
		printf("QSPI: Invalid input arguments cmdlen %d txlen %d\n",
		       cmdlen, txlen);
		return -EINVAL;
	}

	reg = cmdbuf[0] << CQSPI_REG_CMDCTRL_OPCODE_LSB;

	if (cmdlen == 4 || cmdlen == 5) {
		/* Get address */
		uint32_t addr_value = cadence_qspi_apb_cmd2addr(
				&cmdbuf[1],
				cmdlen >= 5 ? 4 : 3);

		/* Command with address */
		reg |= 1 << CQSPI_REG_CMDCTRL_ADDR_EN_LSB;
		/* Number of bytes to write. */
		reg |= ((cmdlen - 2) & CQSPI_REG_CMDCTRL_ADD_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_ADD_BYTES_LSB;

		writel(addr_value, &qslave->regbase->flashcmdaddr);
	}

	if (txlen) {
		/* flashcmdwrdatalo and flashcmdwrdatahigh follow each others */
		const uint32_t *d = &qslave->regbase->flashcmdwrdatalo;
		int i;
		union {
			uint8_t b[8];
			uint32_t l[2];
		} u = { .l = { 0, 0} };
		const uint32_t *s = u.l;

		/* writing data = yes */
		reg |= 1 << CQSPI_REG_CMDCTRL_WR_EN_LSB;
		reg |= ((txlen - 1) & CQSPI_REG_CMDCTRL_WR_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_WR_BYTES_LSB;

		/* We need to copy the data, as it's unaligned */
		for (i = 0; i < txlen; i++)
			u.b[i] = txbuf[i];

		for (i = 0; i < txlen; i += 4, s++, d++)
			writel(*s, d);
	}

	/* Execute the command */
	return cadence_qspi_apb_exec_flash_cmd(qslave, reg);
}

void spi_set_speed(struct spi_slave *slave, uint hz)
{
	struct cadence_qspi_slave *qslave = to_cadence_qspi_slave(slave);
	hz = cadence_qspi_apb_config_baudrate_div(qslave, CONFIG_CQSPI_REF_CLK, hz);

	/* Reconfigure delay timing if speed is changed. */
	cadence_qspi_apb_delay(qslave,
		CONFIG_CQSPI_REF_CLK, hz,
		CONFIG_CQSPI_TSHSL_NS, CONFIG_CQSPI_TSD2D_NS,
		CONFIG_CQSPI_TCHSH_NS, CONFIG_CQSPI_TSLCH_NS);
}

/* calibration sequence to determine the read data capture delay register */
int spi_calibration(struct spi_slave *slave)
{
	struct cadence_qspi_slave *qslave = to_cadence_qspi_slave(slave);
	u8 opcode_rdid = 0x9F;
	unsigned int idcode = 0, temp = 0;
	int err = 0, i, range_lo = -1, range_hi = -1;

	/* start with slowest clock (ie Maximum clock divider) */
	spi_set_speed(slave, 1000000);

	/* configure the read data capture delay register to 0 */
	cadence_qspi_apb_readdata_capture(qslave, 0);

	/* Enable QSPI */
	cadence_qspi_apb_controller_enable(qslave);

	/* read the ID which will be our golden value */
	err = cadence_qspi_apb_command_read(qslave,
			1, &opcode_rdid,
			3, (u8 *)&idcode);
	if (err) {
		puts("SF: Calibration failed (read)\n");
		return err;
	}

	/* use back the intended clock and find low range */
	spi_set_speed(slave, qslave->max_hz);
	for (i = 0; i < CQSPI_READ_CAPTURE_MAX_DELAY; i++) {
		/* Disable QSPI */
		cadence_qspi_apb_controller_disable(qslave);

		/* reconfigure the read data capture delay register */
		cadence_qspi_apb_readdata_capture(qslave, i);

		/* Enable back QSPI */
		cadence_qspi_apb_controller_enable(qslave);

		/* issue a RDID to get the ID value */
		err = cadence_qspi_apb_command_read(qslave,
				1, &opcode_rdid,
				3, (u8 *)&temp);
		if (err) {
			puts("SF: Calibration failed (read)\n");
			return err;
		}

		/* search for range lo */
		if (range_lo == -1 && temp == idcode) {
			range_lo = i;
			continue;
		}

		/* search for range hi */
		if (range_lo != -1 && temp != idcode) {
			range_hi = i - 1;
			break;
		}
		range_hi = i;
	}

	if (range_lo == -1) {
		puts("SF: Calibration failed (low range)\n");
		return err;
	}

	/* Disable QSPI for subsequent initialization */
	cadence_qspi_apb_controller_disable(qslave);

	/* configure the final value for read data capture delay register */
	cadence_qspi_apb_readdata_capture(qslave, (range_hi + range_lo) / 2);
	debug("SF: Read data capture delay calibrated to %i (%i - %i)\n",
	      (range_hi + range_lo) / 2, range_lo, range_hi);

	/* just to ensure we do once only when speed or chip select change */
	qslave->qspi_calibrated_hz = qslave->max_hz;
	qslave->qspi_calibrated_cs = slave->cs;
	return 0;
}

static void cadence_qspi_apb_controller_init(struct cadence_qspi_slave *qslave)
{
	unsigned reg;

	cadence_qspi_apb_controller_disable(qslave);

	/* Configure the remap address register, no remap */
	writel(0, &qslave->regbase->remapaddr);

	/* Disable all interrupts */
	writel(0, &qslave->regbase->irqmask);

	/* Ensure these registers haven't been clobbered by the ROM */
	writel(0x1002, &qslave->regbase->devsz);
	writel(0x2, &qslave->regbase->devwr);
	writel(0x3, &qslave->regbase->devrd);

	/* enable direct mode */
	reg = readl(&qslave->regbase->cfg);
	reg |= CQSPI_REG_CONFIG_ENABLE_MASK;
	reg |= CQSPI_REG_CONFIG_DIRECT_MASK;
	writel(reg, &qslave->regbase->cfg);
	/* Enable AHB write protection, to prevent random write access
	 * that could nuke the flash memory. Instead you have to explicitly
	 * use 'sf write' to change the flash.
	 * We set the protection range to the maximum, so the whole
	 * address range should be protected, regardless of what size
	 * is actually used.
	 */
	writel(0, &qslave->regbase->lowwrprot);
	writel(~0, &qslave->regbase->uppwrprot);
	writel(CQSPI_REG_WRPROT_ENABLE_MASK, &qslave->regbase->wrprot);

	cadence_qspi_apb_controller_enable(qslave);
}

int spi_cs_is_valid(unsigned int bus, unsigned int cs)
{
	unsigned int max_cs;
	unsigned int max_bus;

#if (CONFIG_CQSPI_DECODER == 1)
	max_cs = CQSPI_NO_DECODER_MAX_CS;
#else
	max_cs = CQSPI_DECODER_MAX_CS;
#endif

	max_bus = sizeof(qspi_bases) / sizeof(qspi_bases[0]);

	if (((cs >= 0) && (cs < max_cs)) &&
	    ((bus >= 0) && (bus < max_bus))) {
		return 1;
	}
	printf("QSPI: Invalid bus or cs. Bus %d cs %d\n", bus, cs);
	return 0;
}

void spi_cs_activate(struct spi_slave *slave)
{
}

void spi_cs_deactivate(struct spi_slave *slave)
{
}

void spi_init(void)
{
	/* nothing to do */
}

/*
 * This is an addition to the general spi slave interface, this function
 * is called if a flash has been detected, to allow the slave to configure
 * itself.
 * In our case here, we need to know the number of address lines and stuff
 */
void spi_slave_register_flash(struct spi_slave *slave, struct spi_flash *flash)
{
	struct cadence_qspi_slave *qslave = to_cadence_qspi_slave(slave);
	int dummy_bytes, dummy_clk;
	uint32_t rd_reg, reg;

	if (slave->memory_map == NULL)
		return;

	qslave->flash = flash;
	debug("%s dummy %d - page %d sector %d erase %d(ffs %d)\n", __func__,
	      flash->dummy_byte, flash->page_size, flash->sector_size,
	      flash->erase_size, ffs(flash->erase_size));
	debug("  rd command %2x wr %02x\n", flash->read_cmd, flash->write_cmd);
	/*
	 * Calculate number of dummy cycles
	 */
	dummy_bytes = flash->dummy_byte;
	if (dummy_bytes > CQSPI_DUMMY_BYTES_MAX)
		dummy_bytes = CQSPI_DUMMY_BYTES_MAX;
	/* Convert to clock cycles. */
	dummy_clk = dummy_bytes * CQSPI_DUMMY_CLKS_PER_BYTE;

	cadence_qspi_apb_controller_disable(qslave);

	/* Configure the device size and address bytes */
	reg = readl(&qslave->regbase->devsz);
	/* Clear the previous value */
	reg &= ~(CQSPI_REG_SIZE_PAGE_MASK << CQSPI_REG_SIZE_PAGE_LSB);
	reg &= ~(CQSPI_REG_SIZE_BLOCK_MASK << CQSPI_REG_SIZE_BLOCK_LSB);
	reg |= (flash->page_size << CQSPI_REG_SIZE_PAGE_LSB);
	reg |= ((ffs(flash->erase_size)-1) << CQSPI_REG_SIZE_BLOCK_LSB);
	writel(reg, &qslave->regbase->devsz);

	/* 3 or 4-byte addressing */
	reg = readl(&qslave->regbase->devsz);
	reg &= ~0xf;
	reg |= qslave->slave.addressing_bytes - 1;
	writel(reg, &qslave->regbase->devsz);

	rd_reg = (flash->read_cmd << CQSPI_REG_RD_INSTR_OPCODE_LSB);

	if (flash->read_cmd != CMD_READ_ARRAY_SLOW && flash->read_cmd != CMD_READ_ARRAY_FAST)
		rd_reg |= (1 << CQSPI_REG_RD_INSTR_MODE_EN_LSB);

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
		dummy_clk /= 2;
		break;
	case CMD_READ_QUAD_IO_FAST:		/* 0xeb */
	case CMD_READ_QUAD_IO_FAST_4B:		/* 0xec */
		rd_reg |= (CQSPI_INST_TYPE_QUAD << CQSPI_REG_RD_INSTR_TYPE_ADDR_LSB);
	case CMD_READ_QUAD_OUTPUT_FAST:		/* 0x6b */
	case CMD_READ_QUAD_OUTPUT_FAST_4B:	/* 0x6c */
		rd_reg |= (CQSPI_INST_TYPE_QUAD << CQSPI_REG_RD_INSTR_TYPE_DATA_LSB);
		dummy_clk /= 4;
		break;
	default:
		debug("%s unsupported read command %02x\n", __func__, flash->read_cmd);
	}
	rd_reg |= (dummy_clk & CQSPI_REG_RD_INSTR_DUMMY_MASK) << CQSPI_REG_RD_INSTR_DUMMY_LSB;
	writel(rd_reg, &qslave->regbase->devrd);
	writel(0xff, &qslave->regbase->modebit);

	cadence_qspi_apb_controller_enable(qslave);
}

struct spi_slave *spi_setup_slave(
		unsigned int bus, unsigned int cs,
		unsigned int max_hz, unsigned int mode)
{
	struct cadence_qspi_slave *qslave;

	debug("%s: bus %d cs %d max_hz %dMHz mode %d\n", __func__,
	      bus, cs, max_hz/1000000, mode);

	if (!spi_cs_is_valid(bus, cs))
		return NULL;

	qslave = spi_alloc_slave(struct cadence_qspi_slave, bus, cs);
	if (!qslave) {
		printf("QSPI: Can't allocate. Bus %d cs %d\n", bus, cs);
		return NULL;
	}

	qslave->slave.memory_map = (void *)qspi_ahb_bases[bus];
	qslave->slave.memory_map_write = (void *)qspi_ahb_bases[bus];
	qslave->slave.op_mode_rx = qspi_modes[bus];
	qslave->mode = mode;
	qslave->max_hz = max_hz;
	qslave->regbase = (void *)qspi_bases[bus];

	if (!qslave->initialised)
		cadence_qspi_apb_controller_init(qslave);
	qslave->initialised = 1;

	return &qslave->slave;
}

void spi_free_slave(struct spi_slave *slave)
{
	struct cadence_qspi_slave *qslave = to_cadence_qspi_slave(slave);

	free(qslave);
}

int spi_claim_bus(struct spi_slave *slave)
{
	struct cadence_qspi_slave *qslave = to_cadence_qspi_slave(slave);
	unsigned int clk_pol = (qslave->mode & SPI_CPOL) ? 1 : 0;
	unsigned int clk_pha = (qslave->mode & SPI_CPHA) ? 1 : 0;
	int err = 0;

	debug("%s: bus:%i cs:%i\n", __func__, slave->bus, slave->cs);

	/* Disable QSPI */
	cadence_qspi_apb_controller_disable(qslave);

	/* Set Chip select */
	cadence_qspi_apb_chipselect(qslave, slave->cs, CONFIG_CQSPI_DECODER);

	/* Set SPI mode */
	cadence_qspi_apb_set_clk_mode(qslave, clk_pol, clk_pha);

	/* Set clock speed */
	spi_set_speed(slave, qslave->max_hz);

	/* calibration required for different SCLK speed or chip select */
	if ((qslave->qspi_calibrated_hz != qslave->max_hz) ||
	    (qslave->qspi_calibrated_cs != slave->cs)) {
		err = spi_calibration(slave);
		if (err)
			return err;
	}

	/* Enable QSPI */
	cadence_qspi_apb_controller_enable(qslave);

	return 0;
}

void spi_release_bus(struct spi_slave *slave)
{
}

int spi_xfer(
		struct spi_slave *slave, unsigned int bitlen,
		const void *data_out,
		void *data_in, unsigned long flags)
{
	struct cadence_qspi_slave *qslave = to_cadence_qspi_slave(slave);
	size_t data_bytes = bitlen / 8;
	int err = 0;

	if (flags & SPI_XFER_BEGIN) {
		/* copy command to local buffer */
		qslave->cmd_len = data_bytes;
		memcpy(qslave->cmd_buf, data_out, qslave->cmd_len);
		data_bytes = 0;
	}

	if ((flags & SPI_XFER_END) || (flags == 0)) {
		if (data_in && data_bytes) {
			err = cadence_qspi_apb_command_read(qslave,
				qslave->cmd_len, qslave->cmd_buf,
				data_bytes, data_in);
		} else if (data_out) {
			err = cadence_qspi_apb_command_write(qslave,
				qslave->cmd_len, qslave->cmd_buf,
				data_bytes, data_out);
		}
	}
	if (flags & SPI_XFER_MMAP_WRITE)
		writel(0, &qslave->regbase->wrprot);

	if (flags & (SPI_XFER_MMAP_END|SPI_XFER_END))
		writel(CQSPI_REG_WRPROT_ENABLE_MASK,
		       &qslave->regbase->wrprot);

	return err;
}

/*
 * Cadence DDR Controller
 *
 * Copyright (C) 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 */

/*
 * The Cadence DDR Controller has a huge number of registers that principally
 * cover two aspects, DDR specific timing information and AXI bus interfacing.
 * Cadence's TCL script generates all of the register values for specific
 * DDR devices operating at a specific frequency. The TCL script uses Denali
 * SOMA files as inputs. The tool also generates the AXI bus register values as
 * well, however this driver assumes that users will want to modifiy these to
 * meet a specific application's needs.
 * Therefore, this driver is passed two arrays containing register values for
 * the DDR device specific information, and explicity sets the AXI registers.
 *
 * AXI bus interfacing:
 *  The controller has four AXI slaves connections, and each of these can be
 * programmed to accept requests from specific AXI masters (using their IDs).
 * The regions of DDR that can be accessed by each AXI slave can be set such
 * as to isolate DDR used by one AXI master from another. Further, the maximum
 * bandwidth allocated to each AXI slave can be set.
 */

#include <common.h>
#include <linux/sizes.h>
#include <asm/io.h>
#include "cadence_ddr_ctrl.h"

#define DDR_NR_AXI_PORTS		4
#define DDR_NR_ENTRIES			16

#define DDR_START_REG			(0)		/* DENALI_CTL_00 */
#define DDR_ECC_ENABLE_REG		(36 * 4 + 2)	/* DENALI_CTL_36 */
#define DDR_ECC_DISABLE_W_UC_ERR_REG	(37 * 4 + 2)	/* DENALI_CTL_37 */
#define DDR_HALF_DATAPATH_REG		(54 * 4)	/* DENALI_CTL_54 */
#define DDR_INTERRUPT_STATUS		(56 * 4)	/* DENALI_CTL_56 */
#define DDR_INTERRUPT_MASK		(58 * 4)	/* DENALI_CTL_58 */
#define DDR_RW_PRIORITY_REGS		(87 * 4 + 2)	/* DENALI_CTL_87 */
#define DDR_AXI_ALIGN_STRB_DISABLE_REG	(90 * 4 + 2)	/* DENALI_CTL_90 */
#define DDR_AXI_PORT_PROT_ENABLE_REG	(90 * 4 + 3)	/* DENALI_CTL_90 */
#define DDR_ADDR_RANGE_REGS		(91 * 4)	/* DENALI_CTL_91 */
#define DDR_RANGE_PROT_REGS		(218 * 4 + 2)	/* DENALI_CTL_218 */
#define  RANGE_PROT_BITS_PRIV_SECURE	0
#define  RANGE_PROT_BITS_SECURE		1
#define  RANGE_PROT_BITS_PRIV		2
#define  RANGE_PROT_BITS_FULL		3
#define DDR_ARB_CMD_Q_THRESHOLD_REG	(346 * 4 + 2)	/* DENALI_CTL_346 */
#define DDR_AXI_PORT_BANDWIDTH_REG	(346 * 4 + 3)	/* DENALI_CTL_346 */

void cdns_ddr_set_port_rw_priority(void *base, int port,
			  u8 read_pri, u8 write_pri, u8 fifo_type)
{
	u8 *reg8 = base + DDR_RW_PRIORITY_REGS;

	reg8 += (port * 3);

	writeb(read_pri, reg8++);
	writeb(write_pri, reg8++);
	writeb(fifo_type, reg8++);
}

/* The DDR Controller has 16 entries. Each entry can specify an allowed address
 * range (with 16KB resolution) for one of the 4 AXI slave ports.
 */
void cdns_ddr_enable_port_addr_range_x(void *base, int port, int entry,
			      u32 addr_start, u32 size)
{
	u32 addr_end;
	u32 *reg32 = (u32 *)(base + DDR_ADDR_RANGE_REGS);

	reg32 += (port * DDR_NR_ENTRIES * 2);
	reg32 += (entry * 2);

	/* These registers represent 16KB address blocks */
	addr_start /= SZ_16K;
	addr_end = addr_start + (size / SZ_16K) - 1;

	writel(addr_start, reg32++);
	writel(addr_end, reg32++);
}

void cdns_ddr_enable_port_prot_x(void *base, int port, int entry,
	u8 range_protection_bits,
	u16 range_RID_check_bits,
	u16 range_WID_check_bits,
	u8 range_RID_check_bits_ID_lookup,
	u8 range_WID_check_bits_ID_lookup)
{
	/*
	 * Technically, the offset here points to the byte before the start of
	 * the range protection registers. However, all entries consist of 8
	 * bytes, except the first one (which is missing a padding byte) so we
	 * work around that subtlely.
	 */
	u8 *reg8 = base + DDR_RANGE_PROT_REGS;

	reg8 += (port * DDR_NR_ENTRIES * 8);
	reg8 += (entry * 8);

	if (port == 0 && entry == 0)
		writeb(range_protection_bits, reg8 + 1);
	else
		writeb(range_protection_bits, reg8);

	writew(range_RID_check_bits, reg8 + 2);
	writew(range_WID_check_bits, reg8 + 4);
	writeb(range_RID_check_bits_ID_lookup, reg8 + 6);
	writeb(range_WID_check_bits_ID_lookup, reg8 + 7);
}

void cdns_ddr_set_port_bandwidth(void *base, int port,
		        u8 max_percent, u8 overflow_ok)
{
	u8 *reg8 = base + DDR_AXI_PORT_BANDWIDTH_REG;

	reg8 += (port * 3);

	writeb(max_percent, reg8++);	/* Maximum bandwidth percentage */
	writeb(overflow_ok, reg8++);	/* Bandwidth overflow allowed */
}

void cdns_ddr_ctrl_init(u32 *ddr_ctrl_base, int async,
			const u32 *reg0, const u32 *reg350,
			u32 ddr_start_addr, u32 ddr_size)
{
	int i, axi;
	u8 *base8 = (u8 *)ddr_ctrl_base;

	writel(*reg0, ddr_ctrl_base + 0);
	/* 1 to 6 are read only */
	for (i = 7; i <= 25; i++)
		writel(*(reg0 + i), ddr_ctrl_base + i);
	/* 26 to 29 are read only */
	for (i = 30; i <= 87; i++)
		writel(*(reg0 + i), ddr_ctrl_base + i);

	/* Enable/disable ECC */
#if defined(CONFIG_CADENCE_DDR_CTRL_ENABLE_ECC)
	writeb(1, base8 + DDR_ECC_ENABLE_REG);
#else
	writeb(0, base8 + DDR_ECC_ENABLE_REG);
#endif

	/* ECC: Disable corruption for read/modify/write operations */
	writeb(1, base8 + DDR_ECC_DISABLE_W_UC_ERR_REG);

	/* ECC: AXI aligned strobe disable => enable */
	writeb(0, base8 + DDR_AXI_ALIGN_STRB_DISABLE_REG);

	/* Set 8/16-bit data width using reduc bit (enable half datapath)*/
#if defined(CONFIG_CADENCE_DDR_CTRL_8BIT_WIDTH)
	writeb(1, base8 + DDR_HALF_DATAPATH_REG);
#else
	writeb(0, base8 + DDR_HALF_DATAPATH_REG);
#endif

	/* Threshold for command queue */
	writeb(0x03, base8 + DDR_ARB_CMD_Q_THRESHOLD_REG);

	/* AXI port protection enable => enable */
	writeb(0x01, base8 + DDR_AXI_PORT_PROT_ENABLE_REG);

	/* We enable all AXI slave ports with the same settings */
	for (axi = 0; axi < DDR_NR_AXI_PORTS; axi++) {
		/* R/W priorities */
		if (async)
			cdns_ddr_set_port_rw_priority(ddr_ctrl_base, axi, 2, 2, 0);
		else
			cdns_ddr_set_port_rw_priority(ddr_ctrl_base, axi, 2, 2, 3);

		/* DDR address range */
		cdns_ddr_enable_port_addr_range_x(ddr_ctrl_base, axi, 0,
					 ddr_start_addr, ddr_size);

		/* Access rights, see manual for details */
		cdns_ddr_enable_port_prot_x(ddr_ctrl_base, axi, 0,
				   RANGE_PROT_BITS_FULL,
				   0xffff, 0xffff, 0x0f, 0x0f);

		/* AXI bandwidth */
		cdns_ddr_set_port_bandwidth(ddr_ctrl_base, axi, 50, 1);
	}

	for (i = 350; i <= 374; i++)
		writel(*(reg350 - 350 + i), ddr_ctrl_base + i);

	/*
	 * Disable all interrupts, we are not handling them.
	 * For detail of the interrupt mask, ack and status bits, see the
	 * manual's description of the 'int_status' parameter.
	 */
	writel(0, base8 + DDR_INTERRUPT_MASK);

	/* Start */
	writeb(1, base8 + DDR_START_REG);

	/* Wait for controller to be ready (interrupt status) */
	while (!(readl(base8 + DDR_INTERRUPT_STATUS) & 0x100))
		;
}

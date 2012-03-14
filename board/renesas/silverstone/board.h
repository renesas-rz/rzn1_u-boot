/*
 * board/renesas/silverstone/board.h
 *
 * Copyright (C) 2011 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* for setting pin */
struct pin_db {
	u32	addr;	/* register address */
	u32	mask;	/* mask value */
	u32	val;	/* setting value */
};

#define	FPGA_BASE	0x18200000
#define FPGA_BUSSW_EN	(FPGA_BASE + 0x500)
#define		LBSC_EN		(1<<0)
#define		IRQ23_EN	(1<<7)
#define		TS_EN		(1<<8)
#define FPGA_SD1_FUNC	(FPGA_BASE + 0x510)
#define		SD1_SEL0	(1<<0)
#define		SD1_SEL12	(3<<1)
#define		SD1_SEL34	(3<<3)
#define		SD1_SEL56	(3<<5)
#define FPGA_IRQ_FUNC	(FPGA_BASE + 0x514)
#define		IRQ23_SEL	(3<<1)
#define FPGA_SCI_FUNC	(FPGA_BASE + 0x52c)
#define		TS_SEL		(1<<0)
#define FPGA_DSW_STS	(FPGA_BASE + 0x600)
#define		DSW1001_1	(1<<0)
#define		DSW1001_2	(1<<1)
#define		DSW1001_3	(1<<2)
#define		DSW1001_4	(1<<3)
#define	FPGA_RESET_EN	(FPGA_BASE + 0x800)
#define		HRST_EN		(1<<0)
#define		PERI_EN		(1<<2)
#define	FPGA_HRST_TRG	(FPGA_BASE + 0x804)
#define		HRST_TRG	(1<<0)
#define	FPGA_PERI_TRG	(FPGA_BASE + 0x80c)
#define		FBOARD_TRG	(1<<1)
#define		LAN_TRG		(1<<2)
#define	FPGA_VER	(FPGA_BASE + 0xf00)
#define	FPGA_BOARD_ID	(FPGA_BASE + 0xf10)

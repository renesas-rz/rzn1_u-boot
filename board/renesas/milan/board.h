/*
 * board/renesas/milan/board.h
 *
 * Copyright(c) 2011 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* for setting pin */
struct pin_db {
	u32	addr;	/* register address */
	u32	mask;	/* mask value */
	u32	val;	/* setting value */
};

// SGL: Milan: Need to remove the use of these in board reset and memory config as we have no FPGA.
#if 0
#define	FPGA_BASE	0x18200000
#define	FPGA_SRSTR	(FPGA_BASE + 0x00)
#define		KEY_RESET	0xa5a5
#define	FPGA_FPVERR	(FPGA_BASE + 0x50)
#define	FPGA_BUSWMR1	(FPGA_BASE + 0x74)
#define		MD1_IN		(1 << 9)
#endif

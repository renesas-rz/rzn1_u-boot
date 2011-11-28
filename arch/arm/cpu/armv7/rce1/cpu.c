/*
 * arch/arm/cpu/armv7/rce1/cpu.c
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

#include <common.h>
#include <asm/io.h>
#include <asm/arch/cpu.h>

void reset_cpu(ulong addr)
{
	board_reset();
}

int print_cpuinfo(void)
{
	unsigned int	md = readl(MODEMR);
	unsigned char	*cddr;

	if (md & MD12) {
		if (md & MD11)
			cddr = "400";
		else
			cddr = "533";
	} else {
		if (md & MD11)
			cddr = "800";
		else
			cddr = "1067";
	}

	printf("CPU  : R-CarE1 (md:0x%x)\n", md);
	printf("       [CPU:%sMHz,SHwy:%sMHz,DDR:%sMHz,EXCLK:%sMHz]\n",
			md & MD11 ? "400" : "533",
			md & MD11 ? "200" : "266",
			cddr,
			md & MD11 ? "50" : "66.66");
	timer_init();
	return 0;
}

/*
 * board/renesas/milan/milan.c
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
#include <netdev.h>
#include <asm/arch/cpu.h>
#include "board.h"

DECLARE_GLOBAL_DATA_PTR;

struct pin_db	pin_guard[] = {
	{ GPSR0, 0xffffffe0, 0xffffffe0 },	/* BS#, A0-A25 */
	{ GPSR1, 0x003f101f, 0x003f101f },	/* RD/WR#, WE[01]#, */
		/* EX_CS[01]#, SCIF_CLK, TX0, RX0, SCK0, CTS0#, RTS0# */
};

struct pin_db	pin_tbl[] = {
};

void pin_init(void)
{
	struct pin_db	*db;

	for (db = pin_guard; db < &pin_guard[sizeof(pin_guard) /
			sizeof(struct pin_db)]; db++) {
		SetGuardREG(db);
	}
	for (db = pin_tbl; db < &pin_tbl[sizeof(pin_tbl) /
			sizeof(struct pin_db)]; db++) {
		SetREG(db);
	}
}

void exbus_init(void)
{
	/* Configure the LBSC */
	EXB_W(CS0BSTCTL, 0x00001000);	/* Area 0 burst lenght access - 8 cycles */
	EXB_W(CS0BTPH, 0x00000021);		/* 0 cycle hold, 2 cycles wait (1st), 1 cycles wait (2nd) */
	EXB_W(CS0CTRL, 0x00008020);		/* Little Endian, 64Mb Capacity, 16bit bus, burst rom mode */
	EXB_W(CS1CTRL, 0x00000020);		/* 16bit, SRAM */
	EXB_W(ECS0CTRL, 0x00000120);	/* 1Mb capacity, 16bit bus, same cyc as CS#, SRAM */
}

static void uart_init(void)
{
	writew(CONFIG_SYS_CLK_FREQ / CONFIG_BAUDRATE / 16,
			SCIF_BASE + SCIF_DL);
	writew(CKS_EXTERNAL, SCIF_BASE + SCIF_CKS);
	wait_usec((1000000 + (CONFIG_BAUDRATE - 1))
			/ CONFIG_BAUDRATE);	/* one bit interval */
}

int board_early_init_f(void)
{
	uart_init();

	return 0;
}

int board_init(void)
{
	gd->bd->bi_arch_number = MACH_TYPE_RCAR_MILAN;
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

	icache_enable();
	invalidate_dcache();

	/* Set the ARM NMI so that a NMI interrupt request is detected on signal level high */ 
	writeb((readb(NMICTL) | NMI_BIT), 
			NMICTL);

	return 0;
}

int board_eth_init(bd_t *bis)
{
	int	rc = 0;

#ifdef CONFIG_SMC911X
	rc = smc911x_initialize(0, CONFIG_SMC911X_BASE);
#endif
	return rc;
}

int dram_init(void)
{
	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->bd->bi_dram[0].size = CONFIG_SYS_SDRAM_SIZE;
	gd->ram_size = CONFIG_SYS_SDRAM_SIZE;
	return 0;
}

int checkboard(void)
{
	unsigned short	cpld0;
	unsigned short	cpld1;
	unsigned short	cpld2;
	unsigned short	cpld3;
	unsigned short	cpld4;
	unsigned short	cpld5;
	unsigned short	cpld6;
	unsigned short	cpld7;

	cpld0 = readb(CPLD0_BASE);
	cpld1 = readb(CPLD1_BASE);
	cpld2 = readb(CPLD2_BASE);
	cpld3 = readb(CPLD3_BASE);
	cpld4 = readb(CPLD4_BASE);
	cpld5 = readb(CPLD5_BASE);
	cpld6 = readb(CPLD6_BASE);
	cpld7 = readb(CPLD7_BASE);

	/* Print Milan board version bits and the board config in the CPLD registers */
	printf("Board: Milan (version: 0x%x)\n", ((cpld0 >> 4) & 0x3));
	printf("         [CPLD0: 0x%02x, CPLD1: 0x%02x, CPLD2: 0x%02x, CPLD3: 0x%02x]\n", 
			cpld0, cpld1, cpld2, cpld3);
	printf("         [CPLD4: 0x%02x, CPLD5: 0x%02x, CPLD6: 0x%02x, CPLD7: 0x%02x]\n",
			cpld4, cpld5, cpld6, cpld7);

	return 0;
}

void
wait_usec(int usec)
{
	unsigned long	tick;

	tick = usec * TCLOCK / 4 - 1;
	writeb(readb(TBASE + TSTR0) & ~(1 << 0),
			TBASE + TSTR0);
	writew(0, TBASE + TCR0);
	writel(tick, TBASE + TCOR0);
	writel(tick, TBASE + TCNT0);
	writeb(readb(TBASE + TSTR0) | (1 << 0),
			TBASE + TSTR0);
	while ((readw(TBASE + TCR0) & (1 << 8)) == 0)
		;
	writeb(readb(TBASE + TSTR0) & ~(1 << 0),
			TBASE + TSTR0);
}

void memc_init(void)
{
	u32	val;

	/* Referred to R-CarM1_02_04_DBSC_0070_j.pdf */

	/* (1) */
	wait_usec(200);
	/* (2) */
	MEMC_W(DBCMD, 0x20000000);	/* opc:RstL, arg:0 */
	/* (3) */
	MEMC_W(DBCMD, 0x1000D055);	/* opc:PDEn, arg:100us */

	/* dbsc3 setting-1 */
	/* (4) */
	MEMC_W(DBKIND, 0x7);		/* ddr3-sdram */
	/* (5) */
	MEMC_W(DBCONF0, 0x0f030a02);	/* RowAd:15, Bank:8, CoulumAd:10 */
	MEMC_W(DBTR0, 0x8);		/* CL:8 */
	MEMC_W(DBTR1, 0x6);		/* CWL:6 */
	MEMC_W(DBTR2, 0);		/* AL:0 */
	MEMC_W(DBTR3, 0x8);		/* TRCD:8 */
	MEMC_W(DBTR4, 0x90008);		/* TRPA:9, TRP:8 */
	MEMC_W(DBTR5, 0x1c);		/* TRC:28 */
	MEMC_W(DBTR6, 0x14);		/* TRAS:20 */
	MEMC_W(DBTR7, 0x6);		/* TRRD:6 */
	MEMC_W(DBTR8, 0x1b);		/* TFAW:27 */
	MEMC_W(DBTR9, 0x6);		/* TRDPR:6 */
	MEMC_W(DBTR10, 0x8);		/* TWR:8 */
	MEMC_W(DBTR11, 0x8);		/* TRDWR:8 */
	MEMC_W(DBTR12, 0xe);		/* TWRRD:14 */
	MEMC_W(DBTR13, 0x56);		/* TRFC:86 */
	MEMC_W(DBTR14, 0xd0004);	/* DLL:13, TCKEH:4 */
	MEMC_W(DBTR15, 0x4);		/* TCKEL:4 */
	MEMC_W(DBTR16, 0x10181001);	/* DQL:24 */
	MEMC_W(DBTR17, 0xc000d);	/* TMOD:12, TRDMR:13 */
	MEMC_W(DBTR18, 0);		/* ODT */
	MEMC_W(DBTR19, 0x40);		/* TZQCS */
	MEMC_W(DBRNK0, 0);		/* initial value */
	/* (6) */
	MEMC_W(DBADJ0, 1);		/* CAMODE:1 */
	/* (7) */
	MEMC_W(DBADJ2, 0x0008);		/* ACAP0:8 */
	/* (8) */
	MEMC_W(DBADJ2, 0x2008);		/* ACAPC0:20 */

	/* phy setting */
	/* (9) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x100);	/* PTRRST:1 */
	/* (10) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val & ~0x100);	/* PTRRST:0 */
	/* (11) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x800);	/* CALMODE:1 */
	/* (12) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x400);	/* CALEN:1 */
	/* (13) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x01000000);	/* DLLRESET:1 */
	/* (14) */
	wait_usec(50);
	/* (15) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x2000);	/* IO_ENABLE:1 */
	/* (16) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x80000000);	/* PLL2_RESET:1 */
	/* (17) */
	wait_usec(100);
	/* (18) */
	val = MEMC_R(DBPDCNT3);
	MEMC_W(DBPDCNT3, val | 0x30000000);	/* STBY[1]:1, STBY[0]:1 */
	/* (19) */
	val = MEMC_R(DBPDCNT0);
	MEMC_W(DBPDCNT0, val | 0x80000000);	/* BW32:1 */
	/* (20) */
	MEMC_W(DBPDCNT0, 0x80000000);	/* OFFSET:0 */
	/* (21) */
	MEMC_W(DBPDCNT0, 0x80010000);	/* ODT:60ohm */
	MEMC_W(DBPDCNT1, 0);		/* Drive:40ohm */
	MEMC_W(DBPDCNT2, 0);		/* initial value */

	/* ddr3-sdram setting */
	/* (22) */
	MEMC_W(DBCMD, 0x0000d055);	/* Wait, 100us */
	/* (23) */
	MEMC_W(DBCMD, 0x2100d055);	/* RstH, 100us */
	/* (24) */
	MEMC_W(DBCMD, 0x0000d055);	/* Wait, 100us */
	MEMC_W(DBCMD, 0x0000d055);	/* Wait, 100us */
	MEMC_W(DBCMD, 0x0000d055);	/* Wait, 100us */
	MEMC_W(DBCMD, 0x0000d055);	/* Wait, 100us */
	/* (25) */
	MEMC_W(DBCMD, 0x1100005b);	/* PDXt, 170ns */
	/* (26) */
	MEMC_W(DBCMD, 0x2a000008);	/* MR2, CWL:6 */
	/* (27) */
	MEMC_W(DBCMD, 0x2b000000);	/* MR3 */
	/* (28) */
	MEMC_W(DBCMD, 0x29000000);	/* MR1, AL:0, DLL:En, ODT=Dis,
						ODS:40ohm */
	/* (29) */
	MEMC_W(DBCMD, 0x28000940);	/* MR0, Na, DLL_RES, BL:8, BT:Seque,
						CL:8, WR:8 */
	/* (30) */
	MEMC_W(DBCMD, 0x03000200);	/* ZQCL, 512 */

	/* dbsc3 setting-2 */
	/* (31) */
	MEMC_W(DBCALCNF, 0x01005334);	/* DBCALCNF */
	MEMC_W(DBCALTR, 0x06a406a4);	/* DBCALTR */
	/* (32) */
	MEMC_W(DBRFCNF0, 0xc8);		/* 200cycle */

	if (readl(MODEMR) & MD1) {	/* dip-switch MD_SW1-2 (off = 1) */
		MEMC_W(DBRFCNF1, 0xed8);	/* 533MHz */
	} else {
		MEMC_W(DBRFCNF1, 0xaf0);	/* 400MHz */
	} 

	MEMC_W(DBRFCNF2, 0);		/* REFINT:1/1 */
	/* (33) */
	MEMC_W(DBRFEN, 1);		/* DBRFEN. ARFEN */

	/* workaround for m1a 2nd cut */
	MEMC_W(0x0280, 0x0000A55A);
	MEMC_W(0x0290, 0x00000000);
	MEMC_W(0x02A0, 0xA5390000);
	MEMC_W(0x0290, 0x00000025);
	MEMC_W(0x02A0, 0x00016100);
	MEMC_W(0x0290, 0x00000000);
	MEMC_W(0x02A0, 0x00000000);
	MEMC_W(0x0280, 0x00000000);

	/* (34) */
	MEMC_W(DBACEN, 1);		/* DBACEN. ACCEN */
	/* (35) */
	MEMC_R(DBWAIT);			/* wait for done */
}

void board_reset(void)
{
	/* Tell the CPLD to reset the board */
	writeb(CPLD0_SOFT_RESET, CPLD0_BASE);
 
	while (1)
		;
}

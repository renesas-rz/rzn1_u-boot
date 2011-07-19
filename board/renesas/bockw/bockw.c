/*
 * board/renesas/bockw/bockw.c
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
	EXB_W(CS0BSTCTL, 0);		/* none burst */
	EXB_W(CS0BTPH, 0x000000f7);	/* a0h:0, a0w:15, a0b:7 */
	EXB_W(CS0CTRL, 0x00008021);	/* little,64MB,16bit,BROM */
	EXB_W(CS1CTRL, 0x00000020);	/* 16bit, SRAM */
	EXB_W(ECS0CTRL, 0x00000920);	/* 9MB, 16bit, sram */
	EXB_W(ECS1CTRL, 0x00000122);	/* 1MB, 16bit, ata */
	EXB_W(ECS2CTRL, 0x00003620);	/* 54MB, 16bit, sram */

	/* pulse control */
	/* (write) setup, hold, pulse, (read) setup, hold, pulse */
	EXB_W(CSWCR0, 0x02190338);	/* 2, 1, 9, 3, 3, 8 */
	EXB_W(CSWCR1, 0x02190338);	/* 2, 1, 9, 3, 3, 8 */
	EXB_W(ECSWCR0, 0x00280028);	/* 0, 2, 8, 0, 2, 8 */
	EXB_W(ECSWCR1, 0x077f077f);	/* 7, 7, 15, 7, 7, 15 */
	EXB_W(ECSWCR2, 0x077f077f);	/* 7, 7, 15, 7, 7, 15 */

	EXB_W(CSPWCR0, 0x00000000);	/* v:0, rv:0, winv:0, */
					/* exwt2:0, wxwt1:0 exwt0:0 */
	EXB_W(CSPWCR1, 0x00000000);	/* v:0, rv:0, winv:0, */
					/* exwt2:0, wxwt1:0 exwt0:0 */
	EXB_W(EXSPWCR0, 0x00000029);	/* v:1, rv:0, winv:1, */
					/* exwt2:0, wxwt1:0 exwt0:1 */
	EXB_W(EXSPWCR1, 0x00000029);	/* v:1, rv:0, winv:1, */
					/* exwt2:0, wxwt1:0 exwt0:1 */
	EXB_W(EXSPWCR2, 0x00000000);	/* v:0, rv:0, winv:0, */
					/* exwt2:0, wxwt1:0 exwt0:0 */
	EXB_W(EXWTSYNC, 0x00000000);	/* sync2:0, sync1:0, sync0:0 */

	EXB_W(CS1GDST, 0x00000000);	/* cs1gd:0, timer_set:0 */
	EXB_W(ECS0GDST, 0x00000000);	/* ecs0gd:0, timer_set:0 */
	EXB_W(ECS1GDST, 0x00000000);	/* ecs1gd:0, timer_set:0 */
	EXB_W(ECS2GDST, 0x00000000);	/* ecs2gd:0, timer_set:0 */

	EXB_W(ATACSCTRL, 0x00000004);
	EXB_W(EXDMASET0, 0x00000008);	/* DM0ECS1:1 */
	EXB_W(EXDMACR0, 0x00001404);	/* dbst:1, exql:1, exal:1 */
	EXB_W(EXDMACR1, 0x00000004);	/* exal:1 */
	EXB_W(EXDMACR2, 0x00000004);	/* exal:1 */
	EXB_W(BCINTMR, 0x00000000);	/* attem:0 */
	EXB_W(EXBATLV, 0x00000000);	/* ex-blv: 0 */
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
	gd->bd->bi_arch_number = MACH_TYPE_BOCKW;
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

	icache_enable();
	invalidate_dcache();

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
	unsigned short	val;

	val = readw(FPGA_FPVERR);
	printf("BOARD: BOCK-W (V%c%c.%c%c)\n",
			(val >> 12) + '0',
			((val >> 8) & 0xf) + '0',
			((val >> 4) & 0xf) + '0',
			(val & 0xf) + '0');
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
	MEMC_W(DBCONF0, 0x0e030a02);	/* RowAd:14, Bank:8, CoulumAd:10 */
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
	if (readw(FPGA_BUSWMR1) & MD1_IN) {	/* dipsw25-2 */
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
	writew(KEY_RESET, FPGA_SRSTR);
	while (1)
		;
}

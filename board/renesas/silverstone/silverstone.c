/*
 * board/renesas/silverstone/silverstone.c
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
	{ GPSR0   , 0xFFFFFFE0, 0xFFFFFFE0 },
	{ GPSR1   , 0x003F001F, 0x003F001F },
#if defined(CONFIG_CONS_SCIF5)
	{ GPSR1   , 0x0000C000, 0x00000000 },
	{ IPSR3   , 0x0000001F, 0x00000005 },
	{ GPSR1   , 0x00000000, 0x0000C000 },
#endif
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
	EXB_W(CS1CTRL  , 0x00000020);	/* 16bit, SRAM */
	EXB_W(ECS0CTRL , 0x00000920);	/* 9MB, 16bit, sram */
	EXB_W(ECS1CTRL , 0x00000122);	/* 1MB, 16bit, ata */
	EXB_W(ECS2CTRL , 0x00003620);	/* 54MB, 16bit, sram */

	/* pulse control */
	/* (write) setup, hold, pulse, (read) setup, hold, pulse */
	EXB_W(CSWCR0   , 0x01140127);
	EXB_W(CSWCR1   , 0x01170227);
	EXB_W(ECSWCR0  , 0x02240225);
	EXB_W(ECSWCR1  , 0x01170117);
	EXB_W(ECSWCR2  , 0x01170117);

	EXB_W(CSPWCR0  , 0x00000000);	/* v:0, rv:0, winv:0, */
					/* exwt2:0, wxwt1:0 exwt0:0 */
	EXB_W(CSPWCR1  , 0x00000000);	/* v:0, rv:0, winv:0, */
					/* exwt2:0, wxwt1:0 exwt0:0 */
	EXB_W(ECSPWCR0 , 0x00000000);
	EXB_W(ECSPWCR1 , 0x00000031);
	EXB_W(ECSPWCR2 , 0x00000000);	/* v:0, rv:0, winv:0, */
					/* exwt2:0, wxwt1:0 exwt0:0 */
	EXB_W(EXWTSYNC , 0x00000001);

	EXB_W(CS1GDST  , 0x00000000);	/* cs1gd:0, timer_set:0 */
	EXB_W(ECS0GDST , 0x00000000);	/* ecs0gd:0, timer_set:0 */
	EXB_W(ECS1GDST , 0x00000000);	/* ecs1gd:0, timer_set:0 */
	EXB_W(ECS2GDST , 0x00000000);	/* ecs2gd:0, timer_set:0 */

	EXB_W(ATACSCTRL, 0x00000004);
}

static void uart_init(void)
{
	writew(CONFIG_SYS_CLK_FREQ / CONFIG_BAUDRATE / 16,
			SCIF_BASE + SCIF_DL);
	writew(CKS_EXTERNAL, SCIF_BASE + SCIF_CKS);
	wait_usec((1000000 + (CONFIG_BAUDRATE - 1))
			/ CONFIG_BAUDRATE);	/* one bit interval */
}

static void fpga_init(void)
{
	/* Select ExBus A25 */
	writew(readw(FPGA_BUSSW_EN) | LBSC_EN, FPGA_BUSSW_EN);
	writew(readw(FPGA_SD1_FUNC) | SD1_SEL0, FPGA_SD1_FUNC);

	/* Select SCIF0 */
	writew(readw(FPGA_BUSSW_EN) | TS_EN, FPGA_BUSSW_EN);
	writew(readw(FPGA_SCI_FUNC) & ~TS_SEL, FPGA_SCI_FUNC);

#if defined(CONFIG_CONS_SCIF5)
	/* Select SCIF5_B */
	writew(readw(FPGA_BUSSW_EN) | IRQ23_EN, FPGA_BUSSW_EN);
	writew(readw(FPGA_IRQ_FUNC) & ~IRQ23_SEL, FPGA_IRQ_FUNC);
#endif

#if defined(CONFIG_SMC911X)
	/* Release LAN reset */
	writew(readw(FPGA_RESET_EN) | PERI_EN,  FPGA_RESET_EN);
	writew(readw(FPGA_PERI_TRG) & ~LAN_TRG, FPGA_PERI_TRG);
#endif
}

int board_early_init_f(void)
{
	fpga_init();
	uart_init();

	return 0;
}

int board_init(void)
{
	gd->bd->bi_arch_number = MACH_TYPE_SILVERSTONE;
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

	icache_enable();
	invalidate_dcache();

	return 0;
}

int board_eth_init(bd_t *bis)
{
	int	rc = 0;

#if defined(CONFIG_SMC911X)
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
	unsigned short	ver;
	unsigned short	id;

	ver = readw(FPGA_VER);
	id = readw(FPGA_BOARD_ID);
	printf("BOARD: Silverstone (FPGAVer:0x%04x,BoardID:0x%04x)\n", ver, id);

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
	/* (4) */
	PHY_W(PYFUNCCTRL , 0x00000101);	/* DVDDQ=1.5V : clear soft reset */
	MEMC_W(DBCMD     , 0x2100d066);	/* Opt=RstH arg=0xd066(100usec@533MHz)
						MRESETB=H */

	MEMC_W(DBCMD     , 0x0000d066);
	MEMC_W(DBCMD     , 0x0000d066);
	MEMC_W(DBCMD     , 0x0000d066);
	MEMC_W(DBCMD     , 0x0000d066);

	/* (5) */
	MEMC_W(DBKIND    , 0x00000007);
	/* (6) */
	MEMC_W(DBCONF0   , 0x0e030a01);
	MEMC_W(DBPHYTYPE , 0x00000001);	/* DBPHYTYPE test */
	MEMC_W(DBBL      , 0x00000000);
	MEMC_W(DBTR0     , 0x00000007);
	MEMC_W(DBTR1     , 0x00000006);
	MEMC_W(DBTR2     , 0x00000000);
	MEMC_W(DBTR3     , 0x00000007);
	MEMC_W(DBTR4     , 0x00080008);
	MEMC_W(DBTR5     , 0x0000001a);
	MEMC_W(DBTR6     , 0x00000014);
	MEMC_W(DBTR7     , 0x00000006);
	MEMC_W(DBTR8     , 0x00000018);
	MEMC_W(DBTR9     , 0x00000004);
	MEMC_W(DBTR10    , 0x00000008);
	MEMC_W(DBTR11    , 0x0000000C);
	MEMC_W(DBTR12    , 0x00000012);
	MEMC_W(DBTR13    , 0x000000c8);
	MEMC_W(DBTR14    , 0x000e0004);
	MEMC_W(DBTR15    , 0x00000004);
	MEMC_W(DBTR16    , 0x50006006);
	MEMC_W(DBTR17    , 0x000c0017);
	MEMC_W(DBTR18    , 0x00000400);
	MEMC_W(DBTR19    , 0x00000040);
	MEMC_W(DBRNK0    , 0x00120033);

	MEMC_W(DBBS0CNT0 , 0x00000001);
	MEMC_W(DBBS0CNT1 , 0x00000000);

	/* (27) */
	MEMC_W(DBRFCNF0  , 0x000000ff);
	MEMC_W(DBRFCNF1  , 0x000836b0);
	MEMC_W(DBRFCNF2  , 0x00000001);
	MEMC_W(DBCMD     , 0x0000d066);
	/* (10) */
	PHY_W(PYDLLCTRL  , 0x00000005);
	/* (11) */
	PHY_W(PYZQCALCTRL, 0x00000182);
	/* (12) */
	PHY_W(PYZQODTCTRL, 0xAAAB8051);
	/* (13) */
	PHY_W(PYRDCTRL   , 0xB545B544);
	/* (14) */
	PHY_W(PYRDTMG    , 0x000000b0);
	/* (15) */
	PHY_W(PYFIFOINIT , 0x00000101);
	/* (16) */
	PHY_W(PYOUTCTRL  , 0x020A0806);

	PHY_W(PYWLCTRL1  , 0x00000000);
	/* (17) */
	PHY_W(PYDLLCTRL  , 0x00000004);
	/* (18) */
	PHY_W(PYZQCALCTRL, 0x00000183);

	MEMC_W(DBCMD     , 0x0000d066);

	PHY_W(PYDQCALOFS1, 0x00004646);
	PHY_W(PYDQCALEXP , 0x800000AA);
	MEMC_W(DBDFICNT  , 0x00000000);
	/* (16)-2? */
	PHY_W(PYOUTCTRL  , 0x020A0807);

	MEMC_W(DBCMD     , 0x11000005);
	MEMC_W(DBCMD     , 0x0b040000);
	MEMC_W(DBCMD     , 0x00040100);
	MEMC_W(DBCMD     , 0x2a000008);
	MEMC_W(DBCMD     , 0x2b000000);
	MEMC_W(DBCMD     , 0x29000002);
	MEMC_W(DBCMD     , 0x28000830);
	MEMC_W(DBCMD     , 0x03000200);
	MEMC_W(DBCMD     , 0x2a010008);
	MEMC_W(DBCMD     , 0x2b010000);
	MEMC_W(DBCMD     , 0x29010002);
	MEMC_W(DBCMD     , 0x28010830);
	MEMC_W(DBCMD     , 0x03010200);

	/* (30) */
	MEMC_R(DBWAIT);

	MEMC_W(DBCALCNF  , 0x00000005);
	MEMC_W(DBCALTR   , 0x07d10f89);
	/* (28) */
	MEMC_W(DBRFEN    , 0x00000001);
	/* (29) */
	MEMC_W(DBACEN    , 0x00000001);
}

void board_reset(void)
{
	writew((readw(FPGA_RESET_EN) | HRST_EN),  FPGA_RESET_EN);
	writew(HRST_TRG, FPGA_HRST_TRG);
	while (1)
		;
}

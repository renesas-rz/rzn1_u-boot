/*
 * evatronix_nand.h - NAND Flash Driver for Evatronix NANDFLASH-CTRL
 * NAND Flash Controller IP.
 *
 * This implementation has been designed against Rev 1.15 of the
 * NANDFLASH-CTRL Design Specification.
 *
 * Copyright (c) 2014 Axis Communication AB, Lund, Sweden.
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

#ifndef _EVATRONIX_NAND_H_
#define  _EVATRONIX_NAND_H_

#include <linux/bitops.h> /* for ffs() */

/* Register offsets for Evatronix NANDFLASH-CTRL IP */
/* Register field shift values and masks are interespersed as it makes
 * them easier to locate. */
/* We use shift values rather than direct masks (e.g. 0x0000d000), as the
 * hardware manual lists the bit number, making the definitions below
 * easier to verify against the manual. */
/* All (known) registers are here, but we only put in the bit fields
 * for the fields we need. */
/* We try to be consistent regarding _SIZE/_MASK/_value macros so as to
 * get a consistent layout here, except for trivial cases where there is
 * only a single bit or field in a register at bit offset 0. */

#define COMMAND_REG		0x00
/* The masks reflect the input data to the MAKE_COMMAND macro, rather than
 * the bits in the register itself. These macros are not intended to be
 * used by the user, who should use the MAKE_COMMAND et al macros. */
#define _CMD_SEQ_SHIFT			0
#define _INPUT_SEL_SHIFT		6
#define _DATA_SEL_SHIFT			7
#define _CMD_0_SHIFT			8
#define _CMD_1_3_SHIFT			16
#define _CMD_2_SHIFT			24

#define _CMD_SEQ_MASK			0x3f
#define _INPUT_SEL_MASK			1
#define _DATA_SEL_MASK			1
#define _CMD_MASK			0xff /* for all CMD_foo */

#define MAKE_COMMAND(CMD_SEQ, INPUT_SEL, DATA_SEL, CMD_0, CMD_1_3, CMD_2) \
	((((CMD_SEQ)	& _CMD_SEQ_MASK)	<< _CMD_SEQ_SHIFT)	| \
	 (((INPUT_SEL)	& _INPUT_SEL_MASK)	<< _INPUT_SEL_SHIFT)	| \
	 (((DATA_SEL)	& _DATA_SEL_MASK)	<< _DATA_SEL_SHIFT)	| \
	 (((CMD_0)	& _CMD_MASK)		<< _CMD_0_SHIFT)	| \
	 (((CMD_1_3)	& _CMD_MASK)		<< _CMD_1_3_SHIFT)	| \
	 (((CMD_2)	& _CMD_MASK)		<< _CMD_2_SHIFT))

#define INPUT_SEL_SIU			0
#define INPUT_SEL_DMA			1
#define DATA_SEL_FIFO			0
#define DATA_SEL_DATA_REG		1

#define CONTROL_REG		0x04
#define CONTROL_BLOCK_SIZE_32		(0 << 6)
#define CONTROL_BLOCK_SIZE_64		(1 << 6)
#define CONTROL_BLOCK_SIZE_128		(2 << 6)
#define CONTROL_BLOCK_SIZE_256		(3 << 6)
#define CONTROL_BLOCK_SIZE(SIZE)	((ffs(SIZE) - 6) << 6)
#define CONTROL_ECC_EN			(1 << 5)
#define CONTROL_INT_EN			(1 << 4)
#define CONTROL_ECC_BLOCK_SIZE_256	(0 << 1)
#define CONTROL_ECC_BLOCK_SIZE_512	(1 << 1)
#define CONTROL_ECC_BLOCK_SIZE_1024	(2 << 1)
#define CONTROL_ECC_BLOCK_SIZE(SIZE)	((ffs(SIZE) - 9) << 1)
#define STATUS_REG		0x08
#define STATUS_MEM_ST(CS)		(1 << (CS))
#define STATUS_CTRL_STAT		(1 << 8)
#define STATUS_MASK_REG		0x0C
#define STATE_MASK_SHIFT		0
#define STATUS_MASK_STATE_MASK(MASK)	(((MASK) & 0xff) << STATE_MASK_SHIFT)
#define ERROR_MASK_SHIFT		8
#define STATUS_MASK_ERROR_MASK(MASK)	(((MASK) & 0xff) << ERROR_MASK_SHIFT)
#define INT_MASK_REG		0x10
#define INT_MASK_ECC_INT_EN(CS)		(1 << (24 + (CS)))
#define INT_MASK_STAT_ERR_INT_EN(CS)	(1 << (16 + (CS)))
#define INT_MASK_MEM_RDY_INT_EN(CS)	(1 << (8 + (CS)))
#define INT_MASK_DMA_INT_EN		(1 << 3)
#define INT_MASK_DATA_REG_EN		(1 << 2)
#define INT_MASK_CMD_END_INT_EN		(1 << 1)
#define INT_STATUS_REG		0x14
#define INT_STATUS_ECC_INT_FL(CS)	(1 << (24 + (CS)))
#define INT_STATUS_STAT_ERR_INT_FL(CS)	(1 << (16 + (CS)))
#define INT_STATUS_MEM_RDY_INT_FL(CS)	(1 << (8 + (CS)))
#define INT_STATUS_DMA_INT_FL		(1 << 3)
#define INT_STATUS_DATA_REG_FL		(1 << 2)
#define INT_STATUS_CMD_END_INT_FL	(1 << 1)
#define ECC_CTRL_REG		0x18
#define ECC_CTRL_ECC_CAP_2		(0 << 0)
#define ECC_CTRL_ECC_CAP_4		(1 << 0)
#define ECC_CTRL_ECC_CAP_8		(2 << 0)
#define ECC_CTRL_ECC_CAP_16		(3 << 0)
#define ECC_CTRL_ECC_CAP_24		(4 << 0)
#define ECC_CTRL_ECC_CAP_32		(5 << 0)
#define ECC_CTRL_ECC_CAP(B)		((B) < 24 ? ffs(B) - 2 : (B) / 6)
/* # ECC corrections that are acceptable during read before setting OVER flag */
#define ECC_CTRL_ECC_THRESHOLD(VAL)	(((VAL) & 0x3f) << 8)
#define ECC_OFFSET_REG		0x1C
#define ECC_STAT_REG		0x20
/* Correctable error flag(s) */
#define ECC_STAT_ERROR(CS)		(1 << (0 + (CS)))
/* Uncorrectable error flag(s) */
#define ECC_STAT_UNC(CS)		(1 << (8 + (CS)))
/* Acceptable errors level overflow flag(s) */
#define ECC_STAT_OVER(CS)		(1 << (16 + (CS)))
#define ADDR0_COL_REG		0x24
#define ADDR0_ROW_REG		0x28
#define ADDR1_COL_REG		0x2C
#define ADDR1_ROW_REG		0x30
#define PROTECT_REG		0x34
#define FIFO_DATA_REG		0x38
#define DATA_REG_REG		0x3C
#define DATA_REG_SIZE_REG	0x40
#define DATA_REG_SIZE_DATA_REG_SIZE(SIZE) (((SIZE) - 1) & 3)
#define DEV0_PTR_REG		0x44
#define DEV1_PTR_REG		0x48
#define DEV2_PTR_REG		0x4C
#define DEV3_PTR_REG		0x50
#define DEV4_PTR_REG		0x54
#define DEV5_PTR_REG		0x58
#define DEV6_PTR_REG		0x5C
#define DEV7_PTR_REG		0x60
#define DMA_ADDR_L_REG		0x64
#define DMA_ADDR_H_REG		0x68
#define DMA_CNT_REG		0x6C
#define DMA_CTRL_REG		0x70
#define DMA_CTRL_DMA_START		(1 << 7) /* start on command */
#define DMA_CTRL_DMA_MODE_SG		(1 << 5) /* scatter/gather mode */
#define DMA_CTRL_DMA_BURST_I_P_4	(0 << 2) /* incr. precise burst */
#define DMA_CTRL_DMA_BURST_S_P_16	(1 << 2) /* stream precise burst */
#define DMA_CTRL_DMA_BURST_SINGLE	(2 << 2) /* single transfer */
#define DMA_CTRL_DMA_BURST_UNSPEC	(3 << 2) /* burst of unspec. length */
#define DMA_CTRL_DMA_BURST_I_P_8	(4 << 2) /* incr. precise burst */
#define DMA_CTRL_DMA_BURST_I_P_16	(5 << 2) /* incr. precise burst */
#define DMA_CTRL_ERR_FLAG		(1 << 1) /* read only */
#define DMA_CTRL_DMA_READY		(1 << 0) /* read only */
#define BBM_CTRL_REG		0x74
#define MEM_CTRL_REG		0x80
#define MEM_CTRL_MEM_CE(CE)		(((CE) & 7) << 0)
#define MEM_CTRL_BANK_SEL(BANK)		(((BANK) & 7) << 16)
#define DATA_SIZE_REG		0x84
#define TIMINGS_ASYN_REG	0x88
#define TIMINGS_SYN_REG		0x8C
#define TIME_SEQ_0_REG		0x90
#define TIME_SEQ_1_REG		0x94
#define TIME_GEN_SEQ_0_REG	0x98
#define TIME_GEN_SEQ_1_REG	0x9C
#define TIME_GEN_SEQ_2_REG	0xA0
#define FIFO_INIT_REG		0xB0
#define FIFO_INIT_FIFO_INIT			1 /* Flush FIFO */
#define FIFO_STATE_REG		0xB4
#define FIFO_STATE_DF_W_EMPTY		(1 << 7)
#define FIFO_STATE_DF_R_FULL		(1 << 6)
#define FIFO_STATE_CF_ACCPT_W		(1 << 5)
#define FIFO_STATE_CF_ACCPT_R		(1 << 4)
#define FIFO_STATE_CF_FULL		(1 << 3)
#define FIFO_STATE_CF_EMPTY		(1 << 2)
#define FIFO_STATE_DF_W_FULL		(1 << 1)
#define FIFO_STATE_DF_R_EMPTY		(1 << 0)
#define GEN_SEQ_CTRL_REG	0xB8		/* aka GENERIC_SEQ_CTRL */
#define _CMD0_EN_SHIFT			0
#define _CMD1_EN_SHIFT			1
#define _CMD2_EN_SHIFT			2
#define _CMD3_EN_SHIFT			3
#define _COL_A0_SHIFT			4
#define _COL_A1_SHIFT			6
#define _ROW_A0_SHIFT			8
#define _ROW_A1_SHIFT			10
#define _DATA_EN_SHIFT			12
#define _DELAY_EN_SHIFT			13
#define _IMD_SEQ_SHIFT			15
#define _CMD3_SHIFT			16
#define ECC_CNT_REG		0x14C
#define ECC_CNT_ERR_LVL_MASK		0x3F

#define _CMD0_EN_MASK			1
#define _CMD1_EN_MASK			1
#define _CMD2_EN_MASK			1
#define _CMD3_EN_MASK			1
#define _COL_A0_MASK			3
#define _COL_A1_MASK			3
#define _ROW_A0_MASK			3
#define _ROW_A1_MASK			3
#define _DATA_EN_MASK			1
#define _DELAY_EN_MASK			3
#define _IMD_SEQ_MASK			1
#define _CMD3_MASK			0xff

/* DELAY_EN field values, non-shifted */
#define _BUSY_NONE			0
#define _BUSY_0				1
#define _BUSY_1				2

/* Slightly confusingly, the DELAYx_EN fields enable BUSY phases. */
#define MAKE_GEN_CMD(CMD0_EN, CMD1_EN, CMD2_EN, CMD3_EN, \
		     COL_A0, ROW_A0, COL_A1, ROW_A1, \
		     DATA_EN, BUSY_EN, IMMEDIATE_SEQ, CMD3) \
	((((CMD0_EN)	& _CMD0_EN_MASK)	<< _CMD0_EN_SHIFT)	| \
	 (((CMD1_EN)	& _CMD1_EN_MASK)	<< _CMD1_EN_SHIFT)	| \
	 (((CMD2_EN)	& _CMD2_EN_MASK)	<< _CMD2_EN_SHIFT)	| \
	 (((CMD3_EN)	& _CMD3_EN_MASK)	<< _CMD3_EN_SHIFT)	| \
	 (((COL_A0)	& _COL_A0_MASK)		<< _COL_A0_SHIFT)	| \
	 (((COL_A1)	& _COL_A1_MASK)		<< _COL_A1_SHIFT)	| \
	 (((ROW_A0)	& _ROW_A0_MASK)		<< _ROW_A0_SHIFT)	| \
	 (((ROW_A1)	& _ROW_A1_MASK)		<< _ROW_A1_SHIFT)	| \
	 (((DATA_EN)	& _DATA_EN_MASK)	<< _DATA_EN_SHIFT)	| \
	 (((BUSY_EN)	& _DELAY_EN_MASK)	<< _DELAY_EN_SHIFT)	| \
	 (((IMMEDIATE_SEQ) & _IMD_SEQ_MASK)	<< _IMD_SEQ_SHIFT)	| \
	 (((CMD3)	& _CMD3_MASK)		<< _CMD3_SHIFT))

/* The sequence encodings are not trivial. The ones we use are listed here. */
#define _SEQ_0			0x00 /* send one cmd, then wait for ready */
#define _SEQ_1			0x21 /* send one cmd, one addr, fetch data */
#define _SEQ_2			0x22 /* send one cmd, one addr, fetch data */
#define _SEQ_4			0x24 /* single cycle write then read */
#define _SEQ_10			0x2A /* read page */
#define _SEQ_12			0x0C /* write page, don't wait for R/B */
#define _SEQ_18			0x32 /* read page using general cycle */
#define _SEQ_19			0x13 /* write page using general cycle */
#define _SEQ_14			0x0E /* 3 address cycles, for block erase */

#define MLUN_REG		0xBC
#define DEV0_SIZE_REG		0xC0
#define DEV1_SIZE_REG		0xC4
#define DEV2_SIZE_REG		0xC8
#define DEV3_SIZE_REG		0xCC
#define DEV4_SIZE_REG		0xD0
#define DEV5_SIZE_REG		0xD4
#define DEV6_SIZE_REG		0xD8
#define DEV7_SIZE_REG		0xDC
#define SS_CCNT0_REG		0xE0
#define SS_CCNT1_REG		0xE4
#define SS_SCNT_REG		0xE8
#define SS_ADDR_DEV_CTRL_REG	0xEC
#define SS_CMD0_REG		0xF0
#define SS_CMD1_REG		0xF4
#define SS_CMD2_REG		0xF8
#define SS_CMD3_REG		0xFC
#define SS_ADDR_REG		0x100
#define SS_MSEL_REG		0x104
#define SS_REQ_REG		0x108
#define SS_BRK_REG		0x10C
#define DMA_TLVL_REG		0x114
#define DMA_TLVL_MAX		0xFF
#define AES_CTRL_REG		0x118
#define AES_DATAW_REG		0x11C
#define AES_SVECT_REG		0x120
#define CMD_MARK_REG		0x124
#define LUN_STATUS_0_REG	0x128
#define LUN_STATUS_1_REG	0x12C
#define TIMINGS_TOGGLE_REG	0x130
#define TIME_GEN_SEQ_3_REG	0x134
#define SQS_DELAY_REG		0x138
#define CNE_MASK_REG		0x13C
#define CNE_VAL_REG		0x140
#define CNA_CTRL_REG		0x144
#define INTERNAL_STATUS_REG	0x148
#define ECC_CNT_REG		0x14C
#define PARAM_REG_REG		0x150
#define PARAM_REG_DMA64		(1 << 27)

/* NAND flash command generation */

/* NAND flash command codes */
#define NAND_RESET		0xff
#define NAND_READ_STATUS	0x70
#define NAND_READ_ID		0x90
#define NAND_READ_ID_ADDR_STD	0x00	/* address written to ADDR0_COL */
#define NAND_READ_ID_ADDR_ONFI	0x20	/* address written to ADDR0_COL */
#define NAND_PARAM		0xEC
#define NAND_PAGE_READ		0x00
#define NAND_PAGE_READ_END	0x30
#define NAND_BLOCK_ERASE	0x60
#define NAND_BLOCK_ERASE_END	0xd0
#define NAND_PAGE_WRITE		0x80
#define NAND_PAGE_WRITE_END	0x10

#define _DONT_CARE 0x00 /* When we don't have anything better to say */


/* Assembled values for putting into COMMAND register */

/* Reset NAND flash */

/* Uses SEQ_0: non-directional sequence, single command, wait for ready */
#define COMMAND_RESET \
	MAKE_COMMAND(_SEQ_0, INPUT_SEL_SIU, DATA_SEL_FIFO, \
		NAND_RESET, _DONT_CARE, _DONT_CARE)

/* Read status */

/* Uses SEQ_4: single command, then read data via DATA_REG */
#define COMMAND_READ_STATUS \
	MAKE_COMMAND(_SEQ_4, INPUT_SEL_SIU, DATA_SEL_DATA_REG, \
		NAND_READ_STATUS, _DONT_CARE, _DONT_CARE)

/* Read ID */

/* Uses SEQ_1: single command, ADDR0_COL, then read data via FIFO */
/* ADDR0_COL is set to NAND_READ_ID_ADDR_STD for non-ONFi, and
 * NAND_READ_ID_ADDR_ONFI for ONFi.
 * The controller reads 5 bytes in the non-ONFi case, and 4 bytes in the
 * ONFi case, so the data reception (DMA or FIFO_REG) needs to be set up
 * accordingly. */
#define COMMAND_READ_ID \
	MAKE_COMMAND(_SEQ_1, INPUT_SEL_DMA, DATA_SEL_FIFO, \
		NAND_READ_ID, _DONT_CARE, _DONT_CARE)

#define COMMAND_PARAM \
	MAKE_COMMAND(_SEQ_2, INPUT_SEL_DMA, DATA_SEL_FIFO, \
		NAND_PARAM, _DONT_CARE, _DONT_CARE)

/* Page read via slave interface (FIFO_DATA register) */

/* Standard 5-cycle read command, with 0x30 end-of-cycle marker */
/* Uses SEQ_10: CMD0 + 5 address cycles + CMD2, read data */
#define COMMAND_READ_PAGE_STD \
	MAKE_COMMAND(_SEQ_10, INPUT_SEL_SIU, DATA_SEL_FIFO, \
		NAND_PAGE_READ, _DONT_CARE, NAND_PAGE_READ_END)

/* 4-cycle read command, together with GEN_SEQ_CTRL_READ_PAGE_4CYCLE */
/* Uses SEQ_18 (generic command sequence, see GEN_SEQ_ECTRL_READ_PAGE_4CYCLE)):
   CMD0 + 2+2 address cycles + CMD2, read data */
#define COMMAND_READ_PAGE_GEN \
	MAKE_COMMAND(_SEQ_18, INPUT_SEL_SIU, DATA_SEL_FIFO, \
		NAND_PAGE_READ, _DONT_CARE, NAND_PAGE_READ_END)

/* Page read via master interface (DMA) */

/* Standard 5-cycle read command, with 0x30 end-of-cycle marker */
/* Uses SEQ_10: CMD0 + 5 address cycles + CMD2, read data */
#define COMMAND_READ_PAGE_DMA_STD \
	MAKE_COMMAND(_SEQ_10, INPUT_SEL_DMA, DATA_SEL_FIFO, \
		NAND_PAGE_READ, _DONT_CARE, NAND_PAGE_READ_END)

/* 4-cycle read command, together with GEN_SEQ_CTRL_READ_PAGE_4CYCLE */
/* Uses SEQ_18 (generic command sequence, see GEN_SEQ_ECTRL_READ_PAGE_4CYCLE)):
   CMD0 + 2+2 address cycles + CMD2, read data */
#define COMMAND_READ_PAGE_DMA_GEN \
	MAKE_COMMAND(_SEQ_18, INPUT_SEL_DMA, DATA_SEL_FIFO, \
		NAND_PAGE_READ, _DONT_CARE, NAND_PAGE_READ_END)

/* Page write via master interface (DMA) */

/* Uses SEQ_12: CMD0 + 5 address cycles + write data + CMD1 */
#define COMMAND_WRITE_PAGE_DMA_STD \
	MAKE_COMMAND(_SEQ_12, INPUT_SEL_DMA, DATA_SEL_FIFO, \
		NAND_PAGE_WRITE, NAND_PAGE_WRITE_END, _DONT_CARE)

/* Uses SEQ_19: CMD0 + 4 address cycles + write data + CMD1 */
#define COMMAND_WRITE_PAGE_DMA_GEN \
	MAKE_COMMAND(_SEQ_19, INPUT_SEL_DMA, DATA_SEL_FIFO, \
		NAND_PAGE_WRITE, NAND_PAGE_WRITE_END, _DONT_CARE)

/* Block erase */

/* Uses SEQ_14: CMD0 + 3 address cycles + CMD1 */
#define COMMAND_BLOCK_ERASE \
	MAKE_COMMAND(_SEQ_14, INPUT_SEL_SIU, DATA_SEL_FIFO, \
		NAND_BLOCK_ERASE, NAND_BLOCK_ERASE_END, _DONT_CARE)

/* Assembled values for putting into GEN_SEQ_CTRL register */

/* General command sequence specification for 4 cycle PAGE_READ command */
#define GEN_SEQ_CTRL_READ_PAGE_4CYCLE \
	MAKE_GEN_CMD(1, 0, 1, 0,	/* enable command 0 and 2 phases */ \
		     2, 2,		/* col A0 2 cycles, row A0 2 cycles */ \
		     0, 0,		/* col A1, row A1 not used */ \
		     1,			/* data phase enabled */ \
		     _BUSY_0,		/* busy0 phase enabled */ \
		     0,			/* immediate cmd execution disabled */ \
		     _DONT_CARE)	/* command 3 code not needed */

/* General command sequence specification for 4 cycle PAGE_PROGRAM command */
#define GEN_SEQ_CTRL_WRITE_PAGE_4CYCLE \
	MAKE_GEN_CMD(1, 1, 0, 0,	/* enable command 0 and 1 phases */ \
		     2, 2,		/* col A0 2 cycles, row A0 2 cycles */ \
		     0, 0,		/* col A1, row A1 not used */ \
		     1,			/* data phase enabled */ \
		     _BUSY_1,		/* busy1 phase enabled */ \
		     0,			/* immediate cmd execution disabled */ \
		     _DONT_CARE)	/* command 3 code not needed */

/* BCH ECC size calculations. Should really go somewhere else? */
/* From "Mr. NAND's Wild Ride: Warning: Suprises Ahead", by Robert Pierce,
 * Denali Software Inc. 2009, table on page 5 */
/* Use 8 bit correction as base. */
#define ECC8_BYTES(BLKSIZE) (ffs(BLKSIZE) + 3)
/* The following is valid for 4..24 bits of correction. */
#define ECC_BYTES(CAP, BLKSIZE) ((ECC8_BYTES(BLKSIZE) * (CAP) + 7) / 8)
/* Cadence IP block uses a slightly different calculation */
#define ECC_BYTES_CAD(CAP, BLKSIZE) (((ECC8_BYTES(512) + 1) * (CAP) + 7) / 8)

/* Due to the way mtd interracts with the driver we need to know the maximum
 * number of bytes of data that is read by the READID and PARAM commands. */
#define MAX_READID_DATA 8
#define MAX_PARAM_DATA 768

#endif /* _EVATRONIX_NAND_H_ */

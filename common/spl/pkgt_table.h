/*
 * Renesas RZ/N1 Linux tools: Package Table format
 * (C) 2015-2016 Renesas Electronics Europe, LTD
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */

#ifndef _PKGT_TABLE_H_
#define _PKGT_TABLE_H_

/*
 * The package table data structure is what tells the SPL bootloader to load
 * bits from the QSPI and NAND flash. It is made to be able to load uImage
 * files, or the Renesas specific signed package formats.
 * The table itself is made to have built in redundancy, so it can be read
 * from the NAND with bit errors, due to NAND rot.
 */
#define PKGT_MAX_TBL_ENTRIES	16	/* Must be power of 2 */
#define PKGT_REDUNDANCY_COUNT	128
/* "PKGT" */
#define PKGT_MAGIC 		(('P'<<0)|('K'<<8)|('G'<<16)|('T'<<24))

enum {
	PKGT_TYPE_UIMAGE = 0,	/* uImage */
	PKGT_TYPE_RPKG 	= 1,	/* Renesas RAW signed binary package RPKG */
	PKGT_TYPE_SPKG 	= 2,	/* Renesas ROM Compatible Signed SPKG */
	PKGT_TYPE_RAW	= 3,	/* No headers (Offset is an address) */
};
enum {
	PKGT_CORE_CA70	= 0,	/* Cortex A7 - 1st core */
	PKGT_CORE_CA71	= 1,	/* Cortex A7 - 2nd core (currently unused) */
	PKGT_CORE_CM3	= 2,	/* Cortex M3 */
};
enum {
	PKGT_KIND_CODE	= 0,	/* Code for a core */
	PKGT_KIND_DTB	= 1,	/* Device tree blob -- pass as parameter to core X */
	PKGT_KIND_DATA	= 3,	/* Data */
};
enum {
	PKGT_SRC_SAME	= 0,	/* Same media as the PKGT table */
	PKGT_SRC_QSPI	= 1,	/* Offset is in QSPI */
	PKGT_SRC_NAND	= 2,	/* Offset is in NAND */
};

/* bit positions in pkg_entry->pkg */
enum {
	PKG_TYPE_BIT	= 0,
	PKG_CORE_BIT	= 4,
	PKG_KIND_BIT	= 8,
	PKG_SRC_BIT	= 12,
	PKG_BACKUP_BIT	= 16,	/* Mark this entry has an alternative */
	PKG_NONSEC_BIT	= 17,	/* Start code in NONSEC mode */
	PKG_HYP_BIT	= 18,	/* Start code in HYP(from NONSEC) mode */
	PKG_NOCRC_BIT	= 19,	/* Skip CRC (if on QSPI) */
	PKG_INITRAMFS_BIT	= 20, /* Data package is linux initramfs */
	PKG_ALT_BIT	= 21, 	/* Code is alternative bootloader */
	/* Rest of the bits are free */
};

/* bit positions in pkg_table->pkgt */
enum {
	PKGT_COUNT_BIT	= 0,
};

#define PKG_TYPE(e) (((e)->pkg >> PKG_TYPE_BIT) & 0xf)
#define PKG_CORE(e) (((e)->pkg >> PKG_CORE_BIT) & 0xf)
#define PKG_KIND(e) (((e)->pkg >> PKG_KIND_BIT) & 0xf)
#define PKG_SRC(e) (((e)->pkg >> PKG_SRC_BIT) & 0xf)
#define PKG_SET_SRC(e, s) (e)->pkg = ((e)->pkg & ~(0xf << PKG_SRC_BIT)) | \
				((s & 0xf) << PKG_SRC_BIT)
#define PKG_IS_BACKUP(e) ((e)->pkg & (1 << PKG_BACKUP_BIT))
#define PKG_IS_HYP(e) ((e)->pkg & (1 << PKG_HYP_BIT))
/* HYP implies NONSEC */
#define PKG_IS_NONSEC(e) (PKG_IS_HYP(e) || ((e)->pkg & (1 << PKG_NONSEC_BIT)))
#define PKG_IS_NOCRC(e) ((e)->pkg & (1 << PKG_NOCRC_BIT))
#define PKG_IS_INITRAMFS(e) ((e)->pkg & (1 << PKG_INITRAMFS_BIT))
#define PKG_IS_ALT(e) ((e)->pkg & (1 << PKG_ALT_BIT))

struct pkg_entry {
	uint32_t pkg;		/* Type, Core, Kind, Src + Flags */
	uint32_t offset;	/* offset into the current media for the image */
} __attribute__((packed));

struct pkg_table {
	uint32_t magic;		/* "PKGT" in big endian */
	uint32_t pkgt;		/* number of pkg_table entries, etc */
	uint32_t crc;		/* crc of nr_entries entries following */
	uint32_t reserved;	/* reserved for future use */
	struct pkg_entry entries[PKGT_MAX_TBL_ENTRIES];
} __attribute__((packed));

#endif

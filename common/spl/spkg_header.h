/*
 * Renesas RZ/N1 Linux tools: Package Table format
 * (C) 2015-2016 Renesas Electronics Europe, LTD
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */

#ifndef _SKGT_HEADER_H_
#define _SKGT_HEADER_H_

#define SPKG_HEADER_SIGNATURE (('R'<<0)|('Z'<<8)|('N'<<16)|('1'<<24))
#define SPKG_HEADER_COUNT	8
#define SPKG_BLP_SIZE		264

#define SPKG_HEADER_SIZE	24
#define SPKG_HEADER_SIZE_ALL	(SPKG_HEADER_SIZE * SPKG_HEADER_COUNT)
#define SPKG_HEADER_CRC_SIZE	4

/* Index into SPKG */
#define INDEX_BLP_START		SPKG_HEADER_SIZE_ALL
#define INDEX_IMAGE_START	(INDEX_BLP_START + SPKG_BLP_SIZE)

/* Flags, not supported by ROM code, only used for U-Boot SPL */
enum {
	SPKG_CODE_NONSEC_BIT = 0,
	SPKG_CODE_HYP_BIT,
};

/* SPKG header */
struct spkg_hdr {
	uint32_t 	signature;
	uint8_t		version;
	uint8_t		ecc;
	uint8_t 	ecc_scheme;
	uint8_t 	ecc_bytes;
	uint32_t 	payload_length; /* only HIGHER 24 bits */
	uint32_t 	load_address;
	uint32_t	execution_offset;
	uint32_t	crc; /* of this header */
} __attribute__((packed));

struct spkg_file {
	struct spkg_hdr		header[SPKG_HEADER_COUNT];
	uint8_t			blp[SPKG_BLP_SIZE];
	uint8_t			data[0];
	/* then the CRC */
} __attribute__((packed));

#endif

/*
 * This is a utility to create a SPKG file.
 * It packages the binary code into the SPKG.
 *
 * (C) Copyright 2016 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "spkg_header.h"

/* For Windows compatibility */
#ifndef htole32
#if __BYTE_ORDER == __LITTLE_ENDIAN
 #define htole32(x)	(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
 #define htole32(x)	__builtin_bswap32((uint32_t)(x))
#endif
#endif

#define MAX_PATH		300

// Note: Order of bit fields is not used, this is purely just holding the SPKG information.
struct spkg_header {
	char input[MAX_PATH];
	char output[MAX_PATH+5];
	unsigned int version:4;
	unsigned int ecc_enable:1;
	unsigned int ecc_block_size:2;
	unsigned int ecc_scheme:3;
	unsigned int ecc_bytes:8;
	unsigned int dummy_blp_length : 10;
	unsigned int payload_length:24;
	unsigned int spl_nonsec : 1;
	unsigned int spl_hyp : 1;
	unsigned int load_address;
	unsigned int execution_offset;
	uint32_t padding;
};

struct spkg_header g_header = {
	.version = 1,
	.padding = 256,
};

int verbose = 0;

static uint32_t
crc32(const uint8_t *message, uint32_t l)
{
	uint32_t crc = ~0;

	while (l--) {
		uint32_t byte = *message++;		// Get next byte.
		crc = crc ^ byte;
		for (int8_t j = 7; j >= 0; j--) {	// Do eight times.
			uint32_t mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
	}
	return ~crc;
}

static int spkg_write(
	struct spkg_header *h,
	FILE *file_Input,
	FILE *file_SPKG)
{
	int i;
	uint32_t length_inputfile;
	uint32_t length_read;
	uint32_t length_written;
	uint32_t length_total;
	uint32_t padding = 0;
	uint8_t *pData, *start;
	uint32_t crc;

	/* Calculate length of input file */
	fseek(file_Input, 0, SEEK_END);	// seek to end of file
	length_inputfile = ftell(file_Input);	// get current file pointer
	fseek(file_Input, 0, SEEK_SET);	// seek back to beginning of file

	/* Set payload_length field. */
	h->payload_length =
	    length_inputfile + h->dummy_blp_length + SPKG_HEADER_CRC_SIZE;

	/* Calculate total length of SPKG */
	length_total =
	    (SPKG_HEADER_SIZE * SPKG_HEADER_COUNT) + h->dummy_blp_length +
	    length_inputfile + SPKG_HEADER_CRC_SIZE;
	padding = h->padding ? h->padding - (length_total % h->padding) : 0;
	length_total += padding;
	/* Padding needs to be part of the payload size, otherwise the ROM DFU
	 * refuses to accept the extra bytes and return and error. */
	h->payload_length += padding;

	printf("Addr: 0x%08x ", h->load_address);
	printf("In: %8d ", length_inputfile);
	printf("padding to %3dKB: %6d ", h->padding / 1024, padding);
	printf("Total: 0x%08x ", length_total);
	printf("%s\n", h->output);

	/* Create and zero array for SPKG */
	pData = malloc(length_total);
	memset(pData, 0, length_total);

	/* Fill the SPKG with the headers */
	{
		struct spkg_hdr head = {
			.signature = SPKG_HEADER_SIGNATURE,
			.version = h->version,
			.ecc = (h->ecc_enable << 5) | (h->ecc_block_size << 1),
			.ecc_scheme = h->ecc_scheme,
			.ecc_bytes = h->ecc_bytes,
			.payload_length = htole32((h->payload_length << 8) |
				(h->spl_nonsec << SPKG_CODE_NONSEC_BIT) |
				(h->spl_hyp << SPKG_CODE_HYP_BIT)),
			.load_address = htole32(h->load_address),
			.execution_offset = htole32(h->execution_offset),
		};

		head.crc = crc32((uint8_t*)&head, sizeof(head) - SPKG_HEADER_CRC_SIZE);
		for (i = 0; i < SPKG_HEADER_COUNT; i++)
			((struct spkg_hdr*)pData)[i] = head;
	}

	start = pData + INDEX_BLP_START;

	/* Fill the SPKG with the Dummy BLp */
	for (i = 0; i < h->dummy_blp_length; i++)
		*start++ = 0x88;
	/* Fill the SPKG with the data from the code file. */
	length_read =
	    fread(start, sizeof(char), length_inputfile,
		  file_Input);

	if (length_read != length_inputfile) {
		fprintf(stderr, "Error reading %s: ferror=%d, feof=%d \n",
		       h->input, ferror(file_Input), feof(file_Input));

		return -1;
	}
	/* fill padding with flash friendly one bits */
	memset(start + length_inputfile + SPKG_HEADER_CRC_SIZE, 0xff, padding);

	/* Add Payload CRC */
	crc = crc32(&pData[INDEX_BLP_START],
		     h->dummy_blp_length + length_inputfile + padding);

	start += length_inputfile + padding;
	start[0] = crc;
	start[1] = crc >> 8;
	start[2] = crc >> 16;
	start[3] = crc >> 24;

	/* Write the completed SKPG to file */
	length_written = fwrite(pData, sizeof(char), length_total, file_SPKG);

	if (length_written != length_total) {
		fprintf(stderr, "Error writing to %s\n", h->output);
		return -1;
	}

	return 0;
}

const char * usage =
	"%s\n"
	"  [-i <filename>]	: input file\n"
	"  [-o <filename>]	: output file\n"
	"  [--load_address <hex constant>] : code load address\n"
	"  [--execution_offset <hex constant>] : starting offset\n"
	"  [--nand_ecc_enable] : Enable nand ECC\n"
	"  [--nand_ecc_blksize <hex constant>] : Block size code\n"
	"        0=256 bytes, 1=512 bytes, 2=1024 bytes\n"
	"  [--nand_ecc_scheme <hex constant>] : ECC scheme code\n"
	"        0=BCH2 1=BCH4 2=BCH8 3=BCH16 4=BCH24 5=BCH32\n"
	"  [--add_dummy_blp] : Add a passthru BLP\n"
	"  [--spl_nonsec] : Code package run in NONSEC\n"
	"  [--spl_hyp] : Code package run in HYP (and NONSEC)\n"
	"  [--padding <value>[K|M]] : Pass SPKG to <value> size block\n"
	;

static int spkg_parse_option(
	struct spkg_header *h,
	const char *name,
	const char *sz,
	uint32_t value )
{
//	printf("%s %s=%s %08x\n", __func__, name, sz, value);
	if (!strcmp("file_input", name) || !strcmp("i", name))
		strncpy(h->input, sz, sizeof(h->input));
	else if (!strcmp("file_output", name) || !strcmp("o", name))
		strncpy(h->output, sz, sizeof(h->output));
	else if (!strcmp("version", name))
		h->version = value;
	else if (!strcmp("load_address", name))
		h->load_address = value;
	else if (!strcmp("execution_offset", name))
		h->execution_offset = value;
	else if (!strcmp("nand_ecc_enable", name))
		h->ecc_enable = value;
	else if (!strcmp("nand_ecc_blksize", name))
		h->ecc_block_size = value;
	else if (!strcmp("nand_ecc_scheme", name))
		h->ecc_scheme = value;
	else if (!strcmp("nand_bytes_per_ecc_block", name))
		h->ecc_bytes = value;
	else if (!strcmp("add_dummy_blp", name))
		h->dummy_blp_length = value ? SPKG_BLP_SIZE : 0;
	else if (!strcmp("spl_nonsec", name))
		h->spl_nonsec = !!value;
	else if (!strcmp("spl_hyp", name))
		h->spl_hyp = !!value;
	else if (!strcmp("help", name) || !strcmp("h", name)) {
		fprintf(stderr, usage, "spkg_utility");
		exit(0);
	} else if (!strcmp("padding", name) && sz && value) {
		if (strchr(sz, 'K'))
			h->padding = value * 1024;
		else if (strchr(sz, 'M'))
			h->padding = value * 1024 * 1024;
		else
			h->padding = value;
	} else
		return -1;

	return 0;
}

int main(int argc, char *argv[])
{
	FILE *file_SPKG = NULL;
	FILE *file_Input = NULL;
	int result = -1;

	for (int i = 1; i < argc; i++) {
		unsigned long int value = 0;
		char *name = argv[i];
		char *sz = NULL;

		if (!name || name[0] != '-') {
			fprintf(stderr, "%s invalid argument '%s'\n",
				argv[0], argv[i]);
			return -1;
		}
		name++;
		if (name[0] == '-')
			name++;

		if (i < argc - 1 && argv[i+1][0] != '-')
			sz = argv[++i];

		if (sz) {
			if (!sscanf(sz, "0x%lx", &value))
				sscanf(sz, "%lu", &value);
		} else {
			value = 1;
		}
		if (spkg_parse_option(&g_header, name, sz, value)) {
			fprintf(stderr, "%s Error invalid '%s'\n",
				argv[0],  argv[i]);
			return -1;
		}
	}

	if (!g_header.input[0]) {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	if (!g_header.output[0])
		snprintf(g_header.output, sizeof(g_header.output),
			"%s.spkg", g_header.input);
	if (verbose)
		printf("%s -> %s\n", g_header.input, g_header.output);

	/*NOTE: Using binary mode as this seems necessary if running in Windows */
	file_SPKG = fopen(g_header.output, "wb");
	file_Input = fopen(g_header.input, "rb");

	if (!file_SPKG)
		perror(g_header.output);
	if (!file_Input)
		perror(g_header.input);

	if (file_Input && file_SPKG)
		result = spkg_write(&g_header, file_Input, file_SPKG);

	if (file_SPKG)
		fclose(file_SPKG);
	if (file_Input)
		fclose(file_Input);

	if (result >= 0) {
		if (verbose)
			printf("%s created \n", g_header.output);
	} else {
		fprintf(stderr, "ERROR creating %s\n", g_header.output);
	}

	return result;
}

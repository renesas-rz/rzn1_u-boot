/*
 * Renesas RZ/N1 Linux tools: pkgt_add_entry.c
 *
 * Normally, U-Boot SPL loads a uImage from NAND or SPI Flash from a fixed
 * offset into the media, and the uImage header contains all the information
 * needed.
 * The Renesas RZ/N1 version of U-Boot SPL is different, it can load uImages or
 * Renesas RPKGs, it can load multiple images and the images can be data, an
 * executable for the Cortex M3 processor or for the Cortex A7 processor.
 * In order to do this, it loads a 'Package Table' from a fixed offset into the
 * media. This program generates the 'Package Table' image, by adding entries
 * to an existing 'Package Table' image file. If the file doesn't exist, it will
 * create a new one.
 *
 * This software is licensed under the MIT License (MIT), details below.
 * Copyright (c) 2015 Renesas Electronics Europe Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <byteswap.h>
#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "pkgt_table.h"


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

#define OM(_n,_b,_m,_d) \
	{ .name=_n, .bit=_b, .value=_m, .mask = 0xf, .doc=_d }
#define O(_n,_b,_d) \
	{ .name=_n, .bit=_b, .value=1, .mask = 1, .doc=_d }
const struct {
	const char * name;
	const char * doc;
	int bit : 5;
	int value : 5;
	int mask : 5;
} options[] = {
	{ .doc = "Options for core selection:" },
	OM("ca70", PKG_CORE_BIT, PKGT_CORE_CA70,
		"[default]"),
	OM("ca71", PKG_CORE_BIT, PKGT_CORE_CA71,
		"Sets target core to Cortex A7 #1."),
	OM("cm3", PKG_CORE_BIT, PKGT_CORE_CM3,
		"Set target core to the the Cortex M3."),

	{ .doc = "Options for package content:" },
	OM("code", PKG_KIND_BIT, PKGT_KIND_CODE,
		"Set packet kind to Code. [default]"),
	OM("dtb", PKG_KIND_BIT, PKGT_KIND_DTB,
		"Set packet kind to to Device Tree blob."),
	OM("data", PKG_KIND_BIT, PKGT_KIND_DATA,
		"Set packet kind to to Data (or initramfs)."),

	{ .doc = "Options for package location:" },
	OM("same", PKG_SRC_BIT, PKGT_SRC_SAME,
		"Offset is in Boot flash [default]."),
	OM("qspi", PKG_SRC_BIT, PKGT_SRC_QSPI,
		"Offset is QSPI."),
	OM("nand", PKG_SRC_BIT, PKGT_SRC_NAND,
		"Offset is in NAND."),

	{ .doc = "Options for package kind:" },
	OM("u", PKG_TYPE_BIT, PKGT_TYPE_UIMAGE, ""),
	OM("uimage", PKG_TYPE_BIT, PKGT_TYPE_UIMAGE,
		"Offset containts a (u-boot) uimage"),
	OM("rpkg", PKG_TYPE_BIT, PKGT_TYPE_RPKG,
		"Is a Raw package."),
	OM("spkg", PKG_TYPE_BIT, PKGT_TYPE_SPKG,
		"Is a Renesas SPKG [default]."),
	OM("raw", PKG_TYPE_BIT, PKGT_TYPE_RAW,
		"Offset has no headers."),

	{ .doc = "Modifier flags:" },
	O("backup", PKG_BACKUP_BIT,
		"Mark this entry as an alternate to load in case\n"
		"\tThe primary one has not been loaded due to errors."),
	O("nonsec", PKG_NONSEC_BIT,
		"Switch to NONSEC mode for this code blob."),
	O("hyp", PKG_HYP_BIT,
		"Switch to HYP mode for this code blob."),
	O("nocrc", PKG_NOCRC_BIT,
		"Don't check SPKG payload CRC."),
	O("initramfs", PKG_INITRAMFS_BIT,
		"Data payload is a linux initramfs."),
	O("alt", PKG_ALT_BIT,
		"Code is alternative payload (u-boot)."),

	{ 0 }, /* trailer */
};

static void print_usage(
	char * progname, int exitcode)
{
	printf("RZ/N1: Add an entry to the U-Boot/SPL Package Table.\n");
	printf("Use:\n");
	printf("  %s [<options>] -offset <offset> <file>\n", progname);

	for (int i = 0; options[i].doc; i++) {
		if (options[i].name)
			printf("  -%-9.9s %s\n",
				options[i].name, options[i].doc);
		else
			printf(" %s\n", options[i].doc);
	}
	printf(" Mandatory options:\n");
	printf("  -offset <offset>  Offset in source (hex)\n");
	printf("  <file>    Filename of PKG Table file to be extended, or written if it doesn't exist.\n");
	printf(" (Table size is %ld bytes; with redundancy of %d it is %ld total)\n",
		sizeof(struct pkg_table), PKGT_REDUNDANCY_COUNT,
		sizeof(struct pkg_table) * PKGT_REDUNDANCY_COUNT);
	exit(exitcode);
}

int main(int argc, char *argv[])
{
	struct pkg_entry *entry;
	struct pkg_table table = { };
	const char *image_name = NULL;
	FILE *fp;
	uint8_t corrupt = 0;
	uint32_t offset = ~0;
	uint32_t pkgt = PKGT_TYPE_SPKG << PKG_TYPE_BIT;
	int pkg_count = 0, i;

	for (int i = 1; i < argc; i++) {
		char *p = argv[i];

		/* treat --<param> same as -<param> */
		if (!strncmp(p, "--", 2))
			p++;
		if (p[0] == '-') {
			for (int i = 0; options[i].doc; i++)
				if (options[i].name &&
						!strcmp(p + 1, options[i].name)) {
					pkgt &= ~(options[i].mask << options[i].bit);
					pkgt |= options[i].value << options[i].bit;
					goto next;
				}
		}
		if (!strcmp(p, "-debug-corrupt"))
			corrupt++;
		else if (!strncmp(p, "-o", 2) && i < argc-1) {
			char *endptr;
			i++;
			offset = strtoul(argv[i], &endptr, 0);
		} else if (!strncmp(p, "-h", 2))
			print_usage(argv[0], 0);
		else if (p[0] == '-' || image_name) {
			fprintf(stderr, "%s invalid argument '%s' -- use --help\n",
				argv[0], argv[i]);
			exit(1);
		} else
			image_name = argv[i];
	next: ;
	}
	if (!image_name) {
		fprintf(stderr, "%s missing filename -- use --help\n",
			argv[0]);
		exit(1);
	}
	if (offset == ~0) {
		fprintf(stderr, "%s invalid offset -- use --help\n",
			argv[0]);
		exit(1);
	}

	/* If the file exists, read it in */
	if (access(image_name, F_OK) != -1) {
		size_t len;
		fp = fopen(image_name, "rb");
		len = fread(&table, 1, sizeof(table), fp);
		fclose(fp);

		if (len != sizeof(table)) {
			fprintf(stderr, "%s file is too short!\n", image_name);
			exit(1);
		}
		if (table.magic != PKGT_MAGIC) {
			fprintf(stderr, "%s file does not have the magic number!\n",
				image_name);
			exit(1);
		}
		pkg_count = (table.pkgt >> PKGT_COUNT_BIT) & (PKGT_MAX_TBL_ENTRIES-1);
		if (pkg_count == PKGT_MAX_TBL_ENTRIES - 1) {
			fprintf(stderr, "%s is full (%d entries)!\n",
				image_name, PKGT_MAX_TBL_ENTRIES);
			exit(1);
		}
	}

	entry = &table.entries[pkg_count];
	entry->pkg = pkgt;
	entry->offset   = offset;
	/* Now update table header */
	table.magic = PKGT_MAGIC;
	table.pkgt &= ~((PKGT_MAX_TBL_ENTRIES-1) << PKGT_COUNT_BIT);
	table.pkgt |= (pkg_count + 1) << PKGT_COUNT_BIT;
	table.crc = 0;
	table.crc = crc32((uint8_t*)&table, sizeof(table));

	fp = fopen(image_name, "wb");
	if (!fp) {
		perror(image_name);
		exit(1);
	}
	for (i = 0; i < PKGT_REDUNDANCY_COUNT; i++) {
		struct pkg_table w = table;
		/* if debugging, corrupt the first entr(ies) to validate SPL
		 * error recovery. Pass -debug-corrupt multiple times to
		 * corrupt extra headers */
		if (corrupt--)
			w.crc = 0xcafef00d;
		fwrite(&w, 1, sizeof(w), fp);
	}
	fclose(fp);
}

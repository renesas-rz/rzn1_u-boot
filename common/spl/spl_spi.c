/*
 * Copyright (C) 2011 OMICRON electronics GmbH
 *
 * based on drivers/mtd/nand/nand_spl_load.c
 *
 * Copyright (C) 2011
 * Heiko Schocher, DENX Software Engineering, hs@denx.de.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <spi.h>
#include <spi_flash.h>
#include <errno.h>
#include <spl.h>

#if defined(CONFIG_SPL_OS_BOOT) && defined(CONFIG_SYS_SPI_KERNEL_OFFS)
/*
 * Load the kernel, check for a valid header we can parse, and if found load
 * the kernel and then device tree.
 */
static int spi_load_image_os(struct spl_image_info *spl_image,
			     struct spi_flash *flash,
			     struct image_header *header)
{
	int err;

	/* Read for a header, parse or error out. */
	spi_flash_read(flash, CONFIG_SYS_SPI_KERNEL_OFFS, sizeof(*header),
		       (void *)header);

	if (image_get_magic(header) != IH_MAGIC)
		return -1;

	err = spl_parse_image_header(spl_image, header);
	if (err)
		return err;

	spi_flash_read(flash, CONFIG_SYS_SPI_KERNEL_OFFS,
		       spl_image->size, (void *)spl_image->load_addr);

	/* Read device tree. */
	spi_flash_read(flash, CONFIG_SYS_SPI_ARGS_OFFS,
		       CONFIG_SYS_SPI_ARGS_SIZE,
		       (void *)CONFIG_SYS_SPL_ARGS_ADDR);

	return 0;
}
#endif

static ulong spl_spi_fit_read(struct spl_load_info *load, ulong sector,
			      ulong count, void *buf)
{
	struct spi_flash *flash = load->dev;
	ulong ret;

	ret = spi_flash_read(flash, sector, count, buf);
	if (!ret)
		return count;
	else
		return 0;
}

struct spi_flash *spl_spi_probe(void)
{
	struct spi_flash *flash;

	flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED, SPI_MODE_3);
	if (!flash) {
		puts("SPI probe failed.\n");
		hang();
	}

	return flash;
}

int spl_spi_load_one_uimage(struct spl_image_info *spl_image,
	struct spi_flash *flash, u32 offset)
{
	struct image_header header;
	int err = 0;

	/*
	 * Skip loading the mkimage header. This is only necessary for block
	 * based storage systems where the API requires loading whole blocks
	 * into aligned destinations. The SF layer can load any amount of data
	 * from flash and store it anywhere.
	 */
	spl_image->flags |= SPL_COPY_PAYLOAD_ONLY;

	/* Load u-boot mkimage header */
	err = spi_flash_read(flash, offset, sizeof(header), (void *)&header);
	if (err)
		return err;

	if (IS_ENABLED(CONFIG_SPL_LOAD_FIT) &&
		image_get_magic(&header) == FDT_MAGIC) {
		struct spl_load_info load;

		debug("Found FIT\n");
		load.dev = flash;
		load.priv = NULL;
		load.filename = NULL;
		load.bl_len = 1;
		load.read = spl_spi_fit_read;
		err = spl_load_simple_fit(spl_image, &load, offset, &header);
	} else {
		err = spl_parse_image_header(spl_image, &header);
		if (err)
			return err;
		err = spi_flash_read(flash, offset + sizeof(header),
				     spl_image->size,
				     (void *)spl_image->load_addr);
	}

	return err;
}

/*
 * The main entry for SPI booting. It's necessary that SDRAM is already
 * configured and available since this code loads the main U-Boot image
 * from SPI into SDRAM and starts it from there.
 */
static int spl_spi_load_image(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev)
{
	int err = 0;
	struct spi_flash *flash;
#if defined(CONFIG_SPL_OS_BOOT) && defined(CONFIG_SYS_SPI_KERNEL_OFFS)
	struct image_header header_stack;
	struct image_header *header = &header_stack;
#endif

	flash = spl_spi_probe();

#if defined(CONFIG_SPL_OS_BOOT) && defined(CONFIG_SYS_SPI_KERNEL_OFFS)
	if (spl_start_uboot() || spi_load_image_os(spl_image, flash, &header))
#endif
	{
		err = spl_spi_load_one_uimage(spl_image, flash,
					CONFIG_SYS_SPI_U_BOOT_OFFS);
	}

	return err;
}
/* Use priorty 1 so that boards can override this */
SPL_LOAD_IMAGE_METHOD("SPI", 1, BOOT_DEVICE_SPI, spl_spi_load_image);

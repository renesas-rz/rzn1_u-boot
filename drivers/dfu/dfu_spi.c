/*
 * dfu_spi.c -- DFU for SPI Flash devices.
 *
 * Copyright (C) 2015 Renesas Electronics Europe Ltd.
 * author: Michel Pollet <buserror@gmail.com>,<michel.pollet@bp.renesas.com>
 *
 * Bases on dfu_nand etc which is:
 * Copyright (C) 2012-2013 Texas Instruments, Inc.
 *
 * Based on dfu_mmc.c which is:
 * Copyright (C) 2012 Samsung Electronics
 * author: Lukasz Majewski <l.majewski@samsung.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <malloc.h>
#include <errno.h>
#include <div64.h>
#include <dfu.h>
#include <flash.h>
#include <spi_flash.h>

/* These had to be duplicated from cmd_sf */
#ifndef CONFIG_SF_DEFAULT_SPEED
# define CONFIG_SF_DEFAULT_SPEED	1000000
#endif
#ifndef CONFIG_SF_DEFAULT_MODE
# define CONFIG_SF_DEFAULT_MODE		SPI_MODE_3
#endif
#ifndef CONFIG_SF_DEFAULT_CS
# define CONFIG_SF_DEFAULT_CS		0
#endif
#ifndef CONFIG_SF_DEFAULT_BUS
# define CONFIG_SF_DEFAULT_BUS		0
#endif

static LIST_HEAD(dfu_spi_list);

/* The write operation is a bit convoluted, as we use a temporary erase block
 * buffer, load the existing block (that is generally cheap), update it with
 * the data and check whether it's being erased or if we are just clearing bits,
 * in which case it doesn't need to be erased before being written back.
 * This is made to try to limit the number of erase cycles on each block,
 * as they are so slow */
enum {
	EB_LOADED = (1 << 0),
	EB_DIRTY = (1 << 1),
	EB_EMPTY = (1 << 2),
	EB_ERASE = (1 << 3),
};

/* Update 'empty' flag */
static int eb_is_empty(struct dfu_spi_eraseblock *eb)
{
	uint32_t *src = (uint32_t *)eb->buf;
	int l = eb->size / 4;

	eb->state &= ~EB_EMPTY;
	while (l--)
		if (*src++ != ~0)
			return 0;
	eb->state |= EB_EMPTY;
	return 1;
}

static int eb_flush(struct dfu_spi_eraseblock *eb)
{
	int ret = 0;

	if (!(eb->state & EB_LOADED))
		return 0;
	if (!(eb->state & EB_DIRTY))
		return 0;

	/* if the data we write /sets/ bits, we need to erase the sector
	 * but if the data we write just clears bits, we don't
	 * this saves TONS of time as the erase cycle is so slow */
	if (eb->state & EB_ERASE) {
		debug("%s ERASE %08x\n", __func__, (int)eb->offset);
		ret = spi_flash_erase(eb->flash,
			eb->offset,
			eb->size);
	}
	/* proceed to write the new block */
	if (!ret && !(eb->state & EB_EMPTY)) {
		debug("%s write %08x\n", __func__, (int)eb->offset);

		ret = spi_flash_write(eb->flash,
			eb->offset,
			eb->size,
			eb->buf);
	}
	if (!ret)
		eb->state &= ~EB_DIRTY;
	return ret;
}

/*
 * Note that offset has to be /absolute/ relative to the whole device, not
 * relative to this DFU partition
 */
static int eb_load(struct dfu_spi_eraseblock *eb, u64 offset)
{
	u64 o = offset & ~(eb->size-1);
	int ret = 0;

	/* if it's already loaded.. */
	if ((eb->state & EB_LOADED) && (o == eb->offset))
		return ret;
	/* write the old one, if it was dirty */
	eb_flush(eb);
	eb->state = 0;
	eb->offset = o;
	debug("%s loaded %08x\n", __func__, (int)o);
	ret = spi_flash_read(eb->flash, o,
			eb->size,
			eb->buf);
	if (!ret) {
		eb->state = EB_LOADED;
		/* look to see if thats an empty block */
		eb_is_empty(eb);
	}
	return ret;
}

/*
 * Write to the device, 'offset' and 'len' are not necessarily aligned. So
 * we split it into erase block size writes, check whether we're supposed to
 * write the block, and also attempt to prevent an erase cycle if not
 * strictly necessary.
 * Note that offset has to be /absolute/ relative to the whole device, not
 * relative to this DFU partition
 */
static int eb_write(
	struct dfu_spi_eraseblock *eb, u64 offset,
	void *buf, long len)
{
	while (len > 0) {
		uint32_t start = offset & (eb->size-1);
		long size = start + len > eb->size ?
			eb->size - start : len;
		uint8_t *dst = eb->buf + start;
		uint8_t *src = buf;
		int i, do_flush = 0;

		debug("  offset %08lx start %4ld size %ld\n", (unsigned long)offset,
		      (long)start, (long)size);
		if (eb_load(eb, offset))
			return -1;

		/* Bail as soon as we detect we need to erase the page anyway */
		for (i = 0; i < size && !(eb->state & EB_ERASE); i++) {
			uint8_t changed = src[i] ^ dst[i];
			do_flush += !!changed;
			/* this one makes sense, mark block dirty */
			if (changed)
				eb->state |= EB_DIRTY;
			/* if we are SETTING bits, we need an erase cycle too */
			if (src[i] & changed)
				eb->state |= EB_ERASE;
			dst[i] = src[i];
		}
		/* If we bailed out early, copy the remaining
		 * the old fashioned way */
		if (i < size)
			memcpy(dst + i, src + i, size - i);
		/* if we changed anything, lets check to see if the buffer is
		 * empty, and flush it out */
		if (do_flush) {
			eb_is_empty(eb);
			if (eb_flush(eb))
				return -1;
		}
		offset += eb->size - start;
		len -= size;
		buf += size;
	}
	return 0;
}

static int dfu_write_medium_spi(struct dfu_entity *dfu,
		u64 offset, void *buf, long *len)
{
	int ret = -1;

	debug("%s %08lx size %ld\n", __func__, (unsigned long)offset, *len);

	switch (dfu->layout) {
	case DFU_RAW_ADDR:
		if (dfu->data.spi.eb) {
			ret = eb_write(dfu->data.spi.eb,
					dfu->data.spi.start + offset,
					buf, *len);
		} else {
			/* 'dumb' write, no spare/smart erase cycle */
			ret = spi_flash_erase(dfu->data.spi.flash,
				dfu->data.spi.start + offset, *len);
			if (!ret)
				ret = spi_flash_write(dfu->data.spi.flash,
					dfu->data.spi.start + offset, *len,
					buf);
		}
		break;
	default:
		printf("%s: Layout (%s) not (yet) supported!\n", __func__,
		       dfu_get_layout(dfu->layout));
	}

	return ret;
}

static int dfu_read_medium_spi(struct dfu_entity *dfu, u64 offset, void *buf,
		long *len)
{
	int ret = -1;

	switch (dfu->layout) {
	case DFU_RAW_ADDR:
		if (*len == 0) {
			*len = dfu->data.spi.size;
			ret = 0;
		} else {
			ret = spi_flash_read(dfu->data.spi.flash,
				offset + dfu->data.spi.start, *len, buf);
		}
		break;
	default:
		printf("%s: Layout (%s) not (yet) supported!\n", __func__,
		       dfu_get_layout(dfu->layout));
	}

	return ret;
}


static int dfu_flush_medium_spi(struct dfu_entity *dfu)
{
	debug("%s\n", __func__);
	if (dfu->data.spi.eb)
		eb_flush(dfu->data.spi.eb);

	return 0;
}

/* the erase cycle is very slow, so lets return
 * something to prevent a timeout */
unsigned int dfu_polltimeout_spi(struct dfu_entity *dfu)
{
	return DFU_DEFAULT_POLL_TIMEOUT * 100;
}

#ifdef DFU_EXT_INFO
static void dfu_spi_free(struct dfu_entity *dfu)
{
	if (dfu->data.spi.eb) {
		dfu->data.spi.eb->uses--;
		if (!dfu->data.spi.eb->uses)
			free(dfu->data.spi.eb);
	}
	list_del(&dfu->data.spi.list);
}
#endif

int dfu_fill_entity_spi(struct dfu_entity *dfu, char *s)
{
	char *st;
	struct spi_flash *flash = NULL;
	struct dfu_spi_eraseblock *eb = NULL;

#ifdef DFU_EXT_INFO
	/*
	 * Since multiple devices are possible, we need to keep track of the
	 * ones we've already probed to make sure we don't duplicate drivers
	 * for separate 'partitions'.
	 */
	struct dfu_entity *ext = NULL;

	/* clean the list up */
	dfu->free = dfu_spi_free;

	list_for_each_entry(ext, &dfu_spi_list, data.spi.list) {
		/* that device already probed */
		if (ext->dev_num == dfu->dev_num) {
			flash = ext->data.spi.flash;
			eb = ext->data.spi.eb;
			if (eb) /* count us in as a user */
				eb->uses++;
			break;
		}
	}
#endif
	if (!flash) {
		printf("%s PROBING flash dev:%d cs:%d\n", __func__,
		       dfu->dev_num / 8, dfu->dev_num % 8);
		flash = spi_flash_probe(
			dfu->dev_num / 8, dfu->dev_num % 8,
			CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
	}
	if (!flash) {
		printf("%s: probing failed dev:%d cs:%d\n",
		       __func__, dfu->dev_num / 8, dfu->dev_num % 8);
		return -1;
	}
	debug("DFU flash sector:%d page:%d erase:%d\n",
	      (int)flash->sector_size, (int)flash->page_size,
		(int)flash->erase_size);
	if (!eb) {
		int eb_size = flash->erase_size;

		eb = malloc(sizeof(*eb) + eb_size);
		if (eb) {
			// clear just the header
			memset(eb, 0, sizeof(*eb));
			eb->size = eb_size;
			eb->uses = 1;
			eb->flash = flash;
			debug("%s:%s allocated erase block for %d\n",
				__func__, dfu->name, (int)flash->erase_size);
		} else {
			printf("%s:%s warning could not alloc erase block (%d)!\n",
			       __func__, dfu->name, (int)flash->erase_size);
		}
	}

	INIT_LIST_HEAD(&dfu->data.spi.list);
	dfu->dev_type = DFU_DEV_FLASH;
	dfu->data.spi.flash = flash;
	dfu->data.spi.eb = eb;	/* can be NULL */
	st = strsep(&s, " ");
	if (!strcmp(st, "raw")) {
		dfu->layout = DFU_RAW_ADDR;
		dfu->data.spi.start = simple_strtoul(s, &s, 16);
		s++;
		dfu->data.spi.size = simple_strtoul(s, &s, 16);
		if (dfu->data.spi.size == 0) {
			dfu->data.spi.size = flash->size - dfu->data.spi.start;
			printf("%s:%s calculated size is %lx\n", __func__,
				dfu->name, (unsigned long)dfu->data.spi.size);
		}
		/* If we have a erase block cache, we don't need to worry too much
		 * about alignment, as the cached erase block is aligned */
		if (!eb) {
			/* make sure we're aligned on erase blocks */
			if ((dfu->data.spi.start & (flash->erase_size-1)) ||
			    (dfu->data.spi.size & (flash->erase_size-1))) {
				printf("%s:%s ERROR start/size not aligned to %d!\n",
				       __func__, dfu->name, (int)flash->erase_size);
				return -1;
			}
		}
	} else {
		printf("%s: Memory layout (%s) not supported!\n", __func__, st);
		return -1;
	}

	dfu->read_medium = dfu_read_medium_spi;
	dfu->write_medium = dfu_write_medium_spi;
	dfu->flush_medium = dfu_flush_medium_spi;
	dfu->poll_timeout = dfu_polltimeout_spi;
	debug("%s:%s probed fine %p\n", __func__, dfu->name,
	      dfu->data.spi.flash);
	/* initial state */
	dfu->inited = 0;
	list_add_tail(&dfu->data.spi.list, &dfu_spi_list);

	return 0;
}

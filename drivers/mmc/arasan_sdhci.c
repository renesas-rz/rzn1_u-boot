/*
 * Generic Arasan SD Host Controller Interface
 *
 * Based on:
 * Xilinx Zynq SD Host Controller Interface
 * (C) Copyright 2013 - 2015 Xilinx, Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <fdtdec.h>
#include <libfdt.h>
#include <malloc.h>
#include <sdhci.h>

DECLARE_GLOBAL_DATA_PTR;

static int arasan_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	const void *fdt = gd->fdt_blob;
	int node = dev->of_offset;
	int max_freq;

	host->bus_width	= fdtdec_get_int(fdt, node, "bus-width", 4);

	/*
	 * When we call sdhci_setup_cfg with a max_freq of 0, the SDHC code will
	 * attempt to get it from the SDHC registers.
	 */
	max_freq = fdtdec_get_int(fdt, node, "max-frequency", 0);

	if (fdtdec_get_bool(fdt, node, "no-1-8-v")) {
		host->quirks = SDHCI_QUIRK_BROKEN_VOLTAGE;
		host->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	}

	if (fdt_get_property(fdt, node, "non-removable", NULL))
		host->quirks |= SDHCI_QUIRK_NO_CD;

#ifdef CONFIG_SDHCI_ARASAN_QUIRKS
	host->quirks |= CONFIG_SDHCI_ARASAN_QUIRKS;
#endif

	add_sdhci(host, max_freq, 0);

	upriv->mmc = host->mmc;
	host->mmc->dev = dev;

	return 0;
}

static int arasan_sdhci_ofdata_to_platdata(struct udevice *dev)
{
	struct sdhci_host *host = dev_get_priv(dev);

	host->name = dev->name;
	host->ioaddr = (void *)dev_get_addr(dev);

	return 0;
}

static const struct udevice_id arasan_sdhci_ids[] = {
	{ .compatible = "arasan,sdhci-8.9a" },
	{ }
};

U_BOOT_DRIVER(arasan_sdhci_drv) = {
	.name		= "arasan_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= arasan_sdhci_ids,
	.ofdata_to_platdata = arasan_sdhci_ofdata_to_platdata,
	.probe		= arasan_sdhci_probe,
	.priv_auto_alloc_size = sizeof(struct sdhci_host),
};

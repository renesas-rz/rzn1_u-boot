/*
 * (C) Copyright 2007
 * Stelian Pop <stelian@popies.net>
 * Lead Tech Design <www.leadtechdesign.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#ifndef __ASM_ARM_DMA_MAPPING_H
#define __ASM_ARM_DMA_MAPPING_H

#define	dma_mapping_error(x, y)	0

enum dma_data_direction {
	DMA_BIDIRECTIONAL	= 0,
	DMA_TO_DEVICE		= 1,
	DMA_FROM_DEVICE		= 2,
};

/* U-Boot has a 1:1 mapping between physical and virtual addresses */
static inline unsigned long dma_map_single(volatile void *vaddr, size_t len,
					   enum dma_data_direction dir)
{
	unsigned long start = (unsigned long)vaddr & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = (unsigned long)vaddr + len - 1;

	end = (end + ARCH_DMA_MINALIGN) & ~(ARCH_DMA_MINALIGN - 1);

	if (len) {
		if (dir != DMA_FROM_DEVICE)
			flush_dcache_range(start, end);
		if (dir != DMA_TO_DEVICE)
			invalidate_dcache_range(start, end);
	}

	return (unsigned long)vaddr;
}

static inline void dma_unmap_single(volatile void *vaddr, size_t len,
				    unsigned long paddr)
{
}

static inline void *dma_alloc_coherent(size_t len, unsigned long *handle)
{
	void *vaddr = memalign(ARCH_DMA_MINALIGN, len);

	*handle = dma_map_single(vaddr, len, DMA_TO_DEVICE);

	return vaddr;
}

static inline void dma_free_coherent(void *addr)
{
	free(addr);
}

#endif /* __ASM_ARM_DMA_MAPPING_H */

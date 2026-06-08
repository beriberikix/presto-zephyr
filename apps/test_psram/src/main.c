/*
 * PSRAM bring-up test for the Pimoroni Presto.
 *
 * Confirms the 8 MB APS6404 (QMI window 1 @ 0x11000000) came up, exercises the
 * whole device with a few RAM test patterns, and allocates a large buffer from
 * the PSRAM mem-attr heap.
 *
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/mem_mgmt/mem_attr_heap.h>
#include <zephyr/dt-bindings/memory-attr/memory-attr-sw.h>

LOG_MODULE_REGISTER(test_psram, LOG_LEVEL_INF);

#define PSRAM_NODE DT_NODELABEL(psram)
#define PSRAM_BASE DT_REG_ADDR(PSRAM_NODE)
#define PSRAM_SIZE DT_REG_SIZE(PSRAM_NODE)

/* Provided by the PSRAM driver (drivers/presto/drivers/memc/psram_aps6404.c). */
extern size_t psram_get_size(void);

/* Write each word with its own address, then read the whole region back and
 * compare. Catches stuck/swapped address lines and window aliasing.
 */
static int test_address_in_address(volatile uint32_t *base, size_t words)
{
	for (size_t i = 0; i < words; i++) {
		base[i] = (uint32_t)(uintptr_t)&base[i];
	}

	for (size_t i = 0; i < words; i++) {
		uint32_t want = (uint32_t)(uintptr_t)&base[i];

		if (base[i] != want) {
			LOG_ERR("addr-in-addr fail @%p: got 0x%08x want 0x%08x",
				(void *)&base[i], base[i], want);
			return -EIO;
		}
		if ((i & 0x3ffff) == 0) {
			LOG_INF("  addr-in-addr %zu/%zu MiB",
				(i * sizeof(uint32_t)) >> 20, (size_t)PSRAM_SIZE >> 20);
		}
	}
	return 0;
}

/* Sweep a constant pattern across the whole region, read back and compare. */
static int test_pattern(volatile uint32_t *base, size_t words, uint32_t pat)
{
	for (size_t i = 0; i < words; i++) {
		base[i] = (i & 1) ? ~pat : pat;
	}
	for (size_t i = 0; i < words; i++) {
		uint32_t want = (i & 1) ? ~pat : pat;

		if (base[i] != want) {
			LOG_ERR("pattern 0x%08x fail @%p: got 0x%08x want 0x%08x",
				pat, (void *)&base[i], base[i], want);
			return -EIO;
		}
	}
	return 0;
}

/* Walking ones/zeros on a sampled set of words (catches data-line faults). */
static int test_walking_bits(volatile uint32_t *base, size_t words)
{
	for (size_t i = 0; i < words; i += 0x1000) {
		for (int b = 0; b < 32; b++) {
			uint32_t v = BIT(b);

			base[i] = v;
			if (base[i] != v) {
				LOG_ERR("walk-1 fail @%p bit %d", (void *)&base[i], b);
				return -EIO;
			}
			base[i] = ~v;
			if (base[i] != ~v) {
				LOG_ERR("walk-0 fail @%p bit %d", (void *)&base[i], b);
				return -EIO;
			}
		}
	}
	return 0;
}

static int test_heap(void)
{
	const size_t n = 4 * 1024 * 1024;
	int ret = mem_attr_heap_pool_init();

	if (ret != 0) {
		LOG_ERR("mem_attr_heap_pool_init failed: %d", ret);
		return ret;
	}

	uint8_t *buf = mem_attr_heap_alloc(DT_MEM_SW_ALLOC_DMA, n);

	if (buf == NULL) {
		LOG_ERR("mem_attr_heap_alloc(%zu) returned NULL", n);
		return -ENOMEM;
	}
	if ((uintptr_t)buf < PSRAM_BASE || (uintptr_t)buf + n > PSRAM_BASE + PSRAM_SIZE) {
		LOG_ERR("heap alloc %p not in PSRAM window [0x%08lx, 0x%08lx)", (void *)buf,
			(unsigned long)PSRAM_BASE, (unsigned long)(PSRAM_BASE + PSRAM_SIZE));
		mem_attr_heap_free(buf);
		return -EFAULT;
	}

	memset(buf, 0xa5, n);
	for (size_t i = 0; i < n; i++) {
		if (buf[i] != 0xa5) {
			LOG_ERR("heap buffer mismatch @%zu", i);
			mem_attr_heap_free(buf);
			return -EIO;
		}
	}
	mem_attr_heap_free(buf);
	LOG_INF("heap: alloc+verify+free of %zu MiB at %p OK", n >> 20, (void *)buf);
	return 0;
}

int main(void)
{
	volatile uint32_t *psram = (volatile uint32_t *)PSRAM_BASE;
	size_t detected = psram_get_size();

	LOG_INF("PSRAM test: DT region 0x%08lx + %u MiB, detected %zu MiB",
		(unsigned long)PSRAM_BASE, (unsigned int)(PSRAM_SIZE >> 20), detected >> 20);

	if (detected == 0) {
		LOG_ERR("PSRAM not detected — aborting");
		return 0;
	}

	size_t words = detected / sizeof(uint32_t);
	int fails = 0;

	LOG_INF("pass 1/4: address-in-address over %zu MiB", detected >> 20);
	fails += (test_address_in_address(psram, words) != 0);
	LOG_INF("pass 2/4: checkerboard 0x55/0xAA");
	fails += (test_pattern(psram, words, 0x55555555u) != 0);
	LOG_INF("pass 3/4: walking ones/zeros");
	fails += (test_walking_bits(psram, words) != 0);
	LOG_INF("pass 4/4: PSRAM mem-attr heap");
	fails += (test_heap() != 0);

	if (fails == 0) {
		LOG_INF("PSRAM OK: all passes succeeded on %zu MiB", detected >> 20);
	} else {
		LOG_ERR("PSRAM FAILED: %d pass(es) had errors", fails);
	}

	while (1) {
		k_msleep(1000);
	}
	return 0;
}

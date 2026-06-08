/*
 * SPDX-License-Identifier: MIT
 *
 * PSRAM screen: shows the detected 8 MB APS6404 size and its mapped base.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "screens.h"
#include "ui.h"

LOG_MODULE_REGISTER(screen_psram, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_PIMORONI_RP2350_PSRAM)

/* Provided by the PSRAM driver (drivers/presto/drivers/memc/psram_aps6404.c). */
extern size_t psram_get_size(void);

#define PSRAM_BASE DT_REG_ADDR(DT_NODELABEL(psram))

static size_t sz;

static int psram_enter(void)
{
	sz = psram_get_size();
	return sz ? 0 : -ENODEV;
}

static bool psram_update(void)
{
	return false; /* static content */
}

static void psram_render(void)
{
	char buf[24];

	ui_begin("PSRAM", COL_AMBER);

	snprintf(buf, sizeof(buf), "%u MiB", (unsigned)(sz >> 20));
	gfx_text(8, 64, buf, COL_WHITE, UI_BG, 3);

	snprintf(buf, sizeof(buf), "@ 0x%08lx", (unsigned long)PSRAM_BASE);
	gfx_text(8, 120, buf, COL_GREY, UI_BG, 2);

	gfx_text(8, 168, "APS6404 / QMI win1", COL_WHITE, UI_BG, 1);
	ui_footer();
}

static void psram_leave(void)
{
}

#else /* no PSRAM driver */

static int psram_enter(void)
{
	return 0;
}

static bool psram_update(void)
{
	return false;
}

static void psram_render(void)
{
	ui_begin("PSRAM", COL_AMBER);
	gfx_text(8, 96, "PSRAM n/a", COL_GREY, UI_BG, 2);
	ui_footer();
}

static void psram_leave(void)
{
}

#endif /* CONFIG_PIMORONI_RP2350_PSRAM */

static const struct screen instance = {
	.name = "psram",
	.enter = psram_enter,
	.update = psram_update,
	.render = psram_render,
	.leave = psram_leave,
};

const struct screen *screen_psram(void)
{
	return &instance;
}

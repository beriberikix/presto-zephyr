/*
 * SPDX-License-Identifier: MIT
 *
 * NeoPixel screen: drives the 7-LED chain with a rotating colour wheel and
 * mirrors it as on-screen swatches. On native_sim (no LED strip) it animates
 * the swatches alone.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "screens.h"
#include "ui.h"

LOG_MODULE_REGISTER(screen_neopixel, LOG_LEVEL_INF);

#define SWATCH_Y 64
#define SWATCH_H 96

#if IS_ENABLED(CONFIG_LED_STRIP)

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>

#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_LEN  DT_PROP_OR(STRIP_NODE, chain_length, 7)

/* Advance the colour wheel one position every WHEEL_TICKS ticks (TICK_MS=50ms),
 * i.e. ~5 steps/s — a gentle rotation instead of a blur. */
#define WHEEL_TICKS 4

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_LEN];
static size_t step;
static unsigned int ticks;
static bool have_strip;

static const struct led_rgb wheel[] = {
	{ .r = 0x18, .g = 0x00, .b = 0x00 }, { .r = 0x18, .g = 0x0c, .b = 0x00 },
	{ .r = 0x18, .g = 0x18, .b = 0x00 }, { .r = 0x00, .g = 0x18, .b = 0x00 },
	{ .r = 0x00, .g = 0x18, .b = 0x18 }, { .r = 0x00, .g = 0x00, .b = 0x18 },
	{ .r = 0x18, .g = 0x00, .b = 0x18 },
};

static void neopixel_show(void)
{
	for (size_t i = 0; i < STRIP_LEN; i++) {
		pixels[i] = wheel[(step + i) % ARRAY_SIZE(wheel)];
	}
	if (have_strip) {
		(void)led_strip_update_rgb(strip, pixels, STRIP_LEN);
	}
}

static int neopixel_enter(void)
{
	step = 0;
	ticks = 0;
	have_strip = device_is_ready(strip);
	if (!have_strip) {
		LOG_WRN("LED strip not ready");
	}
	neopixel_show(); /* paint the first frame so the wheel isn't blank on entry */
	return have_strip ? 0 : -ENODEV;
}

static bool neopixel_update(void)
{
	if (++ticks < WHEEL_TICKS) {
		return false; /* unchanged this tick — no LED write, no repaint */
	}
	ticks = 0;

	step++;
	neopixel_show();
	return true; /* swatches + LEDs advanced one position */
}

static void neopixel_render(void)
{
	int n = STRIP_LEN;
	int sw = gfx_width() / n;

	ui_begin("NeoPixel", COL_MAGENTA);
	for (int i = 0; i < n; i++) {
		struct led_rgb c = pixels[i];
		uint16_t col = ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3);

		gfx_fill_rect(i * sw, SWATCH_Y, sw - 2, SWATCH_H, col);
	}
	gfx_text(8, SWATCH_Y + SWATCH_H + 12, have_strip ? "7x SK6812" : "no strip",
		 COL_WHITE, UI_BG, 1);
	ui_footer();
}

static void neopixel_leave(void)
{
	if (have_strip) {
		memset(pixels, 0, sizeof(pixels));
		(void)led_strip_update_rgb(strip, pixels, STRIP_LEN);
	}
}

#else /* no LED strip (native_sim): animate swatches only */

static int step;

static int neopixel_enter(void)
{
	step = 0;
	return 0;
}

static bool neopixel_update(void)
{
	step++;
	return true;
}

static void neopixel_render(void)
{
	static const uint16_t wheel[] = { COL_RED,  COL_AMBER, COL_YELLOW, COL_GREEN,
					  COL_CYAN, COL_BLUE,  COL_MAGENTA };
	int n = ARRAY_SIZE(wheel);
	int sw = gfx_width() / n;

	ui_begin("NeoPixel", COL_MAGENTA);
	for (int i = 0; i < n; i++) {
		gfx_fill_rect(i * sw, SWATCH_Y, sw - 2, SWATCH_H,
			      wheel[(step + i) % n]);
	}
	gfx_text(8, SWATCH_Y + SWATCH_H + 12, "(simulated)", COL_GREY, UI_BG, 1);
	ui_footer();
}

static void neopixel_leave(void)
{
}

#endif /* CONFIG_LED_STRIP */

static const struct screen instance = {
	.name = "neopixel",
	.enter = neopixel_enter,
	.update = neopixel_update,
	.render = neopixel_render,
	.leave = neopixel_leave,
};

const struct screen *screen_neopixel(void)
{
	return &instance;
}

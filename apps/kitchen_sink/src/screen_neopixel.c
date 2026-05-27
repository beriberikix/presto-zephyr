/*
 * SPDX-License-Identifier: MIT
 *
 * NeoPixel screen: drives the 7-LED chain with a rotating colour wheel.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "screens.h"

LOG_MODULE_REGISTER(screen_neopixel, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_LED_STRIP)

static int neopixel_enter(void)
{
	LOG_WRN("LED strip disabled in this build");
	return -ENODEV;
}

static void neopixel_update(void)
{
}

static void neopixel_leave(void)
{
}

#else

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>

#define STRIP_NODE	DT_ALIAS(led_strip)
#define STRIP_LEN	DT_PROP_OR(STRIP_NODE, chain_length, 7)

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static struct led_rgb pixels[STRIP_LEN];
static size_t step;

static const struct led_rgb wheel[] = {
	{ .r = 0x18, .g = 0x00, .b = 0x00 },
	{ .r = 0x18, .g = 0x0c, .b = 0x00 },
	{ .r = 0x18, .g = 0x18, .b = 0x00 },
	{ .r = 0x00, .g = 0x18, .b = 0x00 },
	{ .r = 0x00, .g = 0x18, .b = 0x18 },
	{ .r = 0x00, .g = 0x00, .b = 0x18 },
	{ .r = 0x18, .g = 0x00, .b = 0x18 },
};

static int neopixel_enter(void)
{
	step = 0;

	if (!device_is_ready(strip)) {
		LOG_WRN("LED strip not ready — skipping");
		return -ENODEV;
	}

	LOG_INF("NeoPixel screen: %u LEDs", STRIP_LEN);
	return 0;
}

static void neopixel_update(void)
{
	if (!device_is_ready(strip)) {
		return;
	}

	for (size_t i = 0; i < STRIP_LEN; i++) {
		pixels[i] = wheel[(step + i) % ARRAY_SIZE(wheel)];
	}

	(void)led_strip_update_rgb(strip, pixels, STRIP_LEN);
	step++;
}

static void neopixel_leave(void)
{
	if (!device_is_ready(strip)) {
		return;
	}

	memset(pixels, 0, sizeof(pixels));
	(void)led_strip_update_rgb(strip, pixels, STRIP_LEN);
}

#endif /* CONFIG_LED_STRIP */

static const struct screen instance = {
	.name = "neopixel",
	.enter = neopixel_enter,
	.update = neopixel_update,
	.leave = neopixel_leave,
};

const struct screen *screen_neopixel(void)
{
	return &instance;
}

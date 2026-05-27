/*
 * SPDX-License-Identifier: MIT
 *
 * Cycles the Presto's 7-LED SK6812 chain through red, green, blue, off.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_leds, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_LED_STRIP)

int main(void)
{
	LOG_INF("LED strip support is not enabled in this build (CONFIG_LED_STRIP=n)");

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

#else

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>

#define STRIP_NODE	DT_ALIAS(led_strip)
#define STRIP_LEN	DT_PROP_OR(STRIP_NODE, chain_length, 7)

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static struct led_rgb pixels[STRIP_LEN];

static const struct led_rgb palette[] = {
	{ .r = 0x20, .g = 0x00, .b = 0x00 },
	{ .r = 0x00, .g = 0x20, .b = 0x00 },
	{ .r = 0x00, .g = 0x00, .b = 0x20 },
	{ .r = 0x00, .g = 0x00, .b = 0x00 },
};

int main(void)
{
	if (!device_is_ready(strip)) {
		LOG_ERR("LED strip device %s not ready", strip->name);
		return -ENODEV;
	}

	LOG_INF("Driving %u SK6812 LEDs on %s", STRIP_LEN, strip->name);

	size_t step = 0;

	while (1) {
		for (size_t i = 0; i < STRIP_LEN; i++) {
			pixels[i] = palette[(step + i) % ARRAY_SIZE(palette)];
		}

		int err = led_strip_update_rgb(strip, pixels, STRIP_LEN);

		if (err < 0) {
			LOG_ERR("led_strip_update_rgb failed: %d", err);
			return err;
		}

		step++;
		k_msleep(200);
	}

	return 0;
}

#endif /* CONFIG_LED_STRIP */
